#include "connectivity_runtime.h"

#include <inttypes.h>
#include <string.h>

#include "config.h"
#include "config_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "connectivity_runtime";

#define COMMAND_QUEUE_DEPTH 12
#define ATTEMPT_TIMEOUT_MS UINT64_C(30000)
#define QUIET_QUARANTINE_MS UINT64_C(2000)
#define STOP_MASK_STA 1U
#define STOP_MASK_AP 2U

ESP_EVENT_DEFINE_BASE(CONNECTIVITY_FENCE_EVENT);
enum { CONNECTIVITY_FENCE = 1 };

typedef enum {
    CMD_CONNECT,
    CMD_CONNECT_WAIT,
    CMD_START_AP,
    CMD_TEST_CANDIDATE,
    CMD_FINISH_PROVISIONING,
    CMD_STOP,
    CMD_SCAN,
    CMD_SET_PS,
    CMD_DRIVER_EVENT,
    CMD_FENCE,
} command_type_t;

typedef enum {
    PHYSICAL_FREE,
    PHYSICAL_ACTIVE,
    PHYSICAL_STOPPING,
    PHYSICAL_QUARANTINE,
    PHYSICAL_FAULT
} physical_state_t;

typedef struct {
    uint64_t epoch;
    connectivity_attempt_token_t token;
    uint64_t generation;
    wifi_mode_t requested_mode;
    uint8_t required_stop_mask;
} physical_context_t;

typedef struct runtime_command {
    command_type_t type;
    SemaphoreHandle_t done;
    esp_err_t result;
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASS_MAX_LEN];
    bool use_static_ip;
    char static_ip[IP_ADDR_STR_MAX_LEN];
    char netmask[IP_ADDR_STR_MAX_LEN];
    char gateway[IP_ADDR_STR_MAX_LEN];
    char dns[IP_ADDR_STR_MAX_LEN];
    uint32_t timeout_ms;
    wifi_ps_type_t ps;
    wifi_ap_record_t *scan_results;
    int scan_max;
    int scan_count;
    esp_event_base_t event_base;
    int32_t event_id;
    uint16_t disconnect_reason;
    physical_context_t context;
    struct runtime_command *completion_target;
} runtime_command_t;

typedef struct {
    QueueHandle_t queue;
    portMUX_TYPE lock;
    connectivity_retry_state_t retry;
    physical_context_t active;
    physical_state_t physical_state;
    uint64_t next_epoch;
    uint64_t attempt_deadline_ms;
    uint64_t quarantine_deadline_ms;
    uint8_t observed_stop_mask;
    bool fence_posted;
    bool connected;
    bool disconnected_after_success;
    bool result_known;
    bool result_success;
    bool provisioning;
    char desired_ssid[WIFI_SSID_MAX_LEN];
    char desired_password[WIFI_PASS_MAX_LEN];
    char candidate_dns[IP_ADDR_STR_MAX_LEN];
    uint64_t desired_generation;
    runtime_command_t *candidate_waiter;
    bool candidate_is_provisioning;
    uint64_t waiter_generation;
    uint64_t retry_interval_ms;
    runtime_command_t *stop_waiter;
} runtime_t;

static runtime_t s_runtime = {.lock = portMUX_INITIALIZER_UNLOCKED};

static uint64_t now_ms(void)
{
    return (uint64_t) esp_timer_get_time() / UINT64_C(1000);
}

static uint64_t retry_interval_ms(void)
{
    uint64_t interval;
    portENTER_CRITICAL(&s_runtime.lock);
    interval = s_runtime.retry_interval_ms;
    portEXIT_CRITICAL(&s_runtime.lock);
    return interval;
}

static bool contexts_equal(physical_context_t a, physical_context_t b)
{
    return a.epoch == b.epoch && a.token.attempt_id == b.token.attempt_id &&
           a.generation == b.generation && a.requested_mode == b.requested_mode;
}

static void snapshot_context(physical_context_t *out)
{
    portENTER_CRITICAL(&s_runtime.lock);
    *out = s_runtime.active;
    portEXIT_CRITICAL(&s_runtime.lock);
}

