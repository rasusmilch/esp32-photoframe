#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
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
static portMUX_TYPE active_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t fault_bits;
static uint64_t trace_sequence;
static uint64_t next_epoch = 1;
static uint64_t next_attempt = 1;
static unsigned epochs_started;
static bool run_finished;

enum {
    FAULT_EVENT_QUEUE = 1U << 0,
    FAULT_FENCE_POST_RECORD = 1U << 1,
    FAULT_FENCE_QUEUE = 1U << 2,
    FAULT_OWNER_COMMAND = 1U << 3,
    FAULT_CONTEXT = 1U << 4,
    FAULT_DRIVER = 1U << 5,
    FAULT_FENCE_TIMEOUT = 1U << 6,
};

static physical_context_t active_snapshot(void)
{
    physical_context_t snapshot;
    portENTER_CRITICAL(&active_mux);
    snapshot = active;
    portEXIT_CRITICAL(&active_mux);
    return snapshot;
}

static void active_store(physical_context_t value)
{
    portENTER_CRITICAL(&active_mux);
    active = value;
    portEXIT_CRITICAL(&active_mux);
}

static void latch_fault(uint32_t fault)
{
    portENTER_CRITICAL(&active_mux);
    fault_bits |= fault;
    portEXIT_CRITICAL(&active_mux);
}

static bool contexts_equal(physical_context_t left, physical_context_t right)
{
    return left.epoch == right.epoch && left.attempt_id == right.attempt_id &&
           left.generation == right.generation && left.owner == right.owner;
}

static const char *owner_name(owner_t owner)
{
    return owner == OWNER_COORDINATOR ? "coordinator" : owner == OWNER_PORTAL ? "portal" : "none";
}

static const char *event_name(esp_event_base_t base, int32_t id)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            return "sta_start";
        case WIFI_EVENT_STA_STOP:
            return "sta_stop";
        case WIFI_EVENT_STA_CONNECTED:
            return "sta_connected";
        case WIFI_EVENT_STA_DISCONNECTED:
            return "sta_disconnected";
        default:
            return "wifi_other";
        }
    }
    if (base == IP_EVENT) {
        switch (id) {
        case IP_EVENT_STA_GOT_IP:
            return "got_ip";
        case IP_EVENT_STA_LOST_IP:
            return "lost_ip";
        default:
            return "ip_other";
        }
    }
    return "fence_dispatched";
}

static void trace(const char *source, const char *base, int32_t id, const char *event,
                  physical_context_t context, uint8_t reason, const char *action,
                  const char *result)
{
    printf("EPOCH_TRACE {\"run\":1,\"seq\":%" PRIu64 ",\"ts_ms\":%" PRIi64
           ",\"source\":\"%s\",\"base\":\"%s\",\"event_id\":%" PRIi32
           ",\"event\":\"%s\",\"epoch\":%" PRIu64 ",\"attempt_id\":%" PRIu64
           ",\"generation\":%" PRIu64
           ",\"owner\":\"%s\",\"state\":%d,"
           "\"mode\":%d,\"reason\":%u,\"action\":\"%s\",\"result\":\"%s\"}\n",
           ++trace_sequence, esp_timer_get_time() / 1000, source, base, id, event, context.epoch,
           context.attempt_id, context.generation, owner_name(context.owner), context.state,
           context.mode, reason, action, result);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void) arg;
    physical_context_t snapshot = active_snapshot();
    probe_event_t item = {.base = base, .id = id, .context = snapshot};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && event_data != NULL) {
        item.reason = ((wifi_event_sta_disconnected_t *) event_data)->reason;
    }
    if (xQueueSend(event_queue, &item, 0) != pdTRUE) {
        latch_fault(base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP ? FAULT_FENCE_POST_RECORD
                                                                    : FAULT_EVENT_QUEUE);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        esp_err_t result = esp_event_post(PROBE_FENCE_EVENT, PROBE_FENCE_DISPATCHED, &snapshot,
                                          sizeof(snapshot), 0);
        probe_event_t post = {.base = PROBE_FENCE_EVENT,
                              .id = PROBE_FENCE_DISPATCHED,
                              .context = snapshot,
                              .fence_post_result = true,
                              .post_error = result};
        if (xQueueSend(event_queue, &post, 0) != pdTRUE) {
            latch_fault(FAULT_FENCE_POST_RECORD);
        }
        if (result != ESP_OK) {
            latch_fault(FAULT_DRIVER);
        }
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
    if (xQueueSend(event_queue, &item, 0) != pdTRUE) {
        latch_fault(FAULT_FENCE_QUEUE);
    }
}

