#pragma once

#include <esp_err.h>
#include <stdbool.h>

#include "wifi_import.h"

#ifdef __cplusplus
extern "C" {
#endif

// Partition label used for the LittleFS internal flash storage
#define LITTLEFS_PARTITION_LABEL "storage"

// Storage backend types
typedef enum {
    STORAGE_TYPE_NONE = 0,
    STORAGE_TYPE_SDCARD,
    STORAGE_TYPE_LITTLEFS,
    STORAGE_TYPE_MEMFS
} storage_type_t;

/**
 * @brief Initialize the appropriate storage medium based on kconfig and availability
 *        Handles fallbacks (SD Card -> LittleFS -> MemFS)
 *
 * @return esp_err_t ESP_OK on success, ESP_FAIL or error code on failure
 */
esp_err_t storage_init(void);

/**
 * @brief Get the currently active primary storage type
 *
 * @return storage_type_t The active storage type
 */
storage_type_t storage_get_type(void);

/**
 * @brief Check if any persistent storage (SD or Flash) is available
 *
 * @return true if persistent storage is available, false otherwise
 */
bool storage_has_persistent_storage(void);

/** Read exactly one candidate source into a caller-owned bounded buffer. No side effects. */
wifi_import_source_result_t storage_read_wifi_import_source(void *context, const char *path,
                                                            char *destination, size_t capacity,
                                                            size_t *length);

/**
 * @brief Unmount storage before deep sleep to release flash references
 */
void storage_unmount(void);

/**
 * @brief Format the internal flash storage (LittleFS only)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_format(void);

#ifdef __cplusplus
}
#endif