static void publish_context(physical_context_t context, physical_state_t state)
{
    portENTER_CRITICAL(&s_runtime.lock);
    s_runtime.active = context;
    s_runtime.physical_state = state;
    portEXIT_CRITICAL(&s_runtime.lock);
}

static void finish_command(runtime_command_t *command, esp_err_t result)
{
    if (command == NULL)
        return;
    if (command->completion_target != NULL)
        command = command->completion_target;
    command->result = result;
    xSemaphoreGive(command->done);
}

static void driver_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void) arg;
    runtime_command_t command = {.type = CMD_DRIVER_EVENT, .event_base = base, .event_id = id};
    snapshot_context(&command.context);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && data != NULL) {
        command.disconnect_reason = ((wifi_event_sta_disconnected_t *) data)->reason;
    }
    if (xQueueSend(s_runtime.queue, &command, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_runtime.lock);
        s_runtime.physical_state = PHYSICAL_FAULT;
        portEXIT_CRITICAL(&s_runtime.lock);
    }
}

static void fence_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void) arg;
    (void) base;
    (void) id;
    runtime_command_t command = {.type = CMD_FENCE};
    if (data != NULL)
        command.context = *(physical_context_t *) data;
    if (xQueueSend(s_runtime.queue, &command, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_runtime.lock);
        s_runtime.physical_state = PHYSICAL_FAULT;
        portEXIT_CRITICAL(&s_runtime.lock);
    }
}

static uint8_t stop_mask_for_mode(wifi_mode_t mode)
{
    if (mode == WIFI_MODE_STA)
        return STOP_MASK_STA;
    if (mode == WIFI_MODE_AP)
        return STOP_MASK_AP;
    return STOP_MASK_STA | STOP_MASK_AP;
}

static esp_err_t configure_sta(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    strlcpy((char *) config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *) config.sta.password, password, sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (error == ESP_OK)
        error = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return error;
}

static void apply_dns_override(void)
{
    const char *dns = s_runtime.candidate_dns[0] != '\0' ? s_runtime.candidate_dns
                                                         : config_manager_get_dns_server();
    if ((dns == NULL || dns[0] == '\0') && config_manager_get_ip_mode() == IP_MODE_STATIC)
        dns = config_manager_get_static_gateway();
    if (dns == NULL || dns[0] == '\0')
        return;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_dns_info_t info = {0};
    if (netif == NULL || esp_netif_str_to_ip4(dns, &info.ip.u_addr.ip4) != ESP_OK) {
        ESP_LOGE(TAG, "Configured DNS server is invalid");
        return;
    }
    info.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &info);
}

static esp_err_t apply_candidate_ip(const runtime_command_t *command)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL)
        return ESP_ERR_INVALID_STATE;
    if (!command->use_static_ip) {
        esp_netif_dhcpc_start(netif);
        return ESP_OK;
    }
    esp_netif_ip_info_t info = {0};
    if (esp_netif_str_to_ip4(command->static_ip, &info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(command->netmask, &info.netmask) != ESP_OK ||
        esp_netif_str_to_ip4(command->gateway, &info.gw) != ESP_OK)
        return ESP_ERR_INVALID_ARG;
    esp_netif_dhcpc_stop(netif);
    return esp_netif_set_ip_info(netif, &info);
}

static esp_err_t begin_physical(wifi_mode_t mode, connectivity_attempt_token_t token)
{
    physical_context_t context = {.epoch = ++s_runtime.next_epoch,
                                  .token = token,
                                  .generation = token.credential_generation,
                                  .requested_mode = mode,
                                  .required_stop_mask = stop_mask_for_mode(mode)};
    s_runtime.observed_stop_mask = 0;
    s_runtime.fence_posted = false;
    s_runtime.result_known = false;
    s_runtime.result_success = false;
    s_runtime.disconnected_after_success = false;
    publish_context(context, PHYSICAL_ACTIVE);
    esp_err_t error = esp_wifi_set_mode(mode);
    if (error == ESP_OK && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)) {
        error = configure_sta(s_runtime.desired_ssid, s_runtime.desired_password);
    }
    if (error == ESP_OK)
        error = esp_wifi_start();
    if (error == ESP_OK && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)) {
        error = esp_wifi_connect();
        s_runtime.attempt_deadline_ms = now_ms() + ATTEMPT_TIMEOUT_MS;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start physical epoch: %s", esp_err_to_name(error));
        s_runtime.physical_state = PHYSICAL_FAULT;
    }
    return error;
}

