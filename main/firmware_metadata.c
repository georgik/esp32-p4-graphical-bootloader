/**
 * @file firmware_metadata.c
 * @brief Firmware metadata persistence implementation using NVS
 */

#include "firmware_metadata.h"
#include "firmware_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <time.h>
#include <inttypes.h>

#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    #if defined(__SIMULATOR_BUILD__)
        #include "nvs_mock.h"
    #else
        #include "nvs.h"
    #endif
#endif

static const char* TAG = "firmware_metadata";
#define NVS_NAMESPACE "firmware_config"
#define KEY_FIRMWARE_COUNT "firmware_count"
#define KEY_FW_FILENAME "fw_%" PRIu32 "_filename"
#define KEY_FW_PARTITION "fw_%" PRIu32 "_partition"
#define KEY_FW_OFFSET "fw_%" PRIu32 "_offset"
#define KEY_FW_SIZE "fw_%" PRIu32 "_size"
#define KEY_FW_CRC32 "fw_%" PRIu32 "_crc32"
#define KEY_FW_VALID "fw_%" PRIu32 "_valid"
#define KEY_FW_TIMESTAMP "fw_%" PRIu32 "_timestamp"

esp_err_t firmware_metadata_init(void) {
#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "Erasing NVS flash...");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Firmware metadata initialized");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "NVS not available on this platform");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t firmware_metadata_deinit(void) {
#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    return nvs_flash_deinit();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t firmware_metadata_get_count(uint32_t* count) {
    if (!count) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_get_u32(handle, KEY_FIRMWARE_COUNT, count);
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    }

    return ret;
#else
    *count = 0;
    return ESP_OK;
#endif
}

esp_err_t firmware_metadata_set_count(uint32_t count) {
#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u32(handle, KEY_FIRMWARE_COUNT, count);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set firmware count: %s", esp_err_to_name(ret));
    }

    return ret;
#else
    return ESP_OK;
#endif
}

esp_err_t firmware_metadata_get(uint32_t index, firmware_metadata_t* metadata) {
    if (!metadata || index >= MAX_FIRMWARE_ENTRIES) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    char key[64];
    char filename_str[128];
    size_t len;

    // Get filename
    snprintf(key, sizeof(key), KEY_FW_FILENAME, index);
    len = sizeof(filename_str);
    ret = nvs_get_str(handle, key, filename_str, &len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to get filename for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }
    // Safe copy with explicit truncation
    size_t copy_len = len;
    if (copy_len >= sizeof(metadata->filename)) {
        copy_len = sizeof(metadata->filename) - 1;
    }
    memcpy(metadata->filename, filename_str, copy_len);
    metadata->filename[copy_len] = '\0';

    // Get partition
    snprintf(key, sizeof(key), KEY_FW_PARTITION, index);
    len = sizeof(metadata->partition);
    ret = nvs_get_str(handle, key, metadata->partition, &len);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to get partition for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Get offset
    snprintf(key, sizeof(key), KEY_FW_OFFSET, index);
    ret = nvs_get_u32(handle, key, &metadata->offset);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to get offset for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Get size
    snprintf(key, sizeof(key), KEY_FW_SIZE, index);
    ret = nvs_get_u32(handle, key, &metadata->size);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to get size for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Get CRC32
    snprintf(key, sizeof(key), KEY_FW_CRC32, index);
    ret = nvs_get_u32(handle, key, &metadata->crc32);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to get CRC32 for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Get valid flag
    uint8_t valid;
    snprintf(key, sizeof(key), KEY_FW_VALID, index);
    ret = nvs_get_u8(handle, key, &valid);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to get valid flag for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }
    metadata->is_valid = (valid != 0);

    // Get timestamp
    snprintf(key, sizeof(key), KEY_FW_TIMESTAMP, index);
    ret = nvs_get_u32(handle, key, &metadata->timestamp);
    if (ret != ESP_OK) {
        metadata->timestamp = 0;
    }

    nvs_close(handle);
    return ESP_OK;
