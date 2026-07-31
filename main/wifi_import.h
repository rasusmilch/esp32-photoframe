#ifndef WIFI_IMPORT_H
#define WIFI_IMPORT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_IMPORT_SSID_CAPACITY 32U
#define WIFI_IMPORT_PASSWORD_CAPACITY 64U
#define WIFI_IMPORT_DEVICE_NAME_CAPACITY 64U
#define WIFI_IMPORT_PATH_CAPACITY 32U
#define WIFI_IMPORT_CONFIG_PATH "/storage/config/wifi.txt"
#define WIFI_IMPORT_ROOT_PATH "/storage/wifi.txt"

/* 31 + 63 + 63 value bytes, two CRLF separators, and four trailing CRLFs. */
#define WIFI_IMPORT_MAX_FILE_LEN 169U
#define WIFI_IMPORT_FILE_CAPACITY (WIFI_IMPORT_MAX_FILE_LEN + 1U)

typedef enum {
    WIFI_IMPORT_PARSE_OK = 0,
    WIFI_IMPORT_PARSE_EMPTY,
    WIFI_IMPORT_PARSE_MISSING_SSID,
    WIFI_IMPORT_PARSE_MISSING_PASSWORD,
    WIFI_IMPORT_PARSE_SSID_OVERFLOW,
    WIFI_IMPORT_PARSE_PASSWORD_OVERFLOW,
    WIFI_IMPORT_PARSE_DEVICE_NAME_OVERFLOW,
    WIFI_IMPORT_PARSE_EMBEDDED_NUL,
    WIFI_IMPORT_PARSE_EXCESS_DATA,
    WIFI_IMPORT_PARSE_MALFORMED_LINE_ENDING,
    WIFI_IMPORT_PARSE_TOO_LARGE,
} wifi_import_parse_result_t;

typedef struct {
    char ssid[WIFI_IMPORT_SSID_CAPACITY];
    char password[WIFI_IMPORT_PASSWORD_CAPACITY];
    char device_name[WIFI_IMPORT_DEVICE_NAME_CAPACITY];
    bool device_name_present; /* false for an absent or empty third line */
} wifi_import_candidate_t;

/* Candidate owns all strings. Spaces and '#' are data; password line may be empty. */
wifi_import_parse_result_t wifi_import_parse(const char *data, size_t length,
                                             wifi_import_candidate_t *candidate);

typedef enum {
    WIFI_IMPORT_SOURCE_OK = 0,
    WIFI_IMPORT_SOURCE_NOT_FOUND,
    WIFI_IMPORT_SOURCE_OPEN_ERROR,
    WIFI_IMPORT_SOURCE_READ_ERROR,
    WIFI_IMPORT_SOURCE_TOO_LARGE,
    WIFI_IMPORT_SOURCE_NOT_SUPPORTED,
} wifi_import_source_result_t;

typedef wifi_import_source_result_t (*wifi_import_read_source_fn)(void *context, const char *path,
                                                                  char *destination,
                                                                  size_t capacity, size_t *length);

/* Tries config path first, then root; selected_path points to the exact constant path. */
wifi_import_source_result_t wifi_import_discover(wifi_import_read_source_fn read_source,
                                                 void *context, char *destination, size_t capacity,
                                                 size_t *length, const char **selected_path);

typedef struct {
    char ssid[WIFI_IMPORT_SSID_CAPACITY];
    char password[WIFI_IMPORT_PASSWORD_CAPACITY];
    char device_name[WIFI_IMPORT_DEVICE_NAME_CAPACITY];
} wifi_import_profile_t;

typedef enum {
    WIFI_IMPORT_PROFILE_OK = 0,
    WIFI_IMPORT_PROFILE_NONE,
    WIFI_IMPORT_PROFILE_INCOMPLETE,
    WIFI_IMPORT_PROFILE_ERROR,
} wifi_import_profile_result_t;

typedef struct {
    void *context;
    wifi_import_read_source_fn read_source;
    wifi_import_profile_result_t (*load_profile)(void *context, wifi_import_profile_t *profile);
    bool (*commit_profile)(void *context, const wifi_import_profile_t *profile);
    bool (*delete_source)(void *context, const char *path);
    void (*apply_verified_profile)(void *context, const wifi_import_profile_t *profile);
} wifi_import_ports_t;

typedef enum {
    WIFI_IMPORT_OUTCOME_NO_SOURCE = 0,
    WIFI_IMPORT_OUTCOME_INVALID_SOURCE,
    WIFI_IMPORT_OUTCOME_SOURCE_READ_FAILED,
    WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETED,
    WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETE_FAILED,
    WIFI_IMPORT_OUTCOME_COMMITTED_DELETED,
    WIFI_IMPORT_OUTCOME_COMMITTED_DELETE_FAILED,
    WIFI_IMPORT_OUTCOME_PROFILE_LOAD_FAILED,
    WIFI_IMPORT_OUTCOME_COMMIT_FAILED,
    WIFI_IMPORT_OUTCOME_VERIFICATION_FAILED,
} wifi_import_outcome_t;

/* Runs discover -> parse -> load -> optional commit -> readback verify -> cache -> exact delete. */
wifi_import_outcome_t wifi_import_run(const wifi_import_ports_t *ports);

#ifdef __cplusplus
}
#endif

#endif
