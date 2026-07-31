#include "wifi_import.h"

#include <string.h>

typedef struct {
    const char *start;
    size_t length;
} line_t;

static wifi_import_parse_result_t next_line(const char *data, size_t length, size_t *offset,
                                            line_t *line, bool *had_ending)
{
    size_t start = *offset;
    size_t end = start;
    while (end < length && data[end] != '\n' && data[end] != '\r') {
        if (data[end] == '\0') {
            return WIFI_IMPORT_PARSE_EMBEDDED_NUL;
        }
        end++;
    }
    line->start = data + start;
    line->length = end - start;
    *had_ending = end < length;
    if (end < length) {
        if (data[end] == '\r') {
            if (end + 1U >= length || data[end + 1U] != '\n') {
                return WIFI_IMPORT_PARSE_MALFORMED_LINE_ENDING;
            }
            end += 2U;
        } else {
            end++;
        }
    }
    *offset = end;
    return WIFI_IMPORT_PARSE_OK;
}

static wifi_import_parse_result_t copy_line(const line_t *line, char *destination, size_t capacity,
                                            wifi_import_parse_result_t overflow)
{
    if (line->length >= capacity) {
        return overflow;
    }
    memcpy(destination, line->start, line->length);
    destination[line->length] = '\0';
    return WIFI_IMPORT_PARSE_OK;
}

wifi_import_parse_result_t wifi_import_parse(const char *data, size_t length,
                                             wifi_import_candidate_t *candidate)
{
    if (data == NULL || candidate == NULL) {
        return WIFI_IMPORT_PARSE_EMPTY;
    }
    if (length == 0U) {
        return WIFI_IMPORT_PARSE_EMPTY;
    }
    if (length > WIFI_IMPORT_MAX_FILE_LEN) {
        return WIFI_IMPORT_PARSE_TOO_LARGE;
    }
    if (memchr(data, '\0', length) != NULL) {
        return WIFI_IMPORT_PARSE_EMBEDDED_NUL;
    }

    memset(candidate, 0, sizeof(*candidate));
    size_t offset = 0;
    line_t line;
    bool ended;
    wifi_import_parse_result_t result = next_line(data, length, &offset, &line, &ended);
    if (result != WIFI_IMPORT_PARSE_OK) {
        return result;
    }
    if (line.length == 0U) {
        return WIFI_IMPORT_PARSE_MISSING_SSID;
    }
    result =
        copy_line(&line, candidate->ssid, sizeof(candidate->ssid), WIFI_IMPORT_PARSE_SSID_OVERFLOW);
    if (result != WIFI_IMPORT_PARSE_OK) {
        return result;
    }
    if (!ended) {
        return WIFI_IMPORT_PARSE_MISSING_PASSWORD;
    }

    result = next_line(data, length, &offset, &line, &ended);
    if (result != WIFI_IMPORT_PARSE_OK) {
        return result;
    }
    result = copy_line(&line, candidate->password, sizeof(candidate->password),
                       WIFI_IMPORT_PARSE_PASSWORD_OVERFLOW);
    if (result != WIFI_IMPORT_PARSE_OK) {
        return result;
    }

    if (ended) {
        result = next_line(data, length, &offset, &line, &ended);
        if (result != WIFI_IMPORT_PARSE_OK) {
            return result;
        }
        if (line.length > 0U) {
            result = copy_line(&line, candidate->device_name, sizeof(candidate->device_name),
                               WIFI_IMPORT_PARSE_DEVICE_NAME_OVERFLOW);
            if (result != WIFI_IMPORT_PARSE_OK) {
                return result;
            }
            candidate->device_name_present = true;
        }
    }

    while (offset < length) {
        result = next_line(data, length, &offset, &line, &ended);
        if (result != WIFI_IMPORT_PARSE_OK) {
            return result;
        }
        if (line.length != 0U) {
            return WIFI_IMPORT_PARSE_EXCESS_DATA;
        }
    }
    return WIFI_IMPORT_PARSE_OK;
}

