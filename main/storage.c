#include "storage.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "esp_err.h"
#include "esp_log.h"

#ifdef CONFIG_HAS_SDCARD
#include "sdcard.h"
#endif

#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
#include "esp_littlefs.h"
#endif

#include <sys/stat.h>

#include "album_manager.h"
#include "memfs.h"
#include "utils.h"

static const char *TAG = "storage";
static storage_type_t current_storage_type = STORAGE_TYPE_NONE;

#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
static esp_err_t mount_littlefs(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = FS_MOUNT_POINT,
        .partition_label = LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS Partition size: total: %d, used: %d", total, used);
    }

    return ESP_OK;
}
#endif

esp_err_t storage_init(void)
{
    esp_err_t ret = ESP_OK;

#ifdef CONFIG_HAS_SDCARD
    // For devices with SD card configured, SD card handles its own init in board_hal_init.
    // We just check if it was successfully mounted.
    if (sdcard_is_mounted()) {
        ESP_LOGI(TAG, "SD Card storage is active");
        current_storage_type = STORAGE_TYPE_SDCARD;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SD Card not mounted. Attempting fallback...");
#endif

#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
    // Fallback or explicit flash storage
    if (mount_littlefs() == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS storage is active");
        current_storage_type = STORAGE_TYPE_LITTLEFS;
        return ESP_OK;
    }
#endif

    // Final fallback to MemFS
    ESP_LOGW(TAG, "No persistent storage available, mounting MemFS at %s", FS_MOUNT_POINT);
    ret = memfs_mount(FS_MOUNT_POINT, 10);
    if (ret == ESP_OK) {
        current_storage_type = STORAGE_TYPE_MEMFS;
    } else {
        ESP_LOGE(TAG, "Failed to mount MemFS fallback!");
    }

    return ret;
}

storage_type_t storage_get_type(void)
{
    return current_storage_type;
}

bool storage_has_persistent_storage(void)
{
    return current_storage_type == STORAGE_TYPE_SDCARD ||
           current_storage_type == STORAGE_TYPE_LITTLEFS;
}

void storage_unmount(void)
{
#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
    if (current_storage_type == STORAGE_TYPE_LITTLEFS) {
        ESP_LOGI(TAG, "Unmounting LittleFS before deep sleep");
        esp_vfs_littlefs_unregister(LITTLEFS_PARTITION_LABEL);
    }
#endif
}

esp_err_t storage_format(void)
{
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;

    switch (current_storage_type) {
#ifdef CONFIG_USE_INTERNAL_FLASH_STORAGE
    case STORAGE_TYPE_LITTLEFS:
        ESP_LOGW(TAG, "Formatting LittleFS partition...");
        // Unmount, format, and remount
        esp_vfs_littlefs_unregister(LITTLEFS_PARTITION_LABEL);
        ret = esp_littlefs_format(LITTLEFS_PARTITION_LABEL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to format LittleFS: %s", esp_err_to_name(ret));
            // Try to remount anyway
            mount_littlefs();
            return ret;
        }
        ESP_LOGI(TAG, "LittleFS formatted successfully, remounting...");
        ret = mount_littlefs();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to remount LittleFS after format");
            return ret;
        }
        break;
#endif
#ifdef CONFIG_HAS_SDCARD
    case STORAGE_TYPE_SDCARD:
        // SD card stays mounted across format; only the filesystem is
        // reinitialised as fresh FAT32.
        ret = sdcard_format();
        if (ret != ESP_OK) {
            return ret;
        }
        break;
#endif
    default:
        ESP_LOGE(TAG, "Format not supported for current storage type (%d)", current_storage_type);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Recreate the images directory and default album regardless of backend
    mkdir(IMAGE_DIRECTORY, 0775);
    album_manager_ensure_default_album();

    ESP_LOGI(TAG, "Storage format complete");
    return ESP_OK;
}

wifi_import_source_result_t storage_read_wifi_import_source(void *context, const char *path,
                                                            char *destination, size_t capacity,
                                                            size_t *length)
{
    (void) context;
#ifdef CONFIG_HAS_SDCARD
    if (!sdcard_is_mounted()) {
        return WIFI_IMPORT_SOURCE_NOT_FOUND;
    }
    if (path == NULL || destination == NULL || length == NULL || capacity == 0U) {
        return WIFI_IMPORT_SOURCE_READ_ERROR;
    }
    struct stat info;
    if (stat(path, &info) != 0) {
        return errno == ENOENT ? WIFI_IMPORT_SOURCE_NOT_FOUND : WIFI_IMPORT_SOURCE_OPEN_ERROR;
    }
    if (info.st_size < 0 || (uint64_t) info.st_size > WIFI_IMPORT_MAX_FILE_LEN ||
        (uint64_t) info.st_size >= capacity) {
        return WIFI_IMPORT_SOURCE_TOO_LARGE;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return WIFI_IMPORT_SOURCE_OPEN_ERROR;
    }
    size_t expected = (size_t) info.st_size;
    size_t received = fread(destination, 1U, expected, f);
    int trailing = received == expected ? fgetc(f) : EOF;
    bool grew = trailing != EOF;
    bool read_failed = received != expected || ferror(f) != 0;
    bool close_failed = fclose(f) != 0;
    bool failed = read_failed || close_failed;
    if (failed) {
        return WIFI_IMPORT_SOURCE_READ_ERROR;
    }
    if (grew) {
        return WIFI_IMPORT_SOURCE_TOO_LARGE;
    }
    destination[received] = '\0';
    *length = received;
    return WIFI_IMPORT_SOURCE_OK;
#else
    (void) path;
    (void) destination;
    (void) capacity;
    (void) length;
    return WIFI_IMPORT_SOURCE_NOT_SUPPORTED;
#endif
}
