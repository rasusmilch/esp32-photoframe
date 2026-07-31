#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "provisioning_form.h"

static int failures;

#define CHECK(condition)                                                 \
    do {                                                                 \
        if (!(condition)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            failures++;                                                  \
        }                                                                \
    } while (0)

static provisioning_form_result_t parse(const char *body, provisioning_candidate_t *candidate)
{
    return provisioning_form_parse(body, strlen(body), candidate);
}

typedef struct {
    const char *data;
    size_t length;
    size_t offset;
    const int *results;
    size_t result_count;
    size_t result_offset;
} receiver_t;

static int scripted_receive(void *context, char *destination, size_t capacity)
{
    receiver_t *receiver = context;
    int result = receiver->result_offset < receiver->result_count
                     ? receiver->results[receiver->result_offset++]
                     : (int) (receiver->length - receiver->offset);
    if (result <= 0) {
        return result;
    }
    size_t count = (size_t) result;
    if (count > capacity) {
        count = capacity;
    }
    if (count > receiver->length - receiver->offset) {
        count = receiver->length - receiver->offset;
    }
    memcpy(destination, receiver->data + receiver->offset, count);
    receiver->offset += count;
    return (int) count;
}

static int overflowing_receive(void *context, char *destination, size_t capacity)
{
    (void) context;
    (void) destination;
    return (int) capacity + 1;
}