wifi_import_source_result_t wifi_import_discover(wifi_import_read_source_fn read_source,
                                                 void *context, char *destination, size_t capacity,
                                                 size_t *length, const char **selected_path)
{
    if (read_source == NULL || destination == NULL || length == NULL || selected_path == NULL) {
        return WIFI_IMPORT_SOURCE_READ_ERROR;
    }
    const char *paths[] = {WIFI_IMPORT_CONFIG_PATH, WIFI_IMPORT_ROOT_PATH};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        wifi_import_source_result_t result =
            read_source(context, paths[i], destination, capacity, length);
        if (result == WIFI_IMPORT_SOURCE_OK) {
            *selected_path = paths[i];
            return result;
        }
        if (result != WIFI_IMPORT_SOURCE_NOT_FOUND) {
            return result;
        }
    }
    return WIFI_IMPORT_SOURCE_NOT_FOUND;
}

static bool profiles_equal(const wifi_import_profile_t *left, const wifi_import_profile_t *right)
{
    return strcmp(left->ssid, right->ssid) == 0 && strcmp(left->password, right->password) == 0 &&
           strcmp(left->device_name, right->device_name) == 0;
}

wifi_import_outcome_t wifi_import_run(const wifi_import_ports_t *ports)
{
    if (ports == NULL || ports->read_source == NULL || ports->load_profile == NULL ||
        ports->commit_profile == NULL || ports->delete_source == NULL) {
        return WIFI_IMPORT_OUTCOME_SOURCE_READ_FAILED;
    }

    char data[WIFI_IMPORT_FILE_CAPACITY];
    size_t length = 0;
    const char *path = NULL;
    wifi_import_source_result_t source = wifi_import_discover(ports->read_source, ports->context,
                                                              data, sizeof(data), &length, &path);
    if (source == WIFI_IMPORT_SOURCE_NOT_FOUND) {
        return WIFI_IMPORT_OUTCOME_NO_SOURCE;
    }
    if (source == WIFI_IMPORT_SOURCE_TOO_LARGE) {
        return WIFI_IMPORT_OUTCOME_INVALID_SOURCE;
    }
    if (source != WIFI_IMPORT_SOURCE_OK) {
        return WIFI_IMPORT_OUTCOME_SOURCE_READ_FAILED;
    }

    wifi_import_candidate_t candidate;
    if (wifi_import_parse(data, length, &candidate) != WIFI_IMPORT_PARSE_OK) {
        return WIFI_IMPORT_OUTCOME_INVALID_SOURCE;
    }

    wifi_import_profile_t current = {0};
    wifi_import_profile_result_t current_result = ports->load_profile(ports->context, &current);
    if (current_result == WIFI_IMPORT_PROFILE_ERROR) {
        return WIFI_IMPORT_OUTCOME_PROFILE_LOAD_FAILED;
    }

    wifi_import_profile_t desired = current;
    memcpy(desired.ssid, candidate.ssid, sizeof(desired.ssid));
    memcpy(desired.password, candidate.password, sizeof(desired.password));
    if (candidate.device_name_present) {
        memcpy(desired.device_name, candidate.device_name, sizeof(desired.device_name));
    }

    bool already_applied =
        current_result == WIFI_IMPORT_PROFILE_OK && profiles_equal(&current, &desired);
    wifi_import_profile_t verified = current;
    if (!already_applied) {
        if (!ports->commit_profile(ports->context, &desired)) {
            return WIFI_IMPORT_OUTCOME_COMMIT_FAILED;
        }
        memset(&verified, 0, sizeof(verified));
        if (ports->load_profile(ports->context, &verified) != WIFI_IMPORT_PROFILE_OK ||
            !profiles_equal(&verified, &desired)) {
            return WIFI_IMPORT_OUTCOME_VERIFICATION_FAILED;
        }
    }

    if (ports->apply_verified_profile != NULL) {
        ports->apply_verified_profile(ports->context, &verified);
    }

    bool deleted = ports->delete_source(ports->context, path);
    if (already_applied) {
        return deleted ? WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETED
                       : WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETE_FAILED;
    }
    return deleted ? WIFI_IMPORT_OUTCOME_COMMITTED_DELETED
                   : WIFI_IMPORT_OUTCOME_COMMITTED_DELETE_FAILED;
}
