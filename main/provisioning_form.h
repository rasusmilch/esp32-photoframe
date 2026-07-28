#ifndef PROVISIONING_FORM_H
#define PROVISIONING_FORM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capacities include the terminating NUL and mirror the current firmware contracts. */
#define PROVISIONING_SSID_CAPACITY 32U
#define PROVISIONING_PASSWORD_CAPACITY 64U
#define PROVISIONING_DEVICE_NAME_CAPACITY 64U
#define PROVISIONING_IP_MODE_CAPACITY 8U
#define PROVISIONING_IPV4_CAPACITY 16U

/*
 * Worst-case application/x-www-form-urlencoded body for all eight supported fields:
 * 3 * (31 + 63 + 63 + 7 + 4 * 15) decoded value bytes
 * + 71 field-name bytes + 8 '=' bytes + 7 '&' bytes = 758 bytes.
 * The HTTP adapter allocates one additional byte for a terminating NUL.
 */
#define PROVISIONING_FORM_MAX_BODY_LEN 758U
#define PROVISIONING_FORM_BODY_CAPACITY (PROVISIONING_FORM_MAX_BODY_LEN + 1U)
#define PROVISIONING_EXACT_READ_MAX_TIMEOUTS 3U

typedef enum {
    PROVISIONING_FORM_OK = 0,
    PROVISIONING_FORM_ERR_BODY_TOO_LARGE,
    PROVISIONING_FORM_ERR_MALFORMED,
    PROVISIONING_FORM_ERR_BAD_ESCAPE,
    PROVISIONING_FORM_ERR_FORBIDDEN_BYTE,
    PROVISIONING_FORM_ERR_FIELD_OVERFLOW,
    PROVISIONING_FORM_ERR_DUPLICATE_FIELD,
    PROVISIONING_FORM_ERR_MISSING_SSID,
    PROVISIONING_FORM_ERR_INVALID_IP_MODE,
    PROVISIONING_FORM_ERR_MISSING_STATIC_FIELD,
} provisioning_form_result_t;

typedef struct {
    char ssid[PROVISIONING_SSID_CAPACITY];
    char password[PROVISIONING_PASSWORD_CAPACITY];
    char device_name[PROVISIONING_DEVICE_NAME_CAPACITY];
    char ip_mode[PROVISIONING_IP_MODE_CAPACITY];
    char static_ip[PROVISIONING_IPV4_CAPACITY];
    char static_netmask[PROVISIONING_IPV4_CAPACITY];
    char static_gateway[PROVISIONING_IPV4_CAPACITY];
    char dns_server[PROVISIONING_IPV4_CAPACITY];
    bool password_present;
    bool device_name_present;
    bool ip_mode_present;
    bool static_ip_present;
    bool static_netmask_present;
    bool static_gateway_present;
    bool dns_server_present;
    bool use_static_ip;
} provisioning_candidate_t;

/* Parses and validates recognized form fields without external side effects. */
provisioning_form_result_t provisioning_form_parse(const char *body, size_t body_len,
                                                   provisioning_candidate_t *candidate);

/* Receive callback results: positive byte count, EOF (0), error (-1), or timeout (-2). */
#define PROVISIONING_RECV_ERROR (-1)
#define PROVISIONING_RECV_TIMEOUT (-2)
typedef int (*provisioning_receive_fn)(void *context, char *destination, size_t capacity);

typedef enum {
    PROVISIONING_READ_OK = 0,
    PROVISIONING_READ_ERR_TOO_LARGE,
    PROVISIONING_READ_ERR_EOF,
    PROVISIONING_READ_ERR_RECEIVE,
    PROVISIONING_READ_ERR_TIMEOUT,
    PROVISIONING_READ_ERR_CALLBACK_OVERFLOW,
} provisioning_read_result_t;

/* Reads exactly expected_len bytes, permits three timeouts, and always NUL-terminates on success.
 */
provisioning_read_result_t provisioning_read_exact(size_t expected_len, char *destination,
                                                   size_t destination_capacity,
                                                   provisioning_receive_fn receive,
                                                   void *receive_context);

#ifdef __cplusplus
}
#endif

#endif
