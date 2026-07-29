#include <stdio.h>
#include <stdlib.h>

#include "connectivity_policy.h"

static int failures;
#define CHECK(condition)                                                 \
    do {                                                                 \
        if (!(condition)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static boot_policy_result_t boot(connectivity_mode_t mode,
                                 connectivity_credential_state_t credentials, bool storage,
                                 bool retained, bool normal)
{
    boot_policy_input_t input = {.mode = mode,
                                 .credentials = credentials,
                                 .has_usable_persistent_storage = storage,
                                 .has_valid_retained_display = retained,
                                 .normal_startup = normal};
    return boot_policy_decide(&input);
}

static void test_boot_policy(void)
{
    const connectivity_credential_state_t states[] = {
        CONNECTIVITY_CREDENTIALS_COMPLETE, CONNECTIVITY_CREDENTIALS_ABSENT,
        CONNECTIVITY_CREDENTIALS_INCOMPLETE, CONNECTIVITY_CREDENTIALS_ERROR};
    for (size_t retained = 0; retained < 2U; retained++) {
        for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
            boot_policy_result_t result =
                boot(CONNECTIVITY_MODE_STORAGE, states[i], true, retained != 0U, true);
            CHECK(result.local_services == BOOT_LOCAL_SERVICES_START_IMMEDIATELY);
            CHECK(result.network == BOOT_NETWORK_OPTIONAL);
            CHECK(result.retained_display == (retained ? BOOT_RETAINED_DISPLAY_PRESERVE
                                                       : BOOT_RETAINED_DISPLAY_NO_REQUIREMENT));
            CHECK(result.connection == (states[i] == CONNECTIVITY_CREDENTIALS_COMPLETE
                                            ? BOOT_CONNECTION_ELIGIBLE_ASYNC
                                            : BOOT_CONNECTION_NOT_ELIGIBLE));
            CHECK(result.provisioning == ((states[i] == CONNECTIVITY_CREDENTIALS_ABSENT ||
                                           states[i] == CONNECTIVITY_CREDENTIALS_INCOMPLETE)
                                              ? BOOT_PROVISIONING_ELIGIBLE_ASYNC
                                              : BOOT_PROVISIONING_NOT_ELIGIBLE));
            CHECK(result.status == (states[i] == CONNECTIVITY_CREDENTIALS_ERROR
                                        ? BOOT_POLICY_CREDENTIAL_STORE_HOLD
                                        : BOOT_POLICY_NORMAL));
        }
    }

    boot_policy_result_t result =
        boot(CONNECTIVITY_MODE_STORAGE, CONNECTIVITY_CREDENTIALS_COMPLETE, false, false, true);
    CHECK(result.local_services == BOOT_LOCAL_SERVICES_UNAVAILABLE);
    CHECK(result.connection == BOOT_CONNECTION_ELIGIBLE_ASYNC);

    result = boot(CONNECTIVITY_MODE_URL, CONNECTIVITY_CREDENTIALS_COMPLETE, true, true, true);
    CHECK(result.network == BOOT_NETWORK_REQUIRED);
    CHECK(result.local_services == BOOT_LOCAL_SERVICES_UNAVAILABLE);
    CHECK(result.connection == BOOT_CONNECTION_ELIGIBLE_ASYNC);
    CHECK(result.retained_display == BOOT_RETAINED_DISPLAY_PRESERVE);

    result = boot(CONNECTIVITY_MODE_URL, CONNECTIVITY_CREDENTIALS_ABSENT, true, true, true);
    CHECK(result.network == BOOT_NETWORK_REQUIRED);
    CHECK(result.connection == BOOT_CONNECTION_NOT_ELIGIBLE);
    CHECK(result.provisioning == BOOT_PROVISIONING_ELIGIBLE_ASYNC);

    result = boot(CONNECTIVITY_MODE_STORAGE, CONNECTIVITY_CREDENTIALS_COMPLETE, true, true, false);
    CHECK(result.status == BOOT_POLICY_FAST_WAKE_OUTSIDE_SCOPE);
    CHECK(result.local_services == BOOT_LOCAL_SERVICES_UNAVAILABLE);
    CHECK(result.connection == BOOT_CONNECTION_NOT_ELIGIBLE);
    CHECK(result.provisioning == BOOT_PROVISIONING_NOT_ELIGIBLE);
}

static connectivity_retry_input_t retry_input(connectivity_credential_state_t credentials,
                                              uint64_t generation, uint64_t now, uint64_t interval,
                                              bool connected)
{
    return (connectivity_retry_input_t) {.credentials = credentials,
                                         .credential_generation = generation,
                                         .now_ms = now,
                                         .configured_interval_ms = interval,
                                         .connected = connected};
}

static void test_retry_intervals(void)
{
    CHECK(connectivity_retry_effective_interval(0U) == UINT64_C(900000));
    CHECK(connectivity_retry_effective_interval(123U) == 123U);

    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 7U);
    connectivity_retry_input_t input =
        retry_input(CONNECTIVITY_CREDENTIALS_COMPLETE, 7U, 100U, 0U, false);
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);
    CHECK(state.attempt_active);
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_NO_ACTION);
    CHECK(connectivity_retry_complete(&state, 7U, false, 100U, 0U) == CONNECTIVITY_RETRY_SCHEDULED);
    CHECK(state.retry_deadline_ms == UINT64_C(900100));
    input.now_ms = state.retry_deadline_ms - 1U;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_NO_ACTION);
    input.now_ms = state.retry_deadline_ms;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);
    CHECK(connectivity_retry_complete(&state, 7U, false, input.now_ms, 10U) ==
          CONNECTIVITY_RETRY_SCHEDULED);
    CHECK(state.retry_deadline_ms == input.now_ms + 10U);
    input.now_ms = state.retry_deadline_ms + 1U;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);
    CHECK(connectivity_retry_complete(&state, 7U, true, input.now_ms, 10U) ==
          CONNECTIVITY_RETRY_CONNECTED);
    CHECK(state.connected && !state.retry_pending && !state.attempt_active);
    input.connected = true;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_NO_ACTION);
}