static esp_err_t start_epoch(owner_t owner, uint64_t generation, wifi_mode_t mode)
{
    physical_context_t current = active_snapshot();
    if (current.epoch != 0) {
        trace("owner", "PROBE", 0, "overlap", current, 0, "probe_fault", "failed");
        latch_fault(FAULT_CONTEXT);
        return ESP_ERR_INVALID_STATE;
    }
    current = (physical_context_t) {.epoch = next_epoch++,
                                    .attempt_id = next_attempt++,
                                    .generation = generation,
                                    .owner = owner,
                                    .state = STATE_ACTIVE,
                                    .mode = mode};
    active_store(current);
    epochs_started++;
    trace("owner", "PROBE", 0, "epoch_start", current, 0, "epoch_start", "ok");
    if (mode == WIFI_MODE_AP) {
        wifi_config_t ap = {.ap = {.channel = 1, .max_connection = 1, .authmode = WIFI_AUTH_OPEN}};
        strlcpy((char *) ap.ap.ssid, "EpochFenceProbe", sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen((char *) ap.ap.ssid);
        esp_err_t result = esp_wifi_set_mode(mode);
        trace("owner", "PROBE", 0, "ap_mode", current, 0, "driver_call",
              result == ESP_OK ? "ok" : "failed");
        if (result == ESP_OK)
            result = esp_wifi_set_config(WIFI_IF_AP, &ap);
        trace("owner", "PROBE", 0, "ap_config", current, 0, "ap_configured",
              result == ESP_OK ? "ok" : "failed");
        if (result == ESP_OK)
            result = esp_wifi_start();
        if (result != ESP_OK)
            latch_fault(FAULT_DRIVER);
        return result;
    }
    wifi_config_t config = {0};
    strlcpy((char *) config.sta.ssid, CONFIG_PROBE_WIFI_SSID, sizeof(config.sta.ssid));
    strlcpy((char *) config.sta.password, CONFIG_PROBE_WIFI_PASSWORD, sizeof(config.sta.password));
    esp_err_t result = esp_wifi_set_mode(mode);
    trace("owner", "PROBE", 0, "mode", current, 0, "driver_call",
          result == ESP_OK ? "ok" : "failed");
    if (mode == WIFI_MODE_APSTA && result == ESP_OK) {
        wifi_config_t ap = {.ap = {.channel = 1, .max_connection = 1, .authmode = WIFI_AUTH_OPEN}};
        strlcpy((char *) ap.ap.ssid, "EpochFenceProbe", sizeof(ap.ap.ssid));
        ap.ap.ssid_len = strlen((char *) ap.ap.ssid);
        result = esp_wifi_set_config(WIFI_IF_AP, &ap);
        trace("owner", "PROBE", 0, "ap_config", current, 0, "ap_configured",
              result == ESP_OK ? "ok" : "failed");
    }
    if (result == ESP_OK)
        result = esp_wifi_set_config(WIFI_IF_STA, &config);
    trace("owner", "PROBE", 0, "sta_config", current, 0, "sta_configured",
          result == ESP_OK ? "ok" : "failed");
    if (result == ESP_OK)
        result = esp_wifi_start();
    if (result == ESP_OK)
        result = esp_wifi_connect();
    if (result != ESP_OK)
        latch_fault(FAULT_DRIVER);
    return result;
}

static void request_stop(const char *cause)
{
    physical_context_t current = active_snapshot();
    if (current.state != STATE_ACTIVE) {
        trace("owner", "PROBE", 0, cause, current, 0, "probe_fault", "failed");
        latch_fault(FAULT_CONTEXT);
        return;
    }
    current.state = STATE_STOPPING;
    active_store(current);
    esp_err_t result = esp_wifi_stop();
    trace("owner", "PROBE", 0, cause, current, 0, "stop_requested",
          result == ESP_OK ? "ok" : "failed");
    if (result != ESP_OK)
        latch_fault(FAULT_DRIVER);
}

static void owner_task(void *arg)
{
    (void) arg;
    owner_t owner = (CONFIG_PROBE_SCENARIO == 4 || CONFIG_PROBE_SCENARIO == 5) ? OWNER_PORTAL
                                                                               : OWNER_COORDINATOR;
    wifi_mode_t mode = owner == OWNER_PORTAL ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    (void) start_epoch(owner, 1, mode);
    if (CONFIG_PROBE_SCENARIO == 2 || CONFIG_PROBE_SCENARIO == 3 || CONFIG_PROBE_SCENARIO == 7) {
        physical_context_t update = active_snapshot();
        update.generation = 2;
        trace("owner", "PROBE", 0, "replacement", update, 0, "desired_generation_update", "ok");
        if (CONFIG_PROBE_SCENARIO == 3) {
            update.generation = 3;
            trace("owner", "PROBE", 0, "replacement", update, 0, "desired_generation_update", "ok");
        }
    }

    probe_event_t event;
    bool observation_started = false;
    bool fault_reported = false;
    const unsigned target_epochs =
        (CONFIG_PROBE_SCENARIO == 5 || CONFIG_PROBE_SCENARIO == 6) ? 1U : 2U;
    for (;;) {
        uint32_t faults;
        portENTER_CRITICAL(&active_mux);
        faults = fault_bits;
        portEXIT_CRITICAL(&active_mux);
        if (faults != 0 && !fault_reported) {
            physical_context_t snapshot = active_snapshot();
            trace("owner", "PROBE", 0, "probe_fault", snapshot, 0, "probe_fault", "failed");
            fault_reported = true;
        }
        if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(15000)) != pdTRUE) {
            physical_context_t snapshot = active_snapshot();
            if (faults != 0 || run_finished)
                continue;
            if (snapshot.state == STATE_ACTIVE) {
                trace("owner", "PROBE", 0,
                      observation_started ? "observation_timeout" : "attempt_timeout", snapshot, 0,
                      "timeout", "observed");
                request_stop(observation_started ? "observation_timeout" : "attempt_timeout");
            } else if (snapshot.state == STATE_STOPPING) {
                trace("owner", "PROBE", 0, "fence_timeout", snapshot, 0, "probe_fault", "failed");
                latch_fault(FAULT_FENCE_TIMEOUT);
            }
            continue;
        }
        const char *base = event.fence                ? "PROBE_FENCE_EVENT"
                           : event.base == WIFI_EVENT ? "WIFI_EVENT"
                                                      : "IP_EVENT";
        if (event.fence_post_result) {
            trace("owner", "PROBE_FENCE_EVENT", event.id, "fence_post_result", event.context, 0,
                  "post_fence", event.post_error == ESP_OK ? "ok" : "failed");
            continue;
        }
        if (event.start_next) {
            if (faults != 0 || active_snapshot().epoch != 0) {
                latch_fault(FAULT_CONTEXT);
                continue;
            }
            uint64_t next_generation = CONFIG_PROBE_SCENARIO == 3 ? 3 : 2;
            if (CONFIG_PROBE_SCENARIO == 4) {
                (void) start_epoch(OWNER_PORTAL, next_generation, WIFI_MODE_APSTA);
            } else if (CONFIG_PROBE_SCENARIO != 5) {
                (void) start_epoch(event.context.owner, next_generation, event.context.mode);
            }
            continue;
        }
        trace("owner", base, event.id,
              event.fence ? "fence_dispatched" : event_name(event.base, event.id), event.context,
              event.reason, event.fence ? "fence_observed" : "event_observed", "ok");
        if (event.fence) {
            physical_context_t current = active_snapshot();
            if (current.state != STATE_STOPPING || !contexts_equal(event.context, current)) {
                trace("owner", "PROBE_FENCE_EVENT", event.id, "fence_mismatch", event.context, 0,
                      "probe_fault", "failed");
                latch_fault(FAULT_CONTEXT);
                continue;
            }
            current.state = STATE_FENCED;
            active_store(current);
            trace("owner", "PROBE_FENCE_EVENT", event.id, "epoch_release", current, 0,
                  "epoch_release", "ok");
            active_store((physical_context_t) {0});
            observation_started = false;
            if (epochs_started >= target_epochs) {
                portENTER_CRITICAL(&active_mux);
                faults = fault_bits;
                portEXIT_CRITICAL(&active_mux);
                if (faults == 0) {
                    trace("owner", "PROBE", 0, "run_complete", (physical_context_t) {0}, 0,
                          "run_complete", "ok");
                    run_finished = true;
                }
            } else {
                probe_event_t next = {.context = current, .start_next = true};
                if (xQueueSend(event_queue, &next, 0) != pdTRUE)
                    latch_fault(FAULT_OWNER_COMMAND);
            }
        } else if (!event.fence && event.base == IP_EVENT && event.id == IP_EVENT_STA_GOT_IP &&
                   CONFIG_PROBE_SCENARIO == 5) {
            physical_context_t current = active_snapshot();
            trace("owner", "PROBE", 0, "portal_persist_marker", current, 0, "persist_simulated",
                  "ok");
            trace("owner", "PROBE", 0, "http_response_complete", current, 0, "response_complete",
                  "ok");
            esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
            trace("owner", "PROBE", 0, "apsta_to_sta", current, 0, "set_mode_sta",
                  result == ESP_OK ? "ok" : "failed");
            if (result != ESP_OK)
                latch_fault(FAULT_DRIVER);
            observation_started = result == ESP_OK;
        } else if (!event.fence && event.base == WIFI_EVENT &&
                   event.id == WIFI_EVENT_STA_DISCONNECTED &&
                   active_snapshot().state == STATE_ACTIVE) {
            physical_context_t current = active_snapshot();
            if (CONFIG_PROBE_SCENARIO == 4) {
                trace("owner", "PROBE", 0, "http_response_complete", current, 0,
                      "response_complete", "ok");
            }
            request_stop("attempt_failed");
        } else if (!event.fence && event.base == IP_EVENT && event.id == IP_EVENT_STA_GOT_IP &&
                   CONFIG_PROBE_SCENARIO != 5 && active_snapshot().state == STATE_ACTIVE) {
            request_stop("controlled_success");
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
    ESP_ERROR_CHECK(
        esp_event_handler_register(PROBE_FENCE_EVENT, ESP_EVENT_ANY_ID, fence_handler, NULL));
    xTaskCreate(owner_task, "probe_owner", 8192, NULL, 5, NULL);
}