static void request_stop(void)
{
    if (s_runtime.physical_state != PHYSICAL_ACTIVE)
        return;
    s_runtime.observed_stop_mask = 0;
    s_runtime.fence_posted = false;
    publish_context(s_runtime.active, PHYSICAL_STOPPING);
    esp_err_t error = esp_wifi_stop();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_stop failed; physical slot retained: %s", esp_err_to_name(error));
        s_runtime.physical_state = PHYSICAL_FAULT;
    }
}

static void maybe_post_fence(void)
{
    if (s_runtime.physical_state != PHYSICAL_STOPPING || s_runtime.fence_posted ||
        (s_runtime.observed_stop_mask & s_runtime.active.required_stop_mask) !=
            s_runtime.active.required_stop_mask)
        return;
    physical_context_t copy = s_runtime.active;
    s_runtime.fence_posted = true;
    esp_err_t error =
        esp_event_post(CONNECTIVITY_FENCE_EVENT, CONNECTIVITY_FENCE, &copy, sizeof(copy), 0);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post physical release fence: %s", esp_err_to_name(error));
        s_runtime.physical_state = PHYSICAL_FAULT;
    }
}

static void release_physical(void)
{
    connectivity_attempt_token_t token = s_runtime.active.token;
    bool was_candidate = s_runtime.candidate_waiter != NULL &&
                         s_runtime.waiter_generation == s_runtime.active.generation;
    if (s_runtime.retry.attempt_outstanding) {
        if (s_runtime.retry.cancellation_requested)
            connectivity_retry_acknowledge_cancellation(&s_runtime.retry, token);
        else
            connectivity_retry_complete(&s_runtime.retry, token, s_runtime.result_success, now_ms(),
                                        retry_interval_ms());
    } else if (s_runtime.disconnected_after_success) {
        connectivity_retry_observe_disconnected(&s_runtime.retry, token.credential_generation,
                                                now_ms(), retry_interval_ms());
    }
    memset(&s_runtime.active, 0, sizeof(s_runtime.active));
    publish_context(s_runtime.active, PHYSICAL_FREE);
    if (was_candidate) {
        runtime_command_t *waiter = s_runtime.candidate_waiter;
        s_runtime.candidate_waiter = NULL;
        finish_command(waiter, s_runtime.result_success ? ESP_OK : ESP_FAIL);
    }
    if (s_runtime.stop_waiter != NULL) {
        runtime_command_t *waiter = s_runtime.stop_waiter;
        s_runtime.stop_waiter = NULL;
        finish_command(waiter, ESP_OK);
    }
}

static void start_desired_if_eligible(void)
{
    if (s_runtime.physical_state != PHYSICAL_FREE || s_runtime.provisioning)
        return;
    connectivity_attempt_token_t token;
    if (connectivity_retry_poll(&s_runtime.retry, now_ms(), &token) ==
        CONNECTIVITY_RETRY_START_ATTEMPT) {
        begin_physical(WIFI_MODE_STA, token);
    }
}

