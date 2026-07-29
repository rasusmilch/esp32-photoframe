#include <stdio.h>
#include <stdlib.h>

#include "connectivity_policy.h"

static int failures;
#define CHECK(x)                                                 \
    do {                                                         \
        if (!(x)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
            failures++;                                          \
        }                                                        \
    } while (0)

static boot_policy_result_t boot(connectivity_mode_t mode,
                                 connectivity_credential_state_t credentials, bool storage,
                                 bool retained, bool normal)
{
    boot_policy_input_t input = {mode, credentials, storage, retained, normal};
    return boot_policy_decide(&input);
}

static void test_boot(void)
{
    const connectivity_credential_state_t states[] = {
        CONNECTIVITY_CREDENTIALS_COMPLETE, CONNECTIVITY_CREDENTIALS_ABSENT,
        CONNECTIVITY_CREDENTIALS_INCOMPLETE, CONNECTIVITY_CREDENTIALS_ERROR};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        boot_policy_result_t result = boot(CONNECTIVITY_MODE_STORAGE, states[i], true, true, true);
        CHECK(result.local_services == BOOT_LOCAL_SERVICES_START_IMMEDIATELY);
        CHECK(result.network == BOOT_NETWORK_OPTIONAL);
        CHECK(result.retained_display == BOOT_RETAINED_DISPLAY_PRESERVE);
        CHECK(result.connection == (states[i] == CONNECTIVITY_CREDENTIALS_COMPLETE
                                        ? BOOT_CONNECTION_ELIGIBLE_ASYNC
                                        : BOOT_CONNECTION_NOT_ELIGIBLE));
        CHECK(result.status == (states[i] == CONNECTIVITY_CREDENTIALS_ERROR
                                    ? BOOT_POLICY_CREDENTIAL_STORE_HOLD
                                    : BOOT_POLICY_NORMAL));
    }
    boot_policy_result_t result =
        boot(CONNECTIVITY_MODE_STORAGE, CONNECTIVITY_CREDENTIALS_COMPLETE, false, false, true);
    CHECK(result.local_services == BOOT_LOCAL_SERVICES_UNAVAILABLE);
    result = boot(CONNECTIVITY_MODE_URL, CONNECTIVITY_CREDENTIALS_COMPLETE, true, true, true);
    CHECK(result.network == BOOT_NETWORK_REQUIRED);
    CHECK(result.local_services == BOOT_LOCAL_SERVICES_UNAVAILABLE);
    result = boot(CONNECTIVITY_MODE_URL, CONNECTIVITY_CREDENTIALS_ABSENT, true, false, true);
    CHECK(result.connection == BOOT_CONNECTION_NOT_ELIGIBLE);
    CHECK(result.provisioning == BOOT_PROVISIONING_ELIGIBLE_ASYNC);
    result = boot(CONNECTIVITY_MODE_STORAGE, CONNECTIVITY_CREDENTIALS_COMPLETE, true, true, false);
    CHECK(result.status == BOOT_POLICY_FAST_WAKE_OUTSIDE_SCOPE);
    CHECK(boot_policy_decide(NULL).status == BOOT_POLICY_INVALID_INPUT);
}

static connectivity_attempt_token_t start(connectivity_retry_state_t *state, uint64_t now)
{
    connectivity_attempt_token_t token = {0};
    CHECK(connectivity_retry_poll(state, now, &token) == CONNECTIVITY_RETRY_START_ATTEMPT);
    CHECK(state->attempt_outstanding);
    return token;
}

