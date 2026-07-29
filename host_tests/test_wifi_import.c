#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wifi_import.h"

static int failures;
#define CHECK(x)                                                 \
    do {                                                         \
        if (!(x)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
            failures++;                                          \
        }                                                        \
    } while (0)

static wifi_import_parse_result_t parse(const char *text, wifi_import_candidate_t *candidate)
{
    return wifi_import_parse(text, strlen(text), candidate);
}

static void test_parser(void)
{
    wifi_import_candidate_t candidate;
    CHECK(parse("ssid\npass\nframe", &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(strcmp(candidate.ssid, "ssid") == 0 && strcmp(candidate.password, "pass") == 0);
    CHECK(candidate.device_name_present && strcmp(candidate.device_name, "frame") == 0);
    CHECK(parse("ssid\r\npass\r\nframe\r\n\r\n\r\n", &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(parse("ssid\npass\n", &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(!candidate.device_name_present);
    CHECK(parse("ssid\n\n", &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(candidate.password[0] == '\0');
    CHECK(parse("ssid\n#secret", &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(strcmp(candidate.password, "#secret") == 0);
    CHECK(parse("  ssid  \n  pass  ", &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(strcmp(candidate.ssid, "  ssid  ") == 0 && strcmp(candidate.password, "  pass  ") == 0);

    char input[WIFI_IMPORT_FILE_CAPACITY + 2U];
    memset(input, 's', 31U);
    input[31] = '\n';
    input[32] = '\n';
    CHECK(wifi_import_parse(input, 33U, &candidate) == WIFI_IMPORT_PARSE_OK);
    memset(input, 's', 32U);
    input[32] = '\n';
    input[33] = '\n';
    CHECK(wifi_import_parse(input, 34U, &candidate) == WIFI_IMPORT_PARSE_SSID_OVERFLOW);

    memcpy(input, "s\n", 2U);
    memset(input + 2U, 'p', 63U);
    CHECK(wifi_import_parse(input, 65U, &candidate) == WIFI_IMPORT_PARSE_OK);
    memset(input + 2U, 'p', 64U);
    CHECK(wifi_import_parse(input, 66U, &candidate) == WIFI_IMPORT_PARSE_PASSWORD_OVERFLOW);

    memcpy(input, "s\np\n", 4U);
    memset(input + 4U, 'd', 63U);
    CHECK(wifi_import_parse(input, 67U, &candidate) == WIFI_IMPORT_PARSE_OK);
    memset(input + 4U, 'd', 64U);
    CHECK(wifi_import_parse(input, 68U, &candidate) == WIFI_IMPORT_PARSE_DEVICE_NAME_OVERFLOW);

    CHECK(parse("", &candidate) == WIFI_IMPORT_PARSE_EMPTY);
    CHECK(parse("\npass", &candidate) == WIFI_IMPORT_PARSE_MISSING_SSID);
    CHECK(parse("ssid", &candidate) == WIFI_IMPORT_PARSE_MISSING_PASSWORD);
    CHECK(parse("ssid\npass\nname\nextra", &candidate) == WIFI_IMPORT_PARSE_EXCESS_DATA);
    CHECK(parse("ssid\rpass", &candidate) == WIFI_IMPORT_PARSE_MALFORMED_LINE_ENDING);
    const char nul[] = {'s', '\n', 'p', '\0', 'x'};
    CHECK(wifi_import_parse(nul, sizeof(nul), &candidate) == WIFI_IMPORT_PARSE_EMBEDDED_NUL);

    size_t offset = 0;
    memset(input + offset, 's', 31U);
    offset += 31U;
    memcpy(input + offset, "\r\n", 2U);
    offset += 2U;
    memset(input + offset, 'p', 63U);
    offset += 63U;
    memcpy(input + offset, "\r\n", 2U);
    offset += 2U;
    memset(input + offset, 'd', 63U);
    offset += 63U;
    memcpy(input + offset, "\r\n\r\n\r\n\r\n", 8U);
    offset += 8U;
    CHECK(offset == WIFI_IMPORT_MAX_FILE_LEN);
    CHECK(wifi_import_parse(input, offset, &candidate) == WIFI_IMPORT_PARSE_OK);
    CHECK(wifi_import_parse(input, WIFI_IMPORT_MAX_FILE_LEN + 1U, &candidate) ==
          WIFI_IMPORT_PARSE_TOO_LARGE);
}

typedef struct {
    const char *config_data;
    const char *root_data;
    wifi_import_source_result_t config_result;
    wifi_import_source_result_t root_result;
    int read_calls;
    char paths[2][WIFI_IMPORT_PATH_CAPACITY];
    wifi_import_profile_t stored;
    wifi_import_profile_result_t load_results[3];
    int load_calls;
    int commit_calls;
    int commit_fail_stage;
    int verification_fail_stage;
    int mismatch_field;
    int delete_calls;
    bool delete_ok;
    char deleted_path[WIFI_IMPORT_PATH_CAPACITY];
    int apply_calls;
    wifi_import_profile_t applied;
    int events[16];
    int event_count;
} fake_t;

enum { EVENT_READ = 1, EVENT_LOAD, EVENT_COMMIT, EVENT_APPLY, EVENT_DELETE };

static wifi_import_source_result_t fake_read(void *context, const char *path, char *destination,
                                             size_t capacity, size_t *length)
{
    fake_t *fake = context;
    fake->events[fake->event_count++] = EVENT_READ;
    int call = fake->read_calls++;
    strncpy(fake->paths[call], path, sizeof(fake->paths[call]) - 1U);
    bool config = strcmp(path, WIFI_IMPORT_CONFIG_PATH) == 0;
    wifi_import_source_result_t result = config ? fake->config_result : fake->root_result;
    const char *data = config ? fake->config_data : fake->root_data;
    if (result != WIFI_IMPORT_SOURCE_OK) {
        return result;
    }
    size_t size = strlen(data);
    if (size >= capacity) {
        return WIFI_IMPORT_SOURCE_TOO_LARGE;
    }
    memcpy(destination, data, size);
    *length = size;
    return WIFI_IMPORT_SOURCE_OK;
}

static wifi_import_profile_result_t fake_load(void *context, wifi_import_profile_t *profile)
{
    fake_t *fake = context;
    fake->events[fake->event_count++] = EVENT_LOAD;
    int call = fake->load_calls++;
    wifi_import_profile_result_t result = fake->load_results[call];
    *profile = fake->stored;
    if (call == 1 && fake->verification_fail_stage != 0) {
        return WIFI_IMPORT_PROFILE_ERROR;
    }
    if (call == 1 && fake->mismatch_field == 1) {
        profile->ssid[0] = 'x';
    } else if (call == 1 && fake->mismatch_field == 2) {
        profile->password[0] = 'x';
    } else if (call == 1 && fake->mismatch_field == 3) {
        profile->device_name[0] = 'x';
    }
    return result;
}

static bool fake_commit(void *context, const wifi_import_profile_t *profile)
{
    fake_t *fake = context;
    fake->events[fake->event_count++] = EVENT_COMMIT;
    fake->commit_calls++;
    if (fake->commit_fail_stage != 0) {
        return false;
    }
    fake->stored = *profile;
    return true;
}

static bool fake_delete(void *context, const char *path)
{
    fake_t *fake = context;
    fake->events[fake->event_count++] = EVENT_DELETE;
    fake->delete_calls++;
    strncpy(fake->deleted_path, path, sizeof(fake->deleted_path) - 1U);
    return fake->delete_ok;
}

static void fake_apply(void *context, const wifi_import_profile_t *profile)
{
    fake_t *fake = context;
    fake->events[fake->event_count++] = EVENT_APPLY;
    fake->apply_calls++;
    fake->applied = *profile;
}

static fake_t base_fake(void)
{
    fake_t fake = {
        .config_result = WIFI_IMPORT_SOURCE_OK,
        .root_result = WIFI_IMPORT_SOURCE_NOT_FOUND,
        .config_data = "new\nsecret\nNew Name",
        .delete_ok = true,
        .load_results = {WIFI_IMPORT_PROFILE_NONE, WIFI_IMPORT_PROFILE_OK, WIFI_IMPORT_PROFILE_OK}};
    strcpy(fake.stored.device_name, "Old Name");
    return fake;
}

static wifi_import_outcome_t run(fake_t *fake)
{
    wifi_import_ports_t ports = {.context = fake,
                                 .read_source = fake_read,
                                 .load_profile = fake_load,
                                 .commit_profile = fake_commit,
                                 .delete_source = fake_delete,
                                 .apply_verified_profile = fake_apply};
    return wifi_import_run(&ports);
}

static void test_discovery(void)
{
    char data[WIFI_IMPORT_FILE_CAPACITY];
    size_t length;
    const char *path;
    fake_t fake = base_fake();
    fake.root_result = WIFI_IMPORT_SOURCE_OK;
    fake.root_data = "root\npass";
    CHECK(wifi_import_discover(fake_read, &fake, data, sizeof(data), &length, &path) ==
          WIFI_IMPORT_SOURCE_OK);
    CHECK(strcmp(path, WIFI_IMPORT_CONFIG_PATH) == 0 && fake.read_calls == 1);

    fake = base_fake();
    fake.config_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    fake.root_result = WIFI_IMPORT_SOURCE_OK;
    fake.root_data = "root\npass";
    CHECK(wifi_import_discover(fake_read, &fake, data, sizeof(data), &length, &path) ==
          WIFI_IMPORT_SOURCE_OK);
    CHECK(strcmp(path, WIFI_IMPORT_ROOT_PATH) == 0 && fake.read_calls == 2);
    fake = base_fake();
    fake.config_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    fake.root_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    CHECK(wifi_import_discover(fake_read, &fake, data, sizeof(data), &length, &path) ==
          WIFI_IMPORT_SOURCE_NOT_FOUND);
    const wifi_import_source_result_t errors[] = {
        WIFI_IMPORT_SOURCE_OPEN_ERROR, WIFI_IMPORT_SOURCE_READ_ERROR, WIFI_IMPORT_SOURCE_TOO_LARGE};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        fake = base_fake();
        fake.config_result = errors[i];
        CHECK(wifi_import_discover(fake_read, &fake, data, sizeof(data), &length, &path) ==
              errors[i]);
        CHECK(fake.read_calls == 1);
    }
}

static void test_transactions(void)
{
    fake_t fake = base_fake();
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
    CHECK(fake.commit_calls == 1 && fake.load_calls == 2 && fake.apply_calls == 1 &&
          fake.delete_calls == 1);
    CHECK(strcmp(fake.deleted_path, WIFI_IMPORT_CONFIG_PATH) == 0);
    CHECK(fake.events[fake.event_count - 2] == EVENT_APPLY &&
          fake.events[fake.event_count - 1] == EVENT_DELETE);

    fake = base_fake();
    fake.load_results[0] = WIFI_IMPORT_PROFILE_OK;
    strcpy(fake.stored.ssid, "old");
    strcpy(fake.stored.password, "oldpass");
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
    CHECK(strcmp(fake.stored.ssid, "new") == 0 && strcmp(fake.stored.device_name, "New Name") == 0);

    fake = base_fake();
    fake.config_data = "new\nsecret";
    fake.load_results[0] = WIFI_IMPORT_PROFILE_OK;
    strcpy(fake.stored.ssid, "old");
    strcpy(fake.stored.password, "oldpass");
    strcpy(fake.stored.device_name, "Keep Me");
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
    CHECK(strcmp(fake.stored.device_name, "Keep Me") == 0);

    fake = base_fake();
    fake.load_results[0] = WIFI_IMPORT_PROFILE_OK;
    strcpy(fake.stored.ssid, "new");
    strcpy(fake.stored.password, "secret");
    strcpy(fake.stored.device_name, "New Name");
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETED);
    CHECK(fake.commit_calls == 0 && fake.load_calls == 1);

    fake = base_fake();
    fake.delete_ok = false;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETE_FAILED);
    fake.load_results[0] = WIFI_IMPORT_PROFILE_OK;
    fake.load_calls = fake.commit_calls = fake.delete_calls = fake.apply_calls = fake.event_count =
        0;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETE_FAILED);
    CHECK(fake.commit_calls == 0);

    fake = base_fake();
    fake.config_data = "bad";
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_INVALID_SOURCE);
    CHECK(fake.commit_calls == 0 && fake.delete_calls == 0 && fake.apply_calls == 0);
    fake = base_fake();
    fake.config_data = "bad";
    fake.load_results[0] = WIFI_IMPORT_PROFILE_OK;
    strcpy(fake.stored.ssid, "old");
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_INVALID_SOURCE);
    CHECK(strcmp(fake.stored.ssid, "old") == 0 && fake.load_calls == 0);
    fake = base_fake();
    fake.config_result = WIFI_IMPORT_SOURCE_READ_ERROR;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_SOURCE_READ_FAILED);
    CHECK(fake.commit_calls == 0 && fake.delete_calls == 0);

    for (int stage = 1; stage <= 5; stage++) {
        fake = base_fake();
        fake.commit_fail_stage = stage;
        wifi_import_profile_t before = fake.stored;
        CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMIT_FAILED);
        CHECK(memcmp(&before, &fake.stored, sizeof(before)) == 0);
        CHECK(fake.delete_calls == 0 && fake.apply_calls == 0);
    }

    for (int stage = 1; stage <= 4; stage++) {
        fake = base_fake();
        fake.verification_fail_stage = stage;
        CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_VERIFICATION_FAILED);
        CHECK(fake.delete_calls == 0 && fake.apply_calls == 0);
    }
    for (int field = 1; field <= 3; field++) {
        fake = base_fake();
        fake.mismatch_field = field;
        CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_VERIFICATION_FAILED);
        CHECK(fake.delete_calls == 0 && fake.apply_calls == 0);
    }

    fake = base_fake();
    fake.config_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    fake.root_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_NO_SOURCE);

    fake = base_fake();
    fake.config_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    fake.root_result = WIFI_IMPORT_SOURCE_OK;
    fake.root_data = "root\npass";
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
    CHECK(strcmp(fake.deleted_path, WIFI_IMPORT_ROOT_PATH) == 0);
}

static void test_incomplete_profiles(void)
{
    for (int missing = 1; missing <= 2; missing++) {
        fake_t fake = base_fake();
        fake.load_results[0] = WIFI_IMPORT_PROFILE_INCOMPLETE;
        strcpy(fake.stored.device_name, "Keep Me");
        if (missing == 1) {
            strcpy(fake.stored.password, "partial");
        } else {
            strcpy(fake.stored.ssid, "partial");
        }
        CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
        CHECK(fake.commit_calls == 1 && fake.load_calls == 2 && fake.apply_calls == 1 &&
              fake.delete_calls == 1);
        CHECK(fake.events[fake.event_count - 2] == EVENT_APPLY &&
              fake.events[fake.event_count - 1] == EVENT_DELETE);
        CHECK(strcmp(fake.stored.device_name, "New Name") == 0);
    }

    fake_t fake = base_fake();
    fake.config_data = "new\nsecret";
    fake.load_results[0] = WIFI_IMPORT_PROFILE_INCOMPLETE;
    strcpy(fake.stored.ssid, "partial");
    strcpy(fake.stored.device_name, "Keep Me");
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
    CHECK(strcmp(fake.stored.device_name, "Keep Me") == 0);

    fake = base_fake();
    fake.load_results[0] = WIFI_IMPORT_PROFILE_INCOMPLETE;
    strcpy(fake.stored.ssid, "new");
    strcpy(fake.stored.password, "secret");
    strcpy(fake.stored.device_name, "New Name");
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETED);
    CHECK(fake.commit_calls == 1); /* Incomplete is never treated as already applied. */

    fake = base_fake();
    fake.load_results[0] = WIFI_IMPORT_PROFILE_INCOMPLETE;
    fake.delete_ok = false;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_COMMITTED_DELETE_FAILED);
    CHECK(fake.commit_calls == 1);
    fake.load_results[0] = WIFI_IMPORT_PROFILE_OK;
    fake.load_calls = fake.commit_calls = fake.delete_calls = fake.apply_calls = fake.event_count =
        0;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_ALREADY_APPLIED_DELETE_FAILED);
    CHECK(fake.commit_calls == 0);

    fake = base_fake();
    fake.config_data = "invalid";
    fake.load_results[0] = WIFI_IMPORT_PROFILE_INCOMPLETE;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_INVALID_SOURCE);
    CHECK(fake.load_calls == 0 && fake.commit_calls == 0 && fake.apply_calls == 0 &&
          fake.delete_calls == 0);

    fake = base_fake();
    fake.config_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    fake.root_result = WIFI_IMPORT_SOURCE_NOT_FOUND;
    fake.load_results[0] = WIFI_IMPORT_PROFILE_INCOMPLETE;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_NO_SOURCE);
    CHECK(fake.load_calls == 0 && fake.commit_calls == 0 && fake.delete_calls == 0);

    fake = base_fake();
    fake.load_results[0] = WIFI_IMPORT_PROFILE_ERROR;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_PROFILE_LOAD_FAILED);
    CHECK(fake.commit_calls == 0 && fake.apply_calls == 0 && fake.delete_calls == 0);

    fake = base_fake();
    fake.load_results[1] = WIFI_IMPORT_PROFILE_ERROR;
    CHECK(run(&fake) == WIFI_IMPORT_OUTCOME_VERIFICATION_FAILED);
    CHECK(fake.commit_calls == 1 && fake.apply_calls == 0 && fake.delete_calls == 0);
}

int main(void)
{
    test_parser();
    test_discovery();
    test_transactions();
    test_incomplete_profiles();
    if (failures != 0) {
        fprintf(stderr, "%d wifi-import test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("wifi-import tests passed");
    return EXIT_SUCCESS;
}