static void handle_driver_event(runtime_command_t *command)
{
    if (s_runtime.physical_state == PHYSICAL_FREE)
        return;
    if (!contexts_equal(command->context, s_runtime.active)) {
        ESP_LOGE(TAG, "Stale/mismatched WiFi event; physical slot retained");
        s_runtime.physical_state = PHYSICAL_FAULT;
        return;
    }
    if (s_runtime.physical_state == PHYSICAL_QUARANTINE) {
        ESP_LOGE(TAG, "Attributable WiFi event after fence; physical slot retained");
        s_runtime.physical_state = PHYSICAL_FAULT;
        return;
    }
    if (command->event_base == WIFI_EVENT && command->event_id == WIFI_EVENT_STA_CONNECTED) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL)
            esp_netif_create_ip6_linklocal(netif);
    } else if (command->event_base == IP_EVENT && command->event_id == IP_EVENT_STA_GOT_IP &&
               s_runtime.physical_state == PHYSICAL_ACTIVE && !s_runtime.result_known) {
        s_runtime.result_known = true;
        s_runtime.result_success = true;
        s_runtime.connected = true;
        apply_dns_override();
        connectivity_retry_complete(&s_runtime.retry, s_runtime.active.token, true, now_ms(),
                                    retry_interval_ms());
        ESP_LOGI(TAG, "Connected generation=%" PRIu64 " attempt=%" PRIu64,
                 s_runtime.active.generation, s_runtime.active.token.attempt_id);
        if (s_runtime.candidate_waiter != NULL &&
            s_runtime.waiter_generation == s_runtime.active.generation) {
            runtime_command_t *waiter = s_runtime.candidate_waiter;
            s_runtime.candidate_waiter = NULL;
            finish_command(waiter, ESP_OK);
        }
    } else if (command->event_base == WIFI_EVENT &&
               command->event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "STA disconnected generation=%" PRIu64 " attempt=%" PRIu64 " reason=%u",
                 s_runtime.active.generation, s_runtime.active.token.attempt_id,
                 command->disconnect_reason);
        s_runtime.connected = false;
        if (s_runtime.physical_state == PHYSICAL_ACTIVE) {
            if (s_runtime.result_known && s_runtime.result_success)
                s_runtime.disconnected_after_success = true;
            else {
                s_runtime.result_known = true;
                s_runtime.result_success = false;
            }
            if (s_runtime.candidate_waiter != NULL && s_runtime.candidate_is_provisioning) {
                s_runtime.active.required_stop_mask = STOP_MASK_AP;
                publish_context(s_runtime.active, PHYSICAL_ACTIVE);
                esp_err_t error = esp_wifi_set_mode(WIFI_MODE_AP);
                runtime_command_t *waiter = s_runtime.candidate_waiter;
                s_runtime.candidate_waiter = NULL;
                finish_command(waiter, error == ESP_OK ? ESP_FAIL : error);
            } else {
                request_stop();
            }
        }
    } else if (command->event_base == WIFI_EVENT && command->event_id == WIFI_EVENT_STA_STOP) {
        s_runtime.observed_stop_mask |= STOP_MASK_STA;
        maybe_post_fence();
    } else if (command->event_base == WIFI_EVENT && command->event_id == WIFI_EVENT_AP_STOP) {
        s_runtime.observed_stop_mask |= STOP_MASK_AP;
        maybe_post_fence();
    }
}