#else
    memset(metadata, 0, sizeof(firmware_metadata_t));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t firmware_metadata_set(uint32_t index, const firmware_metadata_t* metadata) {
    if (!metadata || index >= MAX_FIRMWARE_ENTRIES) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    char key[64];

    // Set filename
    snprintf(key, sizeof(key), KEY_FW_FILENAME, index);
    ret = nvs_set_str(handle, key, metadata->filename);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set filename for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Set partition
    snprintf(key, sizeof(key), KEY_FW_PARTITION, index);
    ret = nvs_set_str(handle, key, metadata->partition);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set partition for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Set offset
    snprintf(key, sizeof(key), KEY_FW_OFFSET, index);
    ret = nvs_set_u32(handle, key, metadata->offset);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set offset for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Set size
    snprintf(key, sizeof(key), KEY_FW_SIZE, index);
    ret = nvs_set_u32(handle, key, metadata->size);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set size for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Set CRC32
    snprintf(key, sizeof(key), KEY_FW_CRC32, index);
    ret = nvs_set_u32(handle, key, metadata->crc32);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set CRC32 for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Set valid flag
    snprintf(key, sizeof(key), KEY_FW_VALID, index);
    ret = nvs_set_u8(handle, key, metadata->is_valid ? 1 : 0);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set valid flag for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Set timestamp (use current time)
    snprintf(key, sizeof(key), KEY_FW_TIMESTAMP, index);
    uint32_t timestamp = (uint32_t)time(NULL);
    ret = nvs_set_u32(handle, key, timestamp);
    if (ret != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set timestamp for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
        return ret;
    }

    // Commit all changes
    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit metadata for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "✅ Stored firmware metadata [%" PRIu32 "]: %s -> %s @ 0x%08X",
                 index, metadata->filename, metadata->partition, metadata->offset);
    }

    return ret;
#else
    return ESP_OK;
#endif
}

esp_err_t firmware_metadata_delete(uint32_t index) {
    if (index >= MAX_FIRMWARE_ENTRIES) {
        return ESP_ERR_INVALID_ARG;
    }

#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    char key[64];

    // Erase all keys for this index
    snprintf(key, sizeof(key), KEY_FW_FILENAME, index);
    nvs_erase_key(handle, key);

    snprintf(key, sizeof(key), KEY_FW_PARTITION, index);
    nvs_erase_key(handle, key);

    snprintf(key, sizeof(key), KEY_FW_OFFSET, index);
    nvs_erase_key(handle, key);

    snprintf(key, sizeof(key), KEY_FW_SIZE, index);
    nvs_erase_key(handle, key);

    snprintf(key, sizeof(key), KEY_FW_CRC32, index);
    nvs_erase_key(handle, key);

    snprintf(key, sizeof(key), KEY_FW_VALID, index);
    nvs_erase_key(handle, key);

    snprintf(key, sizeof(key), KEY_FW_TIMESTAMP, index);
    nvs_erase_key(handle, key);

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete metadata for index %" PRIu32 ": %s", index, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "🗑️  Deleted firmware metadata [%" PRIu32 "]", index);
    }

    return ret;
#else
    return ESP_OK;
#endif
}

esp_err_t firmware_metadata_clear_all(void) {
#if defined(__SIMULATOR_BUILD__) || defined(CONFIG_IDF_TARGET_ESP32P4)
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear all metadata: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "🧹 Cleared all firmware metadata");
    }

    return ret;
#else
    return ESP_OK;
#endif
}

esp_err_t firmware_metadata_validate(uint32_t index, bool* is_valid) {
    if (!is_valid || index >= MAX_FIRMWARE_ENTRIES) {
        return ESP_ERR_INVALID_ARG;
    }

    firmware_metadata_t metadata;
    esp_err_t ret = firmware_metadata_get(index, &metadata);
    if (ret != ESP_OK) {
        *is_valid = false;
        return ret;
    }

    // For now, just return the stored valid flag
    // In the future, this could read the actual flash and verify CRC32
    *is_valid = metadata.is_valid;

    ESP_LOGI(TAG, "Firmware [%" PRIu32 "] validation: %s", index, *is_valid ? "VALID" : "INVALID");
    return ESP_OK;
}

