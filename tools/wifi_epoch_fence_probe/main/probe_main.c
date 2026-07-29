#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

ESP_EVENT_DEFINE_BASE(PROBE_FENCE_EVENT);
enum { PROBE_FENCE_DISPATCHED = 1 };

typedef enum { OWNER_NONE, OWNER_COORDINATOR, OWNER_PORTAL } owner_t;
typedef enum { STATE_IDLE, STATE_ACTIVE, STATE_STOPPING, STATE_FENCED } adapter_state_t;

typedef struct {
    uint64_t epoch;
    uint64_t attempt_id;
    uint64_t generation;
    owner_t owner;
    adapter_state_t state;
    wifi_mode_t mode;
} physical_context_t;

typedef struct {
    esp_event_base_t base;
    int32_t id;
    physical_context_t context;
    uint8_t reason;
    bool fence;
    bool fence_post_result;
    bool start_next;
    esp_err_t post_error;
} probe_event_t;

static QueueHandle_t event_queue;
static physical_context_t active;
static uint64_t trace_sequence;
static uint64_t next_epoch = 1;
static uint64_t next_attempt = 1;

static const char *owner_name(owner_t owner)
{
    return owner == OWNER_COORDINATOR ? "coordinator" : owner == OWNER_PORTAL ? "portal" : "none";
}

static void trace(const char *source, const char *base, int32_t id, const char *event,
                  physical_context_t context, uint8_t reason, const char *action,
                  const char *result)
{
    printf("EPOCH_TRACE {\"run\":1,\"seq\":%" PRIu64 ",\"ts_ms\":%" PRIi64
           ",\"source\":\"%s\",\"base\":\"%s\",\"event_id\":%" PRIi32
           ",\"event\":\"%s\",\"epoch\":%" PRIu64 ",\"attempt_id\":%" PRIu64
           ",\"generation\":%" PRIu64 ",\"owner\":\"%s\",\"state\":%d,"
           "\"mode\":%d,\"reason\":%u,\"action\":\"%s\",\"result\":\"%s\"}\n",
           ++trace_sequence, esp_timer_get_time() / 1000, source, base, id, event, context.epoch,
           context.attempt_id, context.generation, owner_name(context.owner), context.state,
           context.mode, reason, action, result);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void) arg;
    probe_event_t item = {.base = base, .id = id, .context = active};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && event_data != NULL) {
        item.reason = ((wifi_event_sta_disconnected_t *) event_data)->reason;
    }
    if (xQueueSend(event_queue, &item, 0) != pdTRUE) {
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        esp_err_t result = esp_event_post(PROBE_FENCE_EVENT, PROBE_FENCE_DISPATCHED, &active,
                                          sizeof(active), 0);
        probe_event_t post = {.base = PROBE_FENCE_EVENT,
                              .id = PROBE_FENCE_DISPATCHED,
                              .context = active,
                              .fence_post_result = true,
                              .post_error = result};
        xQueueSend(event_queue, &post, 0);
    }
}

static void fence_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void) arg;
    (void) base;
    probe_event_t item = {.base = PROBE_FENCE_EVENT, .id = id, .fence = true};
    if (event_data != NULL) {
        item.context = *(physical_context_t *) event_data;
    }
    xQueueSend(event_queue, &item, 0);
}

