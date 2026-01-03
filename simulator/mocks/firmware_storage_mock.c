/**
 * @file firmware_storage_mock.c
 * @brief Mock implementation of firmware storage for simulator
 *
 * In the simulator, we use the flash emulator instead of esp_flash API.
 * This mock reads from the flash emulator's memory-mapped file.
 */

#ifdef __SIMULATOR_BUILD__

#include "firmware_storage.h"
#include "esp_log_mock.h"
#include "flash_emulator.h"
#include <string.h>

static const char* TAG = "firmware_storage_mock";

esp_err_t firmware_storage_check_valid(bool* valid)
{
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }

    *valid = false;

    // Read header from flash emulator
    firmware_storage_header_t header;
    esp_err_t ret = flash_emulator_read(FIRMWARE_STORAGE_OFFSET, &header, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware storage header: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check magic number
    if (memcmp(header.magic, "FWST", 4) != 0) {
        ESP_LOGD(TAG, "No firmware storage found (magic mismatch)");
        return ESP_OK;
    }

    // Check version
    if (header.version != 1) {
        ESP_LOGW(TAG, "Firmware storage version mismatch: %u (expected 1)", header.version);
        return ESP_OK;
    }

    // Sanity check count
    if (header.count > 100) {
        ESP_LOGW(TAG, "Invalid firmware count: %u", header.count);
        return ESP_OK;
    }

    *valid = true;
    ESP_LOGI(TAG, "Firmware storage valid: %u firmwares", header.count);

    return ESP_OK;
}

esp_err_t firmware_storage_get_count(uint32_t* count)
{
    if (!count) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read header from flash emulator
    firmware_storage_header_t header;
    esp_err_t ret = flash_emulator_read(FIRMWARE_STORAGE_OFFSET, &header, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware storage header: %s", esp_err_to_name(ret));
        return ret;
    }

    // Validate magic
    if (memcmp(header.magic, "FWST", 4) != 0) {
        ESP_LOGE(TAG, "Firmware storage not found");
        *count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    *count = header.count;
    return ESP_OK;
}

esp_err_t firmware_storage_get_entry(uint32_t index, firmware_storage_entry_t* entry)
{
    if (!entry) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read header to get count
    firmware_storage_header_t header;
    esp_err_t ret = flash_emulator_read(FIRMWARE_STORAGE_OFFSET, &header, sizeof(header));
    if (ret != ESP_OK) {
        return ret;
    }

    // Validate magic
    if (memcmp(header.magic, "FWST", 4) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    // Check index bounds
    if (index >= header.count) {
        return ESP_ERR_NOT_FOUND;
    }

    // Calculate entry offset
    size_t header_size = sizeof(firmware_storage_header_t);
    size_t entry_offset = FIRMWARE_STORAGE_OFFSET + header_size + (index * sizeof(firmware_storage_entry_t));

    // Read entry
    ret = flash_emulator_read(entry_offset, entry, sizeof(firmware_storage_entry_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware entry %u: %s", index, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t firmware_storage_read_firmware(const firmware_storage_entry_t* entry,
                                         uint8_t* buffer,
                                         size_t buffer_size)
{
    if (!entry || !buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    if (buffer_size < entry->size) {
        ESP_LOGE(TAG, "Buffer too small: %zu < %u", buffer_size, entry->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Calculate firmware data offset
    size_t header_size = sizeof(firmware_storage_header_t);
    size_t entries_size = entry->offset;
    uint32_t firmware_offset = FIRMWARE_STORAGE_OFFSET + header_size + entries_size;

    ESP_LOGI(TAG, "Reading firmware from flash emulator: 0x%X (%u bytes)", firmware_offset, entry->size);

    // Read firmware data from flash emulator
    esp_err_t ret = flash_emulator_read(firmware_offset, buffer, entry->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware data: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ Read %u bytes from firmware storage", entry->size);
    return ESP_OK;
}

#endif // __SIMULATOR_BUILD__