esp_err_t firmware_metadata_find_by_partition(const char* partition, uint32_t* index) {
    if (!partition || !index) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t count = 0;
    esp_err_t ret = firmware_metadata_get_count(&count);
    if (ret != ESP_OK) {
        return ret;
    }

    for (uint32_t i = 0; i < count; i++) {
        firmware_metadata_t metadata;
        ret = firmware_metadata_get(i, &metadata);
        if (ret != ESP_OK) {
            continue;
        }

        if (strcmp(metadata.partition, partition) == 0) {
            *index = i;
            ESP_LOGI(TAG, "Found firmware in partition '%s' at index %u", partition, i);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "No firmware found in partition '%s'", partition);
    return ESP_ERR_NOT_FOUND;
}

void firmware_metadata_print_all(void) {
    uint32_t count = 0;
    if (firmware_metadata_get_count(&count) != ESP_OK || count == 0) {
        ESP_LOGI(TAG, "No firmware metadata stored");
        return;
    }

    ESP_LOGI(TAG, "=== Firmware Metadata (%u entries) ===", count);

    for (uint32_t i = 0; i < count; i++) {
        firmware_metadata_t metadata;
        if (firmware_metadata_get(i, &metadata) == ESP_OK) {
            ESP_LOGI(TAG, "[%" PRIu32 "] %s -> %s @ 0x%08X, size: %" PRIu32 ", CRC32: 0x%08X, valid: %s",
                     i, metadata.filename, metadata.partition, metadata.offset,
                     metadata.size, metadata.crc32,
                     metadata.is_valid ? "✅" : "❌");
        }
    }

    ESP_LOGI(TAG, "======================================");
}

esp_err_t firmware_metadata_scan_and_store(void) {
    ESP_LOGI(TAG, "Scanning firmware storage to populate NVS...");

    // Check if firmware storage exists
    bool storage_valid = false;
    esp_err_t ret = firmware_storage_check_valid(&storage_valid);
    if (ret != ESP_OK || !storage_valid) {
        ESP_LOGW(TAG, "No valid firmware storage found");
        return ESP_ERR_NOT_FOUND;
    }

    // Get firmware count
    uint32_t firmware_count = 0;
    ret = firmware_storage_get_count(&firmware_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get firmware count from storage");
        return ret;
    }

    ESP_LOGI(TAG, "Found %u firmwares in storage, populating NVS...", firmware_count);

    // Clear any existing firmware metadata
    firmware_metadata_clear_all();

    // Read each firmware entry and store in NVS
    for (uint32_t i = 0; i < firmware_count && i < MAX_FIRMWARE_ENTRIES; i++) {
        firmware_storage_entry_t entry;
        ret = firmware_storage_get_entry(i, &entry);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get firmware entry %u", i);
            continue;
        }

        // Create metadata entry
        firmware_metadata_t metadata;
        memset(&metadata, 0, sizeof(metadata));

        // Copy filename from name field
        strncpy(metadata.filename, entry.name, sizeof(metadata.filename) - 1);
        metadata.filename[sizeof(metadata.filename) - 1] = '\0';

        // Assign to OTA partitions (ota_0, ota_1, ota_2, etc.)
        snprintf(metadata.partition, sizeof(metadata.partition), "ota_%" PRIu32, i);

        // Copy other fields
        metadata.offset = entry.offset;
        metadata.size = entry.size;
        metadata.crc32 = entry.crc32;
        metadata.is_valid = true;
        metadata.timestamp = (uint32_t)time(NULL);

        // Store in NVS
        ret = firmware_metadata_set(i, &metadata);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store firmware metadata %" PRIu32 ": %s", i, esp_err_to_name(ret));
            continue;
        }

        ESP_LOGI(TAG, "  [%" PRIu32 "] %s -> %s (%" PRIu32 " bytes, CRC32: 0x%08X)",
                 i, metadata.filename, metadata.partition, metadata.size, metadata.crc32);
    }

    // Set the total count
    ret = firmware_metadata_set_count(firmware_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set firmware count: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ Firmware storage scan complete: %u firmwares stored in NVS", firmware_count);

    // Print what we stored for debugging
    firmware_metadata_print_all();

    return ESP_OK;
}
