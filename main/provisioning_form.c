#include "provisioning_form.h"

#include <string.h>

typedef struct {
    const char *name;
    size_t name_len;
    char *destination;
    size_t capacity;
    bool *present;
} field_descriptor_t;

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static provisioning_form_result_t decode_value(const char *input, size_t input_len, char *output,
                                               size_t output_capacity)
{
    size_t input_offset = 0;
    size_t output_offset = 0;

    while (input_offset < input_len) {
        unsigned char decoded;
        if (input[input_offset] == '+') {
            decoded = ' ';
            input_offset++;
        } else if (input[input_offset] == '%') {
            if (input_len - input_offset < 3U) {
                return PROVISIONING_FORM_ERR_BAD_ESCAPE;
            }
            int high = hex_value(input[input_offset + 1U]);
            int low = hex_value(input[input_offset + 2U]);
            if (high < 0 || low < 0) {
                return PROVISIONING_FORM_ERR_BAD_ESCAPE;
            }
            decoded = (unsigned char) ((high << 4) | low);
            input_offset += 3U;
        } else {
            decoded = (unsigned char) input[input_offset++];
        }

        if (decoded == '\0' || decoded == '\r' || decoded == '\n') {
            return PROVISIONING_FORM_ERR_FORBIDDEN_BYTE;
        }
        if (output_offset + 1U >= output_capacity) {
            return PROVISIONING_FORM_ERR_FIELD_OVERFLOW;
        }
        if (output != NULL) {
            output[output_offset] = (char) decoded;
        }
        output_offset++;
    }
    if (output != NULL) {
        output[output_offset] = '\0';
    }
    return PROVISIONING_FORM_OK;
}

provisioning_form_result_t provisioning_form_parse(const char *body, size_t body_len,
                                                   provisioning_candidate_t *candidate)
{
    if (body == NULL || candidate == NULL || body_len > PROVISIONING_FORM_MAX_BODY_LEN) {
        return body_len > PROVISIONING_FORM_MAX_BODY_LEN ? PROVISIONING_FORM_ERR_BODY_TOO_LARGE
                                                         : PROVISIONING_FORM_ERR_MALFORMED;
    }

    memset(candidate, 0, sizeof(*candidate));
    bool ssid_present = false;
    field_descriptor_t fields[] = {
        {"ssid", 4U, candidate->ssid, sizeof(candidate->ssid), &ssid_present},
        {"password", 8U, candidate->password, sizeof(candidate->password),
         &candidate->password_present},
        {"deviceName", 10U, candidate->device_name, sizeof(candidate->device_name),
         &candidate->device_name_present},
        {"ipMode", 6U, candidate->ip_mode, sizeof(candidate->ip_mode), &candidate->ip_mode_present},
        {"staticIp", 8U, candidate->static_ip, sizeof(candidate->static_ip),
         &candidate->static_ip_present},
        {"staticNetmask", 13U, candidate->static_netmask, sizeof(candidate->static_netmask),
         &candidate->static_netmask_present},
        {"staticGateway", 13U, candidate->static_gateway, sizeof(candidate->static_gateway),
         &candidate->static_gateway_present},
        {"dnsServer", 9U, candidate->dns_server, sizeof(candidate->dns_server),
         &candidate->dns_server_present},
    };

    size_t offset = 0;
    while (offset < body_len) {
        size_t field_end = offset;
        while (field_end < body_len && body[field_end] != '&') {
            field_end++;
        }
        size_t equals = offset;
        while (equals < field_end && body[equals] != '=') {
            equals++;
        }
        if (equals == offset || equals == field_end) {
            return PROVISIONING_FORM_ERR_MALFORMED;
        }

        field_descriptor_t *recognized = NULL;
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
            if (equals - offset == fields[i].name_len &&
                memcmp(body + offset, fields[i].name, fields[i].name_len) == 0) {
                recognized = &fields[i];
                break;
            }
        }
        if (recognized != NULL && *recognized->present) {
            return PROVISIONING_FORM_ERR_DUPLICATE_FIELD;
        }

        provisioning_form_result_t result =
            decode_value(body + equals + 1U, field_end - equals - 1U,
                         recognized != NULL ? recognized->destination : NULL,
                         recognized != NULL ? recognized->capacity : (size_t) -1);
        if (result != PROVISIONING_FORM_OK) {
            return result;
        }
        if (recognized != NULL) {
            *recognized->present = true;
        }

        if (field_end == body_len) {
            offset = body_len;
        } else {
            offset = field_end + 1U;
            if (offset == body_len) {
                return PROVISIONING_FORM_ERR_MALFORMED;
            }
        }
    }

    if (!ssid_present || candidate->ssid[0] == '\0') {
        return PROVISIONING_FORM_ERR_MISSING_SSID;
    }
    if (!candidate->ip_mode_present || candidate->ip_mode[0] == '\0' ||
        strcmp(candidate->ip_mode, "dhcp") == 0) {
        candidate->use_static_ip = false;
    } else if (strcmp(candidate->ip_mode, "static") == 0) {
        candidate->use_static_ip = true;
        if (!candidate->static_ip_present || candidate->static_ip[0] == '\0' ||
            !candidate->static_netmask_present || candidate->static_netmask[0] == '\0' ||
            !candidate->static_gateway_present || candidate->static_gateway[0] == '\0') {
            return PROVISIONING_FORM_ERR_MISSING_STATIC_FIELD;
        }
    } else {
        return PROVISIONING_FORM_ERR_INVALID_IP_MODE;
    }
    return PROVISIONING_FORM_OK;
}

provisioning_read_result_t provisioning_read_exact(size_t expected_len, char *destination,
                                                   size_t destination_capacity,
                                                   provisioning_receive_fn receive,
                                                   void *receive_context)
{
    if (destination == NULL || receive == NULL || destination_capacity == 0U ||
        expected_len >= destination_capacity) {
        return PROVISIONING_READ_ERR_TOO_LARGE;
    }

    size_t received = 0;
    unsigned int timeouts = 0;
    while (received < expected_len) {
        size_t remaining = expected_len - received;
        int result = receive(receive_context, destination + received, remaining);
        if (result == PROVISIONING_RECV_TIMEOUT) {
            timeouts++;
            if (timeouts > PROVISIONING_EXACT_READ_MAX_TIMEOUTS) {
                return PROVISIONING_READ_ERR_TIMEOUT;
            }
            continue;
        }
        if (result < 0) {
            return PROVISIONING_READ_ERR_RECEIVE;
        }
        if (result == 0) {
            return PROVISIONING_READ_ERR_EOF;
        }
        if ((size_t) result > remaining) {
            return PROVISIONING_READ_ERR_CALLBACK_OVERFLOW;
        }
        received += (size_t) result;
    }
    destination[received] = '\0';
    return PROVISIONING_READ_OK;
}