static void handle_command(runtime_command_t *command)
{
    switch (command->type) {
    case CMD_CONNECT:
    case CMD_CONNECT_WAIT: {
        strlcpy(s_runtime.desired_ssid, command->ssid, sizeof(s_runtime.desired_ssid));
        strlcpy(s_runtime.desired_password, command->password, sizeof(s_runtime.desired_password));
        s_runtime.candidate_dns[0] = '\0';
        s_runtime.desired_generation++;
        connectivity_retry_update_credentials(&s_runtime.retry, s_runtime.desired_generation,
                                              CONNECTIVITY_CREDENTIALS_COMPLETE);
        if (s_runtime.physical_state == PHYSICAL_ACTIVE) {
            connectivity_retry_request_cancellation(&s_runtime.retry, s_runtime.active.token);
            request_stop();
        }
        start_desired_if_eligible();
        if (command->type == CMD_CONNECT_WAIT) {
            s_runtime.candidate_waiter = command->completion_target;
            s_runtime.candidate_is_provisioning = false;
            s_runtime.waiter_generation = s_runtime.desired_generation;
        } else
            finish_command(command, ESP_OK);
        break;
    }
    case CMD_START_AP: {
        if (s_runtime.physical_state != PHYSICAL_FREE) {
            finish_command(command, ESP_ERR_INVALID_STATE);
            break;
        }
        s_runtime.provisioning = true;
        wifi_config_t ap = {.ap = {.channel = 1, .max_connection = 4, .authmode = WIFI_AUTH_OPEN}};
        strlcpy((char *) ap.ap.ssid, command->ssid, sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen(command->ssid);
        connectivity_attempt_token_t token = {
            .credential_generation = ++s_runtime.desired_generation,
            .attempt_id = s_runtime.retry.next_attempt_id++};
        physical_context_t context = {.epoch = ++s_runtime.next_epoch,
                                      .token = token,
                                      .generation = token.credential_generation,
                                      .requested_mode = WIFI_MODE_APSTA,
                                      .required_stop_mask = STOP_MASK_STA | STOP_MASK_AP};
        publish_context(context, PHYSICAL_ACTIVE);
        esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (error == ESP_OK)
            error = esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (error == ESP_OK)
            error = esp_wifi_start();
        if (error != ESP_OK)
            s_runtime.physical_state = PHYSICAL_FAULT;
        finish_command(command, error);
        break;
    }
    case CMD_TEST_CANDIDATE:
        if (!s_runtime.provisioning || s_runtime.physical_state != PHYSICAL_ACTIVE ||
            s_runtime.candidate_waiter != NULL) {
            finish_command(command, ESP_ERR_INVALID_STATE);
            break;
        }
        strlcpy(s_runtime.desired_ssid, command->ssid, sizeof(s_runtime.desired_ssid));
        strlcpy(s_runtime.desired_password, command->password, sizeof(s_runtime.desired_password));
        s_runtime.desired_generation++;
        s_runtime.active.generation = s_runtime.desired_generation;
        s_runtime.active.token.credential_generation = s_runtime.desired_generation;
        s_runtime.active.token.attempt_id = s_runtime.retry.next_attempt_id++;
        publish_context(s_runtime.active, PHYSICAL_ACTIVE);
        s_runtime.result_known = false;
        s_runtime.result_success = false;
        s_runtime.candidate_waiter = command->completion_target;
        s_runtime.candidate_is_provisioning = true;
        s_runtime.waiter_generation = s_runtime.desired_generation;
        s_runtime.active.required_stop_mask = STOP_MASK_STA | STOP_MASK_AP;
        publish_context(s_runtime.active, PHYSICAL_ACTIVE);
        strlcpy(s_runtime.candidate_dns, command->dns, sizeof(s_runtime.candidate_dns));
        command->result = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (command->result == ESP_OK)
            command->result = apply_candidate_ip(command);
        if (command->result == ESP_OK)
            command->result = configure_sta(command->ssid, command->password);
        if (command->result == ESP_OK)
            command->result = esp_wifi_connect();
        s_runtime.attempt_deadline_ms = now_ms() + command->timeout_ms;
        if (command->result != ESP_OK) {
            s_runtime.result_known = true;
            runtime_command_t *waiter = s_runtime.candidate_waiter;
            s_runtime.candidate_waiter = NULL;
            finish_command(waiter, command->result);
        }
        break;
    case CMD_FINISH_PROVISIONING:
        if (!s_runtime.provisioning || s_runtime.physical_state != PHYSICAL_ACTIVE ||
            !s_runtime.connected) {
            finish_command(command, ESP_ERR_INVALID_STATE);
            break;
        }
        s_runtime.active.required_stop_mask = STOP_MASK_STA;
        publish_context(s_runtime.active, PHYSICAL_ACTIVE);
        command->result = esp_wifi_set_mode(WIFI_MODE_STA);
        if (command->result == ESP_OK)
            s_runtime.provisioning = false;
        finish_command(command, command->result);
        break;
    case CMD_STOP:
        if (s_runtime.physical_state == PHYSICAL_FREE)
            finish_command(command, ESP_OK);
        else if (s_runtime.physical_state == PHYSICAL_ACTIVE) {
            if (s_runtime.connected) {
                s_runtime.connected = false;
                s_runtime.disconnected_after_success = true;
            }
            s_runtime.stop_waiter = command->completion_target;
            request_stop();
        } else
            finish_command(command, ESP_ERR_INVALID_STATE);
        break;
    case CMD_SCAN: {
        if (!s_runtime.provisioning || s_runtime.physical_state != PHYSICAL_ACTIVE ||
            s_runtime.candidate_waiter) {
            finish_command(command, ESP_ERR_INVALID_STATE);
            break;
        }
        wifi_scan_config_t config = {0};
        esp_err_t error = esp_wifi_scan_start(&config, true);
        uint16_t count = (uint16_t) command->scan_max;
        if (error == ESP_OK)
            error = esp_wifi_scan_get_ap_records(&count, command->scan_results);
        command->scan_count = error == ESP_OK ? count : 0;
        finish_command(command, error);
        break;
    }
    case CMD_SET_PS:
        finish_command(command, esp_wifi_set_ps(command->ps));
        break;
    case CMD_DRIVER_EVENT:
        handle_driver_event(command);
        break;
    case CMD_FENCE:
        if (s_runtime.physical_state != PHYSICAL_STOPPING ||
            !contexts_equal(command->context, s_runtime.active)) {
            ESP_LOGE(TAG, "Mismatched physical fence; physical slot retained");
            s_runtime.physical_state = PHYSICAL_FAULT;
        } else {
            s_runtime.physical_state = PHYSICAL_QUARANTINE;
            s_runtime.quarantine_deadline_ms = now_ms() + QUIET_QUARANTINE_MS;
        }
        break;
    }
}

static void owner_task(void *arg)
{
    (void) arg;
    runtime_command_t command;
    for (;;) {
        if (xQueueReceive(s_runtime.queue, &command, pdMS_TO_TICKS(50)) == pdTRUE)
            handle_command(&command);
        uint64_t now = now_ms();
        if (s_runtime.physical_state == PHYSICAL_ACTIVE && !s_runtime.result_known &&
            (!s_runtime.provisioning || s_runtime.candidate_waiter != NULL) &&
            now >= s_runtime.attempt_deadline_ms) {
            s_runtime.result_known = true;
            s_runtime.result_success = false;
            if (s_runtime.candidate_is_provisioning) {
                s_runtime.active.required_stop_mask = STOP_MASK_AP;
                publish_context(s_runtime.active, PHYSICAL_ACTIVE);
                esp_err_t error = esp_wifi_set_mode(WIFI_MODE_AP);
                runtime_command_t *waiter = s_runtime.candidate_waiter;
                s_runtime.candidate_waiter = NULL;
                finish_command(waiter, error == ESP_OK ? ESP_ERR_TIMEOUT : error);
            } else {
                request_stop();
            }
        }
        if (s_runtime.physical_state == PHYSICAL_QUARANTINE &&
            now >= s_runtime.quarantine_deadline_ms) {
            release_physical();
            start_desired_if_eligible();
        } else if (s_runtime.physical_state == PHYSICAL_FREE) {
            start_desired_if_eligible();
        }
    }
}

static esp_err_t submit(runtime_command_t *command, uint32_t wait_ms)
{
    if (s_runtime.queue == NULL)
        return ESP_ERR_INVALID_STATE;
    command->completion_target = command;
    command->done = xSemaphoreCreateBinary();
    if (command->done == NULL)
        return ESP_ERR_NO_MEM;
    if (xQueueSend(s_runtime.queue, command, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(command->done);
        return ESP_ERR_TIMEOUT;
    }
    if (xSemaphoreTake(command->done, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        /* The owner may still reference synchronous command storage; fail closed by waiting. */
        xSemaphoreTake(command->done, portMAX_DELAY);
        vSemaphoreDelete(command->done);
        return ESP_ERR_TIMEOUT;
    }
    vSemaphoreDelete(command->done);
    return command->result;
}

esp_err_t connectivity_runtime_init(void)
{
    if (s_runtime.queue != NULL)
        return ESP_OK;
    s_runtime.queue = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(runtime_command_t));
    if (s_runtime.queue == NULL)
        return ESP_ERR_NO_MEM;
    connectivity_retry_init(&s_runtime.retry, 1, CONNECTIVITY_CREDENTIALS_ABSENT);
    s_runtime.desired_generation = 1;
    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, driver_event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, driver_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(CONNECTIVITY_FENCE_EVENT, CONNECTIVITY_FENCE,
                                               fence_handler, NULL));
    if (xTaskCreate(owner_task, "connectivity_owner", 6144, NULL, 6, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t connectivity_runtime_connect_async(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0')
        return ESP_ERR_INVALID_ARG;
    runtime_command_t command = {.type = CMD_CONNECT};
    strlcpy(command.ssid, ssid, sizeof(command.ssid));
    strlcpy(command.password, password ? password : "", sizeof(command.password));
    return submit(&command, 2000);
}

esp_err_t connectivity_runtime_connect_wait(const char *ssid, const char *password,
                                            uint32_t timeout_ms)
{
    runtime_command_t command = {.type = CMD_CONNECT_WAIT, .timeout_ms = timeout_ms};
    strlcpy(command.ssid, ssid, sizeof(command.ssid));
    strlcpy(command.password, password ? password : "", sizeof(command.password));
    return submit(&command, timeout_ms + 5000);
}

esp_err_t connectivity_runtime_start_provisioning_ap(const char *ssid)
{
    runtime_command_t command = {.type = CMD_START_AP};
    strlcpy(command.ssid, ssid, sizeof(command.ssid));
    return submit(&command, 5000);
}

esp_err_t connectivity_runtime_test_candidate(const char *ssid, const char *password,
                                              bool use_static_ip, const char *static_ip,
                                              const char *netmask, const char *gateway,
                                              const char *dns, uint32_t timeout_ms)
{
    runtime_command_t command = {
        .type = CMD_TEST_CANDIDATE, .timeout_ms = timeout_ms, .use_static_ip = use_static_ip};
    strlcpy(command.ssid, ssid, sizeof(command.ssid));
    strlcpy(command.password, password ? password : "", sizeof(command.password));
    strlcpy(command.static_ip, static_ip ? static_ip : "", sizeof(command.static_ip));
    strlcpy(command.netmask, netmask ? netmask : "", sizeof(command.netmask));
    strlcpy(command.gateway, gateway ? gateway : "", sizeof(command.gateway));
    strlcpy(command.dns, dns ? dns : "", sizeof(command.dns));
    return submit(&command, timeout_ms + 5000);
}

esp_err_t connectivity_runtime_finish_provisioning(void)
{
    runtime_command_t command = {.type = CMD_FINISH_PROVISIONING};
    return submit(&command, 5000);
}

esp_err_t connectivity_runtime_stop(uint32_t timeout_ms)
{
    if (s_runtime.queue == NULL)
        return ESP_OK;
    runtime_command_t command = {.type = CMD_STOP};
    return submit(&command, timeout_ms);
}

esp_err_t connectivity_runtime_set_power_save(wifi_ps_type_t mode)
{
    runtime_command_t command = {.type = CMD_SET_PS, .ps = mode};
    return submit(&command, 2000);
}

void connectivity_runtime_set_retry_interval(uint64_t interval_ms)
{
    portENTER_CRITICAL(&s_runtime.lock);
    s_runtime.retry_interval_ms = interval_ms;
    portEXIT_CRITICAL(&s_runtime.lock);
}

int connectivity_runtime_scan(wifi_ap_record_t *results, int max_results)
{
    runtime_command_t command = {
        .type = CMD_SCAN, .scan_results = results, .scan_max = max_results};
    return submit(&command, 20000) == ESP_OK ? command.scan_count : 0;
}

bool connectivity_runtime_is_connected(void)
{
    return s_runtime.connected;
}
uint64_t connectivity_runtime_credential_generation(void)
{
    return s_runtime.desired_generation;
}