static void test_intervals(void)
{
    CHECK(connectivity_retry_effective_interval(0U) == UINT64_C(900000));
    CHECK(connectivity_retry_effective_interval(123U) == 123U);
    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    connectivity_attempt_token_t token = start(&state, 100U);
    CHECK(connectivity_retry_poll(&state, 100U, &token) == CONNECTIVITY_RETRY_NO_ACTION);
    CHECK(connectivity_retry_complete(&state, token, false, 100U, 0U) ==
          CONNECTIVITY_RETRY_SCHEDULED);
    CHECK(state.retry_deadline_ms == UINT64_C(900100));
    CHECK(connectivity_retry_poll(&state, state.retry_deadline_ms - 1U, &token) ==
          CONNECTIVITY_RETRY_NO_ACTION);
    token = start(&state, state.retry_deadline_ms);
    CHECK(connectivity_retry_complete(&state, token, false, state.retry_deadline_ms, 10U) ==
          CONNECTIVITY_RETRY_SCHEDULED);
    token = start(&state, state.retry_deadline_ms);
    CHECK(connectivity_retry_complete(&state, token, true, state.retry_deadline_ms, 10U) ==
          CONNECTIVITY_RETRY_CONNECTED);
    CHECK(state.connected && !state.retry_pending);

    connectivity_retry_init(&state, 9U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    token = start(&state, UINT64_MAX - 5U);
    CHECK(connectivity_retry_complete(&state, token, false, UINT64_MAX - 5U, 10U) ==
          CONNECTIVITY_RETRY_SCHEDULED);
    CHECK(state.retry_deadline_ms == UINT64_MAX);
    CHECK(connectivity_retry_poll(&state, UINT64_MAX - 1U, &token) == CONNECTIVITY_RETRY_NO_ACTION);
    (void) start(&state, UINT64_MAX);
}

static void test_replacement_races(void)
{
    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    connectivity_attempt_token_t old = start(&state, 0U);
    CHECK(connectivity_retry_update_credentials(&state, 2U, CONNECTIVITY_CREDENTIALS_COMPLETE) ==
          CONNECTIVITY_RETRY_NO_ACTION);
    CHECK(state.attempt_outstanding && state.outstanding_attempt.attempt_id == old.attempt_id);
    connectivity_attempt_token_t token = {0};
    CHECK(connectivity_retry_poll(&state, 0U, &token) == CONNECTIVITY_RETRY_NO_ACTION);
    CHECK(connectivity_retry_complete(&state, old, true, 0U, 0U) ==
          CONNECTIVITY_RETRY_OBSOLETE_RESULT);
    CHECK(!state.connected && !state.retry_pending && !state.attempt_outstanding);
    connectivity_attempt_token_t current = start(&state, 0U);
    CHECK(current.credential_generation == 2U && current.attempt_id != old.attempt_id);

    connectivity_retry_init(&state, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    old = start(&state, 0U);
    connectivity_retry_update_credentials(&state, 2U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    connectivity_retry_update_credentials(&state, 3U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    CHECK(connectivity_retry_poll(&state, 0U, &token) == CONNECTIVITY_RETRY_NO_ACTION);
    CHECK(connectivity_retry_complete(&state, old, false, 0U, 0U) ==
          CONNECTIVITY_RETRY_OBSOLETE_RESULT);
    current = start(&state, 0U);
    CHECK(current.credential_generation == 3U);

    const connectivity_credential_state_t unavailable[] = {CONNECTIVITY_CREDENTIALS_ABSENT,
                                                           CONNECTIVITY_CREDENTIALS_INCOMPLETE,
                                                           CONNECTIVITY_CREDENTIALS_ERROR};
    for (size_t i = 0; i < sizeof(unavailable) / sizeof(unavailable[0]); i++) {
        connectivity_retry_init(&state, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE);
        old = start(&state, 0U);
        connectivity_retry_update_credentials(&state, 2U, unavailable[i]);
        CHECK(connectivity_retry_poll(&state, 0U, &token) == CONNECTIVITY_RETRY_NO_ACTION);
        CHECK(connectivity_retry_complete(&state, old, false, 0U, 0U) ==
              CONNECTIVITY_RETRY_OBSOLETE_RESULT);
        CHECK(connectivity_retry_poll(&state, 0U, &token) ==
              CONNECTIVITY_RETRY_CREDENTIALS_UNAVAILABLE);
        connectivity_retry_update_credentials(&state, 3U, CONNECTIVITY_CREDENTIALS_COMPLETE);
        current = start(&state, 0U);
        CHECK(current.credential_generation == 3U);
    }
}

static void test_completion_and_cancellation(void)
{
    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    connectivity_attempt_token_t token = start(&state, 0U);
    connectivity_attempt_token_t unknown = {1U, token.attempt_id + 99U};
    CHECK(connectivity_retry_complete(&state, unknown, true, 0U, 0U) ==
          CONNECTIVITY_RETRY_UNKNOWN_ATTEMPT);
    CHECK(state.attempt_outstanding);
    CHECK(connectivity_retry_request_cancellation(&state, token) ==
          CONNECTIVITY_RETRY_CANCELLATION_REQUESTED);
    CHECK(state.attempt_outstanding);
    CHECK(connectivity_retry_poll(&state, 0U, &unknown) == CONNECTIVITY_RETRY_NO_ACTION);
    CHECK(connectivity_retry_acknowledge_cancellation(&state, unknown) ==
          CONNECTIVITY_RETRY_UNKNOWN_ATTEMPT);
    CHECK(connectivity_retry_acknowledge_cancellation(&state, token) ==
          CONNECTIVITY_RETRY_CANCELLATION_ACKNOWLEDGED);
    CHECK(!state.attempt_outstanding);
    CHECK(connectivity_retry_acknowledge_cancellation(&state, token) ==
          CONNECTIVITY_RETRY_NO_OUTSTANDING_ATTEMPT);
    token = start(&state, 0U);
    CHECK(connectivity_retry_complete(&state, token, true, 0U, 0U) == CONNECTIVITY_RETRY_CONNECTED);
    CHECK(connectivity_retry_complete(&state, token, true, 0U, 0U) ==
          CONNECTIVITY_RETRY_NO_OUTSTANDING_ATTEMPT);
}

static void test_connection_events(void)
{
    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 2U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    CHECK(connectivity_retry_observe_connected(&state, 1U) == CONNECTIVITY_RETRY_STALE_EVENT);
    CHECK(!state.connected);
    CHECK(connectivity_retry_observe_connected(&state, 2U) == CONNECTIVITY_RETRY_CONNECTED);
    CHECK(state.connected_generation == 2U);
    CHECK(connectivity_retry_observe_disconnected(&state, 1U, 10U, 20U) ==
          CONNECTIVITY_RETRY_STALE_EVENT);
    CHECK(state.connected && !state.retry_pending);
    CHECK(connectivity_retry_observe_disconnected(&state, 2U, 10U, 20U) ==
          CONNECTIVITY_RETRY_SCHEDULED);
    CHECK(!state.connected && state.retry_deadline_ms == 30U);
    connectivity_attempt_token_t token = start(&state, 30U);
    CHECK(connectivity_retry_complete(&state, token, true, 30U, 20U) ==
          CONNECTIVITY_RETRY_CONNECTED);

    uint64_t deadline = state.retry_deadline_ms;
    connectivity_attempt_token_t stale = {1U, 99U};
    CHECK(connectivity_retry_complete(&state, stale, false, 100U, 1U) ==
          CONNECTIVITY_RETRY_NO_OUTSTANDING_ATTEMPT);
    CHECK(state.retry_deadline_ms == deadline && state.connected);
}

static void test_invalid_inputs(void)
{
    connectivity_attempt_token_t token = {0};
    CHECK(connectivity_retry_poll(NULL, 0U, &token) == CONNECTIVITY_RETRY_INVALID_INPUT);
    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE);
    CHECK(connectivity_retry_poll(&state, 0U, NULL) == CONNECTIVITY_RETRY_INVALID_INPUT);
    CHECK(connectivity_retry_update_credentials(NULL, 1U, CONNECTIVITY_CREDENTIALS_COMPLETE) ==
          CONNECTIVITY_RETRY_INVALID_INPUT);
}

int main(void)
{
    test_boot();
    test_intervals();
    test_replacement_races();
    test_completion_and_cancellation();
    test_connection_events();
    test_invalid_inputs();
    if (failures != 0) {
        fprintf(stderr, "%d connectivity-policy test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("connectivity-policy tests passed");
    return EXIT_SUCCESS;
}
