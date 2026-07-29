#include "wifi_import_runtime.h"

#include <string.h>
#include <unistd.h>

#include "config.h"
#include "config_manager.h"
#include "esp_log.h"
#include "nvs.h"
#include "storage.h"

static const char *TAG = "wifi_import";

_Static_assert(WIFI_IMPORT_SSID_CAPACITY == WIFI_SSID_MAX_LEN, "SSID capacity mismatch");
_Static_assert(WIFI_IMPORT_PASSWORD_CAPACITY == WIFI_PASS_MAX_LEN, "password capacity mismatch");
_Static_assert(WIFI_IMPORT_DEVICE_NAME_CAPACITY == DEVICE_NAME_MAX_LEN,
               "device-name capacity mismatch");

static wifi_import_profile_result_t load_profile(void *context, wifi_import_profile_t *profile)
{
    (void) context;
    memset(profile, 0, sizeof(*profile));
    nvs_handle_t handle;
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(profile->device_name, config_manager_get_device_name(),
                sizeof(profile->device_name) - 1U);
        return WIFI_IMPORT_PROFILE_NONE;
    }
    if (error != ESP_OK) {
        return WIFI_IMPORT_PROFILE_ERROR;
    }

    size_t ssid_length = sizeof(profile->ssid);
    size_t password_length = sizeof(profile->password);
    size_t name_length = sizeof(profile->device_name);
    esp_err_t ssid_error = nvs_get_str(handle, NVS_WIFI_SSID_KEY, profile->ssid, &ssid_length);
    esp_err_t password_error =
        nvs_get_str(handle, NVS_WIFI_PASS_KEY, profile->password, &password_length);
    esp_err_t name_error =
        nvs_get_str(handle, NVS_DEVICE_NAME_KEY, profile->device_name, &name_length);
    nvs_close(handle);

    if (name_error == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(profile->device_name, config_manager_get_device_name(),
                sizeof(profile->device_name) - 1U);
    } else if (name_error != ESP_OK) {
        return WIFI_IMPORT_PROFILE_ERROR;
    }
    bool ssid_absent = ssid_error == ESP_ERR_NVS_NOT_FOUND;
    bool password_absent = password_error == ESP_ERR_NVS_NOT_FOUND;
    if (ssid_absent && password_absent) {
        return WIFI_IMPORT_PROFILE_NONE;
    }
    if ((ssid_absent && password_error == ESP_OK) || (password_absent && ssid_error == ESP_OK)) {
        return WIFI_IMPORT_PROFILE_INCOMPLETE;
    }
    return ssid_error == ESP_OK && password_error == ESP_OK ? WIFI_IMPORT_PROFILE_OK
                                                            : WIFI_IMPORT_PROFILE_ERROR;
}

static bool commit_profile(void *context, const wifi_import_profile_t *profile)
{
    (void) context;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t error = nvs_set_str(handle, NVS_WIFI_SSID_KEY, profile->ssid);
    if (error == ESP_OK) {
        error = nvs_set_str(handle, NVS_WIFI_PASS_KEY, profile->password);
    }
    if (error == ESP_OK) {
        error = nvs_set_str(handle, NVS_DEVICE_NAME_KEY, profile->device_name);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK;
}

static bool delete_source(void *context, const char *path)
{
    (void) context;
    return unlink(path) == 0;
}

static void apply_verified_profile(void *context, const wifi_import_profile_t *profile)
{
    (void) context;
    config_manager_apply_imported_profile(profile->ssid, profile->password, profile->device_name);
}

wifi_import_outcome_t wifi_import_process(void)
{
    wifi_import_ports_t ports = {.context = NULL,
                                 .read_source = storage_read_wifi_import_source,
                                 .load_profile = load_profile,
                                 .commit_profile = commit_profile,
                                 .delete_source = delete_source,
                                 .apply_verified_profile = apply_verified_profile};
    wifi_import_outcome_t outcome = wifi_import_run(&ports);
    ESP_LOGI(TAG, "wifi.txt import outcome: %d", outcome);
    return outcome;
}
