#ifndef CONNECTIVITY_RUNTIME_H
#define CONNECTIVITY_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "connectivity_policy.h"
#include "esp_err.h"
#include "esp_wifi_types.h"

esp_err_t connectivity_runtime_init(void);
esp_err_t connectivity_runtime_connect_async(const char *ssid, const char *password);
esp_err_t connectivity_runtime_connect_wait(const char *ssid, const char *password,
                                            uint32_t timeout_ms);
esp_err_t connectivity_runtime_start_provisioning_ap(const char *ssid);
esp_err_t connectivity_runtime_test_candidate(const char *ssid, const char *password,
                                              bool use_static_ip, const char *static_ip,
                                              const char *netmask, const char *gateway,
                                              const char *dns, uint32_t timeout_ms);
esp_err_t connectivity_runtime_finish_provisioning(void);
esp_err_t connectivity_runtime_stop(uint32_t timeout_ms);
esp_err_t connectivity_runtime_set_power_save(wifi_ps_type_t mode);
void connectivity_runtime_set_retry_interval(uint64_t interval_ms);
int connectivity_runtime_scan(wifi_ap_record_t *results, int max_results);
bool connectivity_runtime_is_connected(void);
uint64_t connectivity_runtime_credential_generation(void);

#endif
