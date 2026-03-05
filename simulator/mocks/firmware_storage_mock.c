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

// Define missing ESP error codes
#ifndef ESP_ERR_INVALID_VERSION
#define ESP_ERR_INVALID_VERSION 0x110
#endif

#ifndef ESP_ERR_NO_SPACE
#define ESP_ERR_NO_SPACE 0x111
#endif

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

esp_err_t firmware_storage_add_entry(const char* name,
                                     uint32_t offset,
                                     uint32_t size,
                                     uint32_t crc32)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Adding firmware entry to storage: %s @ offset 0x%X", name, offset);

    // Read current header
    firmware_storage_header_t header;
    esp_err_t ret = flash_emulator_read(FIRMWARE_STORAGE_OFFSET, &header, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware storage header");
        return ret;
    }

    // Check if storage is initialized
    if (memcmp(header.magic, "FWST", 4) != 0) {
        ESP_LOGI(TAG, "Initializing new firmware storage (erasing region)");
        memset(&header, 0, sizeof(header));
        memcpy(header.magic, "FWST", 4);
        header.version = 1;
        header.count = 0;
        header.header_size = sizeof(firmware_storage_header_t);
        // Note: simulator doesn't need erase, but we log it for consistency
        ESP_LOGI(TAG, "Simulator: Skipping erase (not needed for RAM emulation)");
    } else if (header.version != 1) {
        ESP_LOGE(TAG, "Firmware storage version mismatch: %u", header.version);
        return ESP_ERR_NOT_SUPPORTED;
    } else if (header.count >= 100) {
        ESP_LOGE(TAG, "Firmware storage full (%u entries)", header.count);
        return ESP_ERR_NO_MEM;
    }

    // Calculate where to write the new entry
    size_t entry_offset = FIRMWARE_STORAGE_OFFSET + header.header_size +
                         (header.count * sizeof(firmware_storage_entry_t));

    // Create new entry
    firmware_storage_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.offset = offset;
    entry.size = size;
    entry.crc32 = crc32;
    entry.flags = 0;
    entry.next_offset = 0;

    // Copy name with truncation if needed
    size_t name_len = strlen(name);
    if (name_len >= sizeof(entry.name)) {
        name_len = sizeof(entry.name) - 1;
        ESP_LOGW(TAG, "Firmware name truncated to %zu bytes", name_len);
    }
    memcpy(entry.name, name, name_len);
    entry.name[name_len] = '\0';

    // Write entry to flash emulator
    ESP_LOGI(TAG, "Writing entry %u at offset 0x%X: %s (%u bytes, CRC32: 0x%08X)",
             header.count, (unsigned int)entry_offset, entry.name, entry.size, entry.crc32);

    ret = flash_emulator_write(entry_offset, &entry, sizeof(entry));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write firmware entry: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update header
    header.count++;

    // Write updated header back to flash emulator
    ret = flash_emulator_write(FIRMWARE_STORAGE_OFFSET, &header, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update firmware storage header: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✓ Firmware entry added: %s (total: %u entries)", entry.name, header.count);
    return ESP_OK;
}

#endif // __SIMULATOR_BUILD__
