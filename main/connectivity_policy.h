#ifndef CONNECTIVITY_POLICY_H
#define CONNECTIVITY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONNECTIVITY_RETRY_DEFAULT_INTERVAL_MS UINT64_C(900000)

typedef enum {
    CONNECTIVITY_MODE_STORAGE = 0,
    CONNECTIVITY_MODE_URL,
} connectivity_mode_t;

typedef enum {
    CONNECTIVITY_CREDENTIALS_COMPLETE = 0,
    CONNECTIVITY_CREDENTIALS_ABSENT,
    CONNECTIVITY_CREDENTIALS_INCOMPLETE,
    CONNECTIVITY_CREDENTIALS_ERROR,
} connectivity_credential_state_t;

typedef struct {
    connectivity_mode_t mode;
    connectivity_credential_state_t credentials;
    bool has_usable_persistent_storage;
    bool has_valid_retained_display;
    bool normal_startup;
} boot_policy_input_t;

typedef enum {
    BOOT_POLICY_NORMAL = 0,
    BOOT_POLICY_FAST_WAKE_OUTSIDE_SCOPE,
    BOOT_POLICY_CREDENTIAL_STORE_HOLD,
} boot_policy_status_t;

typedef enum {
    BOOT_LOCAL_SERVICES_UNAVAILABLE = 0,
    BOOT_LOCAL_SERVICES_START_IMMEDIATELY,
} boot_local_services_t;

typedef enum {
    BOOT_CONNECTION_NOT_ELIGIBLE = 0,
    BOOT_CONNECTION_ELIGIBLE_ASYNC,
} boot_connection_decision_t;

typedef enum {
    BOOT_PROVISIONING_NOT_ELIGIBLE = 0,
    BOOT_PROVISIONING_ELIGIBLE_ASYNC,
} boot_provisioning_decision_t;

typedef enum {
    BOOT_NETWORK_OPTIONAL = 0,
    BOOT_NETWORK_REQUIRED,
} boot_network_requirement_t;

typedef enum {
    BOOT_RETAINED_DISPLAY_NO_REQUIREMENT = 0,
    BOOT_RETAINED_DISPLAY_PRESERVE,
} boot_retained_display_t;

typedef struct {
    boot_policy_status_t status;
    boot_local_services_t local_services;
    boot_connection_decision_t connection;
    boot_provisioning_decision_t provisioning;
    boot_network_requirement_t network;
    boot_retained_display_t retained_display;
} boot_policy_result_t;

/* Declarative normal-startup policy. Fast wakes are explicitly outside its scope. */
boot_policy_result_t boot_policy_decide(const boot_policy_input_t *input);

typedef struct {
    uint64_t credential_generation;
    uint64_t retry_deadline_ms;
    bool attempt_active;
    bool retry_pending;
    bool connected;
} connectivity_retry_state_t;

typedef enum {
    CONNECTIVITY_RETRY_NO_ACTION = 0,
    CONNECTIVITY_RETRY_START_ATTEMPT,
    CONNECTIVITY_RETRY_CREDENTIALS_UNAVAILABLE,
    CONNECTIVITY_RETRY_SCHEDULED,
    CONNECTIVITY_RETRY_CONNECTED,
    CONNECTIVITY_RETRY_STALE_RESULT,
} connectivity_retry_action_t;

typedef struct {
    connectivity_credential_state_t credentials;
    uint64_t credential_generation;
    uint64_t now_ms;
    uint64_t configured_interval_ms; /* zero selects the 15-minute default */
    bool connected;
} connectivity_retry_input_t;

void connectivity_retry_init(connectivity_retry_state_t *state, uint64_t credential_generation);

/* Polls eligibility and reserves at most one attempt by setting attempt_active. */
connectivity_retry_action_t connectivity_retry_poll(connectivity_retry_state_t *state,
                                                    const connectivity_retry_input_t *input);

/* Applies a result only when its generation matches the reserved active attempt. */
connectivity_retry_action_t connectivity_retry_complete(connectivity_retry_state_t *state,
                                                        uint64_t attempt_generation, bool success,
                                                        uint64_t now_ms,
                                                        uint64_t configured_interval_ms);

uint64_t connectivity_retry_effective_interval(uint64_t configured_interval_ms);

#ifdef __cplusplus
}
#endif

#endif
