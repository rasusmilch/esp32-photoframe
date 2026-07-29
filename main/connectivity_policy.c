#include "connectivity_policy.h"

#include <stddef.h>

boot_policy_result_t boot_policy_decide(const boot_policy_input_t *input)
{
    boot_policy_result_t result = {0};
    if (input == NULL) {
        result.status = BOOT_POLICY_INVALID_INPUT;
        return result;
    }
    if (!input->normal_startup) {
        result.status = BOOT_POLICY_FAST_WAKE_OUTSIDE_SCOPE;
        return result;
    }

    result.status = input->credentials == CONNECTIVITY_CREDENTIALS_ERROR
                        ? BOOT_POLICY_CREDENTIAL_STORE_HOLD
                        : BOOT_POLICY_NORMAL;
    result.local_services =
        input->mode == CONNECTIVITY_MODE_STORAGE && input->has_usable_persistent_storage
            ? BOOT_LOCAL_SERVICES_START_IMMEDIATELY
            : BOOT_LOCAL_SERVICES_UNAVAILABLE;
    result.network =
        input->mode == CONNECTIVITY_MODE_URL ? BOOT_NETWORK_REQUIRED : BOOT_NETWORK_OPTIONAL;
    result.connection = input->credentials == CONNECTIVITY_CREDENTIALS_COMPLETE
                            ? BOOT_CONNECTION_ELIGIBLE_ASYNC
                            : BOOT_CONNECTION_NOT_ELIGIBLE;
    result.provisioning = input->credentials == CONNECTIVITY_CREDENTIALS_ABSENT ||
                                  input->credentials == CONNECTIVITY_CREDENTIALS_INCOMPLETE
                              ? BOOT_PROVISIONING_ELIGIBLE_ASYNC
                              : BOOT_PROVISIONING_NOT_ELIGIBLE;
    result.retained_display = input->has_valid_retained_display
                                  ? BOOT_RETAINED_DISPLAY_PRESERVE
                                  : BOOT_RETAINED_DISPLAY_NO_REQUIREMENT;
    return result;
}

uint64_t connectivity_retry_effective_interval(uint64_t configured_interval_ms)
{
    return configured_interval_ms == 0U ? CONNECTIVITY_RETRY_DEFAULT_INTERVAL_MS
                                        : configured_interval_ms;
}

