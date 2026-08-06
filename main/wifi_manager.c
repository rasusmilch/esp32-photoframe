#include "wifi_manager.h"

#include <string.h>

#include "config.h"
#include "config_manager.h"
#include "connectivity_runtime.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage.h"
#include "utils.h"

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;

esp_err_t wifi_manager_set_performance_mode(bool enable)
{
    // Modem power save adds ~100ms+ of latency to every round trip, which
    // throttles the web UI hard: bulk transfer speed is roughly one TCP send
    // buffer (~5.7KB) per round trip, i.e. ~45KB/s at 130ms RTT. Full RX
    // (WIFI_PS_NONE) costs ~60-70mA extra while the radio is up, so it is only
    // enabled when someone may actually be using the UI — the policy lives in
    // power_manager's sleep_timer_task.
    static bool applied = false;
    static bool current = false;
    if (applied && current == enable) {
        return ESP_OK;
    }

    esp_err_t err = connectivity_runtime_set_power_save(enable ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    if (err == ESP_OK) {
        applied = true;
        current = enable;
        ESP_LOGI(TAG, "WiFi power save %s (%s mode)", enable ? "disabled" : "enabled",
                 enable ? "performance" : "power-save");
    }
    return err;
}

void wifi_manager_set_retry_interval(uint64_t interval_ms)
{
    connectivity_runtime_set_retry_interval(interval_ms);
}

esp_err_t wifi_manager_update_hostname(void)
{
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    // DHCP hostname from the device name (CamelCase, shown in router device
    // lists). The router picks it up at the next DHCP negotiation (reconnect).
    char hostname[64];
    sanitize_dhcp_hostname(config_manager_get_device_name(), hostname, sizeof(hostname));
    esp_err_t err = esp_netif_set_hostname(s_sta_netif, hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set DHCP hostname: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "DHCP hostname set to: %s", hostname);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create both STA and AP network interfaces
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_manager_update_hostname();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(connectivity_runtime_init());

    ESP_LOGI(TAG, "wifi_manager_init finished.");

    return ESP_OK;
}

esp_err_t wifi_manager_apply_ip_config(void)
{
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config_manager_get_ip_mode() == IP_MODE_STATIC) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_str_to_ip4(config_manager_get_static_ip(), &ip_info.ip) != ESP_OK ||
            esp_netif_str_to_ip4(config_manager_get_static_netmask(), &ip_info.netmask) != ESP_OK ||
            esp_netif_str_to_ip4(config_manager_get_static_gateway(), &ip_info.gw) != ESP_OK) {
            // Never brick the connection on a malformed config — fall back to
            // DHCP so the frame stays reachable and the user can fix it.
            ESP_LOGE(TAG, "Invalid static IP config, falling back to DHCP");
            esp_netif_dhcpc_start(s_sta_netif);
            return ESP_ERR_INVALID_ARG;
        }

        esp_netif_dhcpc_stop(s_sta_netif);
        esp_netif_set_ip_info(s_sta_netif, &ip_info);
        ESP_LOGI(TAG, "Static IP applied: %s/%s gw %s", config_manager_get_static_ip(),
                 config_manager_get_static_netmask(), config_manager_get_static_gateway());
    } else {
        // Make sure DHCP runs when switching back from a static config.
        // Returns ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED in the normal case.
        esp_netif_dhcpc_start(s_sta_netif);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_manager_apply_ip_config();
    return connectivity_runtime_connect_wait(ssid, password, 30000);
}

esp_err_t wifi_manager_connect_async(const char *ssid, const char *password)
{
    wifi_manager_apply_ip_config();
    return connectivity_runtime_connect_async(ssid, password);
}

esp_err_t wifi_manager_disconnect(void)
{
    return connectivity_runtime_stop(10000);
}

bool wifi_manager_is_connected(void)
{
    return connectivity_runtime_is_connected();
}

esp_err_t wifi_manager_get_ip(char *ip_str, size_t len)
{
    if (!ip_str || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_WIFI_PASS_KEY, password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return err;
}

esp_err_t wifi_manager_load_credentials(char *ssid, char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_len = WIFI_SSID_MAX_LEN;
    err = nvs_get_str(nvs_handle, NVS_WIFI_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    size_t pass_len = WIFI_PASS_MAX_LEN;
    err = nvs_get_str(nvs_handle, NVS_WIFI_PASS_KEY, password, &pass_len);
    nvs_close(nvs_handle);

    return err;
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}

int wifi_manager_scan(wifi_ap_record_t *results, int max_results)
{
    return connectivity_runtime_scan(results, max_results);
}

esp_err_t wifi_manager_start_provisioning_ap(const char *ssid)
{
    return connectivity_runtime_start_provisioning_ap(ssid);
}

esp_err_t wifi_manager_test_provisioning_candidate(const char *ssid, const char *password,
                                                   bool use_static_ip, const char *static_ip,
                                                   const char *netmask, const char *gateway,
                                                   const char *dns, uint32_t timeout_ms)
{
    return connectivity_runtime_test_candidate(ssid, password, use_static_ip, static_ip, netmask,
                                               gateway, dns, timeout_ms);
}

esp_err_t wifi_manager_finish_provisioning(void)
{
    return connectivity_runtime_finish_provisioning();
}

esp_err_t wifi_manager_stop(uint32_t timeout_ms)
{
    return connectivity_runtime_stop(timeout_ms);
}
