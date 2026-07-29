#include "connectivity_policy.h"

#include <stddef.h>

boot_policy_result_t boot_policy_decide(const boot_policy_input_t *input)
{
    boot_policy_result_t result = {0};
    if (input == NULL || !input->normal_startup) {
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

void connectivity_retry_init(connectivity_retry_state_t *state, uint64_t credential_generation)
{
    if (state == NULL) {
        return;
    }
    *state = (connectivity_retry_state_t) {.credential_generation = credential_generation};
}

connectivity_retry_action_t connectivity_retry_poll(connectivity_retry_state_t *state,
                                                    const connectivity_retry_input_t *input)
{
    if (state == NULL || input == NULL) {
        return CONNECTIVITY_RETRY_NO_ACTION;
    }

    if (input->credential_generation != state->credential_generation) {
        state->credential_generation = input->credential_generation;
        state->attempt_active = false;
        state->retry_pending = false;
        state->connected = false;
    }

    if (input->credentials != CONNECTIVITY_CREDENTIALS_COMPLETE) {
        state->attempt_active = false;
        state->retry_pending = false;
        state->connected = false;
        return CONNECTIVITY_RETRY_CREDENTIALS_UNAVAILABLE;
    }

    if (input->connected) {
        bool transitioned = !state->connected;
        state->attempt_active = false;
        state->retry_pending = false;
        state->connected = true;
        return transitioned ? CONNECTIVITY_RETRY_CONNECTED : CONNECTIVITY_RETRY_NO_ACTION;
    }
    state->connected = false;

    if (state->attempt_active) {
        return CONNECTIVITY_RETRY_NO_ACTION;
    }
    if (state->retry_pending && input->now_ms < state->retry_deadline_ms) {
        return CONNECTIVITY_RETRY_NO_ACTION;
    }

    state->retry_pending = false;
    state->attempt_active = true;
    return CONNECTIVITY_RETRY_START_ATTEMPT;
}

connectivity_retry_action_t connectivity_retry_complete(connectivity_retry_state_t *state,
                                                        uint64_t attempt_generation, bool success,
                                                        uint64_t now_ms,
                                                        uint64_t configured_interval_ms)
{
    if (state == NULL || !state->attempt_active ||
        attempt_generation != state->credential_generation) {
        return CONNECTIVITY_RETRY_STALE_RESULT;
    }

    state->attempt_active = false;
    if (success) {
        state->retry_pending = false;
        state->connected = true;
        return CONNECTIVITY_RETRY_CONNECTED;
    }

    state->connected = false;
    state->retry_pending = true;
    state->retry_deadline_ms =
        saturating_add(now_ms, connectivity_retry_effective_interval(configured_interval_ms));
    return CONNECTIVITY_RETRY_SCHEDULED;
}