static esp_err_t start_epoch(owner_t owner, uint64_t generation, wifi_mode_t mode)
{
    if (active.epoch != 0) {
        trace("owner", "PROBE", 0, "overlap", active, 0, "epoch_start", "blocked");
        return ESP_ERR_INVALID_STATE;
    }
    active = (physical_context_t) {.epoch = next_epoch++,
                                   .attempt_id = next_attempt++,
                                   .generation = generation,
                                   .owner = owner,
                                   .state = STATE_ACTIVE,
                                   .mode = mode};
    trace("owner", "PROBE", 0, "epoch_start", active, 0, "epoch_start", "ok");
    if (mode == WIFI_MODE_AP) {
        wifi_config_t ap = {.ap = {.channel = 1, .max_connection = 1, .authmode = WIFI_AUTH_OPEN}};
        strlcpy((char *) ap.ap.ssid, "EpochFenceProbe", sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen((char *) ap.ap.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
        return esp_wifi_start();
    }
    wifi_config_t config = {0};
    strlcpy((char *) config.sta.ssid, CONFIG_PROBE_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *) config.sta.password, CONFIG_PROBE_WIFI_PASSWORD, sizeof(config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    return esp_wifi_connect();
}

static void request_stop(const char *cause)
{
    active.state = STATE_STOPPING;
    esp_err_t result = esp_wifi_stop();
    trace("owner", "PROBE", 0, cause, active, 0, "stop_requested",
          result == ESP_OK ? "ok" : "failed");
}

static void owner_task(void *arg)
{
    (void) arg;
    owner_t owner = (CONFIG_PROBE_SCENARIO == 4 || CONFIG_PROBE_SCENARIO == 5) ? OWNER_PORTAL
                                                                              : OWNER_COORDINATOR;
    wifi_mode_t mode = owner == OWNER_PORTAL ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    start_epoch(owner, 1, mode);
    if (CONFIG_PROBE_SCENARIO == 2 || CONFIG_PROBE_SCENARIO == 3 ||
        CONFIG_PROBE_SCENARIO == 7) {
        trace("owner", "PROBE", 0, "replacement", active, 0, "desired_generation_update", "ok");
    }

    probe_event_t event;
    for (;;) {
        if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(15000)) != pdTRUE) {
            trace("owner", "PROBE", 0, "timeout", active, 0, "timeout", "observed");
            request_stop("timeout");
            continue;
        }
        const char *base = event.fence ? "PROBE_FENCE_EVENT"
                                       : event.base == WIFI_EVENT ? "WIFI_EVENT" : "IP_EVENT";
        if (event.fence_post_result) {
            trace("owner", "PROBE_FENCE_EVENT", event.id, "fence_post_result", event.context, 0,
                  "post_fence", event.post_error == ESP_OK ? "ok" : "failed");
            continue;
        }
        if (event.start_next) {
            uint64_t next_generation = CONFIG_PROBE_SCENARIO == 3 ? 3 : 2;
            if (CONFIG_PROBE_SCENARIO == 4) {
                start_epoch(OWNER_PORTAL, next_generation, WIFI_MODE_AP);
            } else if (CONFIG_PROBE_SCENARIO != 5) {
                start_epoch(event.context.owner, next_generation, event.context.mode);
            }
            continue;
        }
        trace("owner", base, event.id, event.fence ? "fence_observed" : "driver_event",
              event.context, event.reason, event.fence ? "fence_observed" : "event_observed", "ok");
        if (event.fence && event.context.epoch == active.epoch) {
            active.state = STATE_FENCED;
            trace("owner", "PROBE_FENCE_EVENT", event.id, "epoch_release", active, 0,
                  "epoch_release", "ok");
            probe_event_t next = {.context = active, .start_next = true};
            active = (physical_context_t) {0};
            xQueueSend(event_queue, &next, 0);
        } else if (!event.fence && event.base == IP_EVENT && event.id == IP_EVENT_STA_GOT_IP &&
                   CONFIG_PROBE_SCENARIO == 5) {
            trace("owner", "PROBE", 0, "portal_persist_marker", active, 0, "persist_simulated",
                  "ok");
            trace("owner", "PROBE", 0, "http_response_complete", active, 0,
                  "response_complete", "ok");
            esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
            trace("owner", "PROBE", 0, "apsta_to_sta", active, 0, "set_mode_sta",
                  result == ESP_OK ? "ok" : "failed");
        } else if (!event.fence && event.base == WIFI_EVENT &&
                   event.id == WIFI_EVENT_STA_DISCONNECTED && active.state == STATE_ACTIVE) {
            if (CONFIG_PROBE_SCENARIO == 4) {
                trace("owner", "PROBE", 0, "http_response_complete", active, 0,
                      "response_complete", "ok");
            }
            request_stop("attempt_failed");
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    event_queue = xQueueCreate(32, sizeof(probe_event_t));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROBE_FENCE_EVENT, ESP_EVENT_ANY_ID, fence_handler,
                                               NULL));
    xTaskCreate(owner_task, "probe_owner", 8192, NULL, 5, NULL);
}