static void test_retry_credentials_and_generation(void)
{
    const connectivity_credential_state_t unavailable[] = {CONNECTIVITY_CREDENTIALS_ABSENT,
                                                           CONNECTIVITY_CREDENTIALS_INCOMPLETE,
                                                           CONNECTIVITY_CREDENTIALS_ERROR};
    for (size_t i = 0; i < sizeof(unavailable) / sizeof(unavailable[0]); i++) {
        connectivity_retry_state_t state;
        connectivity_retry_init(&state, 1U);
        connectivity_retry_input_t input = retry_input(unavailable[i], 1U, 0U, 0U, false);
        CHECK(connectivity_retry_poll(&state, &input) ==
              CONNECTIVITY_RETRY_CREDENTIALS_UNAVAILABLE);
        CHECK(!state.attempt_active && !state.retry_pending && !state.connected);
    }

    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 1U);
    connectivity_retry_input_t input =
        retry_input(CONNECTIVITY_CREDENTIALS_COMPLETE, 1U, 0U, 0U, false);
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);

    input.credential_generation = 2U;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);
    CHECK(state.credential_generation == 2U && state.attempt_active);
    CHECK(connectivity_retry_complete(&state, 1U, true, 0U, 0U) == CONNECTIVITY_RETRY_STALE_RESULT);
    CHECK(state.attempt_active && !state.connected);
    CHECK(connectivity_retry_complete(&state, 1U, false, 0U, 0U) ==
          CONNECTIVITY_RETRY_STALE_RESULT);
    CHECK(!state.retry_pending);
    CHECK(connectivity_retry_complete(&state, 2U, false, 0U, 0U) == CONNECTIVITY_RETRY_SCHEDULED);

    input.connected = true;
    input.now_ms = 1U;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_CONNECTED);
    CHECK(!state.retry_pending && state.connected);
}

static void test_retry_saturation(void)
{
    connectivity_retry_state_t state;
    connectivity_retry_init(&state, 9U);
    connectivity_retry_input_t input =
        retry_input(CONNECTIVITY_CREDENTIALS_COMPLETE, 9U, UINT64_MAX - 5U, 10U, false);
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);
    CHECK(connectivity_retry_complete(&state, 9U, false, input.now_ms,
                                      input.configured_interval_ms) ==
          CONNECTIVITY_RETRY_SCHEDULED);
    CHECK(state.retry_deadline_ms == UINT64_MAX);
    input.now_ms = UINT64_MAX - 1U;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_NO_ACTION);
    input.now_ms = UINT64_MAX;
    CHECK(connectivity_retry_poll(&state, &input) == CONNECTIVITY_RETRY_START_ATTEMPT);
}

int main(void)
{
    test_boot_policy();
    test_retry_intervals();
    test_retry_credentials_and_generation();
    test_retry_saturation();
    if (failures != 0) {
        fprintf(stderr, "%d connectivity-policy test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("connectivity-policy tests passed");
    return EXIT_SUCCESS;
}