static uint64_t saturating_add(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static bool tokens_equal(connectivity_attempt_token_t left, connectivity_attempt_token_t right)
{
    return left.credential_generation == right.credential_generation &&
           left.attempt_id == right.attempt_id;
}

static void clear_connection_and_retry(connectivity_retry_state_t *state)
{
    state->connected = false;
    state->connected_generation = 0U;
    state->retry_pending = false;
    state->retry_generation = 0U;
}

void connectivity_retry_init(connectivity_retry_state_t *state, uint64_t credential_generation,
                             connectivity_credential_state_t credentials)
{
    if (state == NULL) {
        return;
    }
    *state = (connectivity_retry_state_t) {.desired_generation = credential_generation,
                                           .desired_credentials = credentials,
                                           .next_attempt_id = 1U};
}

connectivity_retry_action_t connectivity_retry_update_credentials(
    connectivity_retry_state_t *state, uint64_t credential_generation,
    connectivity_credential_state_t credentials)
{
    if (state == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (credential_generation != state->desired_generation) {
        state->desired_generation = credential_generation;
        clear_connection_and_retry(state);
    }
    state->desired_credentials = credentials;
    if (credentials != CONNECTIVITY_CREDENTIALS_COMPLETE) {
        clear_connection_and_retry(state);
    }
    return CONNECTIVITY_RETRY_NO_ACTION;
}

connectivity_retry_action_t connectivity_retry_poll(connectivity_retry_state_t *state,
                                                    uint64_t now_ms,
                                                    connectivity_attempt_token_t *token)
{
    if (state == NULL || token == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (state->attempt_outstanding) {
        return CONNECTIVITY_RETRY_NO_ACTION;
    }
    if (state->desired_credentials != CONNECTIVITY_CREDENTIALS_COMPLETE) {
        return CONNECTIVITY_RETRY_CREDENTIALS_UNAVAILABLE;
    }
    if (state->connected && state->connected_generation == state->desired_generation) {
        return CONNECTIVITY_RETRY_NO_ACTION;
    }
    if (state->retry_pending && state->retry_generation == state->desired_generation &&
        now_ms < state->retry_deadline_ms) {
        return CONNECTIVITY_RETRY_NO_ACTION;
    }
    if (state->next_attempt_id == UINT64_MAX) {
        return CONNECTIVITY_RETRY_TOKEN_EXHAUSTED;
    }
    state->outstanding_attempt = (connectivity_attempt_token_t) {
        .credential_generation = state->desired_generation,
        .attempt_id = state->next_attempt_id++,
    };
    state->attempt_outstanding = true;
    state->cancellation_requested = false;
    state->retry_pending = false;
    *token = state->outstanding_attempt;
    return CONNECTIVITY_RETRY_START_ATTEMPT;
}

connectivity_retry_action_t connectivity_retry_complete(connectivity_retry_state_t *state,
                                                        connectivity_attempt_token_t token,
                                                        bool success, uint64_t now_ms,
                                                        uint64_t configured_interval_ms)
{
    if (state == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (!state->attempt_outstanding) {
        return CONNECTIVITY_RETRY_NO_OUTSTANDING_ATTEMPT;
    }
    if (!tokens_equal(token, state->outstanding_attempt)) {
        return CONNECTIVITY_RETRY_UNKNOWN_ATTEMPT;
    }
    state->attempt_outstanding = false;
    state->cancellation_requested = false;
    if (token.credential_generation != state->desired_generation) {
        return CONNECTIVITY_RETRY_OBSOLETE_RESULT;
    }
    if (success) {
        state->retry_pending = false;
        state->connected = true;
        state->connected_generation = token.credential_generation;
        return CONNECTIVITY_RETRY_CONNECTED;
    }
    state->connected = false;
    state->retry_pending = true;
    state->retry_generation = token.credential_generation;
    state->retry_deadline_ms =
        saturating_add(now_ms, connectivity_retry_effective_interval(configured_interval_ms));
    return CONNECTIVITY_RETRY_SCHEDULED;
}

connectivity_retry_action_t connectivity_retry_request_cancellation(
    connectivity_retry_state_t *state, connectivity_attempt_token_t token)
{
    if (state == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (!state->attempt_outstanding) {
        return CONNECTIVITY_RETRY_NO_OUTSTANDING_ATTEMPT;
    }
    if (!tokens_equal(token, state->outstanding_attempt)) {
        return CONNECTIVITY_RETRY_UNKNOWN_ATTEMPT;
    }
    state->cancellation_requested = true;
    return CONNECTIVITY_RETRY_CANCELLATION_REQUESTED;
}

connectivity_retry_action_t connectivity_retry_acknowledge_cancellation(
    connectivity_retry_state_t *state, connectivity_attempt_token_t token)
{
    if (state == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (!state->attempt_outstanding) {
        return CONNECTIVITY_RETRY_NO_OUTSTANDING_ATTEMPT;
    }
    if (!tokens_equal(token, state->outstanding_attempt) || !state->cancellation_requested) {
        return CONNECTIVITY_RETRY_UNKNOWN_ATTEMPT;
    }
    state->attempt_outstanding = false;
    state->cancellation_requested = false;
    return CONNECTIVITY_RETRY_CANCELLATION_ACKNOWLEDGED;
}

connectivity_retry_action_t connectivity_retry_observe_connected(connectivity_retry_state_t *state,
                                                                 uint64_t credential_generation)
{
    if (state == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (credential_generation != state->desired_generation ||
        state->desired_credentials != CONNECTIVITY_CREDENTIALS_COMPLETE) {
        return CONNECTIVITY_RETRY_STALE_EVENT;
    }
    state->connected = true;
    state->connected_generation = credential_generation;
    state->retry_pending = false;
    return CONNECTIVITY_RETRY_CONNECTED;
}

connectivity_retry_action_t connectivity_retry_observe_disconnected(
    connectivity_retry_state_t *state, uint64_t credential_generation, uint64_t now_ms,
    uint64_t configured_interval_ms)
{
    if (state == NULL) {
        return CONNECTIVITY_RETRY_INVALID_INPUT;
    }
    if (!state->connected || state->connected_generation != credential_generation ||
        credential_generation != state->desired_generation) {
        return CONNECTIVITY_RETRY_STALE_EVENT;
    }
    state->connected = false;
    state->connected_generation = 0U;
    state->retry_pending = true;
    state->retry_generation = credential_generation;
    state->retry_deadline_ms =
        saturating_add(now_ms, connectivity_retry_effective_interval(configured_interval_ms));
    return CONNECTIVITY_RETRY_SCHEDULED;
}