static void test_reads(void)
{
    const char *data = "abcdef";
    char output[8];
    const int one_byte[] = {1, 1, 1, 1, 1, 1};
    receiver_t receiver = {data, 6, 0, one_byte, 6, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_OK);
    CHECK(strcmp(output, data) == 0);

    const int arbitrary[] = {2, 1, 3};
    receiver = (receiver_t) {data, 6, 0, arbitrary, 3, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_OK);
    receiver = (receiver_t) {data, 6, 0, NULL, 0, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_OK);

    memset(output, 'Z', sizeof(output));
    CHECK(provisioning_read_exact(0, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_OK);
    CHECK(output[0] == '\0');
    CHECK(provisioning_read_exact(sizeof(output), output, sizeof(output), scripted_receive,
                                  &receiver) == PROVISIONING_READ_ERR_TOO_LARGE);

    const int eof[] = {2, 0};
    receiver = (receiver_t) {data, 6, 0, eof, 2, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_ERR_EOF);
    const int immediate_error[] = {PROVISIONING_RECV_ERROR};
    receiver = (receiver_t) {data, 6, 0, immediate_error, 1, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_ERR_RECEIVE);
    const int partial_error[] = {2, PROVISIONING_RECV_ERROR};
    receiver = (receiver_t) {data, 6, 0, partial_error, 2, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_ERR_RECEIVE);

    const int timeout_success[] = {PROVISIONING_RECV_TIMEOUT, 6};
    receiver = (receiver_t) {data, 6, 0, timeout_success, 2, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_OK);
    const int timeout_failure[] = {PROVISIONING_RECV_TIMEOUT, PROVISIONING_RECV_TIMEOUT,
                                   PROVISIONING_RECV_TIMEOUT, PROVISIONING_RECV_TIMEOUT};
    receiver = (receiver_t) {data, 6, 0, timeout_failure, 4, 0};
    CHECK(provisioning_read_exact(6, output, sizeof(output), scripted_receive, &receiver) ==
          PROVISIONING_READ_ERR_TIMEOUT);

    char boundary[6];
    memset(boundary, 'Z', sizeof(boundary));
    receiver = (receiver_t) {"1234", 4, 0, NULL, 0, 0};
    CHECK(provisioning_read_exact(4, boundary, 5, scripted_receive, &receiver) ==
          PROVISIONING_READ_OK);
    CHECK(boundary[4] == '\0' && boundary[5] == 'Z');
    CHECK(provisioning_read_exact(1, boundary, sizeof(boundary), overflowing_receive, NULL) ==
          PROVISIONING_READ_ERR_CALLBACK_OVERFLOW);
}

static void test_valid_forms(void)
{
    provisioning_candidate_t candidate;
    const char *normal =
        "ssid=My+WiFi&password=p%26%3D%25%2B+x&deviceName=Frame&ipMode=static&"
        "staticIp=192.168.1.2&staticNetmask=255.255.255.0&"
        "staticGateway=192.168.1.1&dnsServer=1.1.1.1";
    CHECK(parse(normal, &candidate) == PROVISIONING_FORM_OK);
    CHECK(strcmp(candidate.ssid, "My WiFi") == 0);
    CHECK(strcmp(candidate.password, "p&=%+ x") == 0);
    CHECK(candidate.use_static_ip && candidate.password_present && candidate.dns_server_present);

    CHECK(parse("unknown=a%26b&ipMode=dhcp&password=&ssid=x&deviceName=", &candidate) ==
          PROVISIONING_FORM_OK);
    CHECK(candidate.password_present && candidate.password[0] == '\0');
    CHECK(candidate.device_name_present && candidate.device_name[0] == '\0');
    CHECK(!candidate.use_static_ip);
    CHECK(parse("ssid=x", &candidate) == PROVISIONING_FORM_OK);
    CHECK(!candidate.password_present && !candidate.ip_mode_present);
    CHECK(parse("ipMode=&ssid=x", &candidate) == PROVISIONING_FORM_OK);
    CHECK(parse("ssid=%41%42%43%2b%2B", &candidate) == PROVISIONING_FORM_OK);
    CHECK(strcmp(candidate.ssid, "ABC++") == 0);
    CHECK(parse("ssid=a%2Bb+c", &candidate) == PROVISIONING_FORM_OK);
    CHECK(strcmp(candidate.ssid, "a+b c") == 0);
    CHECK(parse("dnsServer=&ssid=x", &candidate) == PROVISIONING_FORM_OK);
}

static void test_invalid_forms(void)
{
    provisioning_candidate_t candidate;
    const struct {
        const char *body;
        provisioning_form_result_t result;
    } cases[] = {
        {"", PROVISIONING_FORM_ERR_MISSING_SSID},
        {"password=x", PROVISIONING_FORM_ERR_MISSING_SSID},
        {"ssid=", PROVISIONING_FORM_ERR_MISSING_SSID},
        {"ssid=x&", PROVISIONING_FORM_ERR_MALFORMED},
        {"ssid", PROVISIONING_FORM_ERR_MALFORMED},
        {"=x", PROVISIONING_FORM_ERR_MALFORMED},
        {"ssid=%", PROVISIONING_FORM_ERR_BAD_ESCAPE},
        {"ssid=%0", PROVISIONING_FORM_ERR_BAD_ESCAPE},
        {"ssid=%GG", PROVISIONING_FORM_ERR_BAD_ESCAPE},
        {"ssid=%00", PROVISIONING_FORM_ERR_FORBIDDEN_BYTE},
        {"ssid=x%0D", PROVISIONING_FORM_ERR_FORBIDDEN_BYTE},
        {"ssid=x%0a", PROVISIONING_FORM_ERR_FORBIDDEN_BYTE},
        {"ssid=x&ssid=y", PROVISIONING_FORM_ERR_DUPLICATE_FIELD},
        {"ssid=x&ipMode=bogus", PROVISIONING_FORM_ERR_INVALID_IP_MODE},
        {"ssid=x&ipMode=static&staticNetmask=n&staticGateway=g",
         PROVISIONING_FORM_ERR_MISSING_STATIC_FIELD},
        {"ssid=x&ipMode=static&staticIp=i&staticGateway=g",
         PROVISIONING_FORM_ERR_MISSING_STATIC_FIELD},
        {"ssid=x&ipMode=static&staticIp=i&staticNetmask=n",
         PROVISIONING_FORM_ERR_MISSING_STATIC_FIELD},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(parse(cases[i].body, &candidate) == cases[i].result);
    }
}

static void test_lengths(void)
{
    provisioning_candidate_t candidate;
    char body[PROVISIONING_FORM_BODY_CAPACITY + 2U];
    const struct {
        const char *name;
        size_t capacity;
    } fields[] = {{"ssid", PROVISIONING_SSID_CAPACITY},
                  {"password", PROVISIONING_PASSWORD_CAPACITY},
                  {"deviceName", PROVISIONING_DEVICE_NAME_CAPACITY},
                  {"ipMode", PROVISIONING_IP_MODE_CAPACITY},
                  {"staticIp", PROVISIONING_IPV4_CAPACITY},
                  {"staticNetmask", PROVISIONING_IPV4_CAPACITY},
                  {"staticGateway", PROVISIONING_IPV4_CAPACITY},
                  {"dnsServer", PROVISIONING_IPV4_CAPACITY}};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        int prefix = snprintf(body, sizeof(body), "ssid=x&%s=", fields[i].name);
        if (strcmp(fields[i].name, "ssid") == 0) {
            prefix = snprintf(body, sizeof(body), "ssid=");
        }
        memset(body + prefix, 'a', fields[i].capacity);
        body[prefix + fields[i].capacity] = '\0';
        CHECK(parse(body, &candidate) == PROVISIONING_FORM_ERR_FIELD_OVERFLOW);
    }

    strcpy(body, "ssid=x&password=");
    size_t offset = strlen(body);
    for (size_t i = 0; i < 63U; i++) {
        memcpy(body + offset, "%21", 3U);
        offset += 3U;
    }
    body[offset] = '\0';
    CHECK(parse(body, &candidate) == PROVISIONING_FORM_OK);
    CHECK(strlen(candidate.password) == 63U);
    memcpy(body + offset, "%21", 3U);
    body[offset + 3U] = '\0';
    CHECK(parse(body, &candidate) == PROVISIONING_FORM_ERR_FIELD_OVERFLOW);

    memcpy(body, "ssid=x&u=", 9U);
    memset(body + 9U, 'a', PROVISIONING_FORM_MAX_BODY_LEN - 9U);
    CHECK(provisioning_form_parse(body, PROVISIONING_FORM_MAX_BODY_LEN, &candidate) ==
          PROVISIONING_FORM_OK);
    body[PROVISIONING_FORM_MAX_BODY_LEN] = 'a';
    CHECK(provisioning_form_parse(body, PROVISIONING_FORM_MAX_BODY_LEN + 1U, &candidate) ==
          PROVISIONING_FORM_ERR_BODY_TOO_LARGE);
}

int main(void)
{
    test_reads();
    test_valid_forms();
    test_invalid_forms();
    test_lengths();
    if (failures != 0) {
        fprintf(stderr, "%d provisioning-form test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("provisioning-form tests passed");
    return EXIT_SUCCESS;
}
