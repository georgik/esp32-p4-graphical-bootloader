/**
 * @file firmware_storage.c
 * @brief Firmware storage area implementation
 */

#include "firmware_storage.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "firmware_storage";

esp_err_t firmware_storage_check_valid(bool* valid)
{
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }

    *valid = false;

    // Read header from flash
    firmware_storage_header_t header;
    esp_err_t ret = esp_flash_read(NULL, &header, FIRMWARE_STORAGE_OFFSET, sizeof(header));
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
    if (header.count > 100) {  // Arbitrary sanity limit
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

    // Read header from flash
    firmware_storage_header_t header;
    esp_err_t ret = esp_flash_read(NULL, &header, FIRMWARE_STORAGE_OFFSET, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware storage header: %s", esp_err_to_name(ret));
        return ret;
    }

    // Debug: Log what we read
    ESP_LOGI(TAG, "Read header from 0x%X: magic='%.4s' version=%u count=%u header_size=%u",
             FIRMWARE_STORAGE_OFFSET, header.magic, header.version, header.count, header.header_size);

    // Validate magic
    if (memcmp(header.magic, "FWST", 4) != 0) {
        ESP_LOGE(TAG, "Firmware storage not found (magic mismatch: '%.4s' != 'FWST')", header.magic);
        *count = 0;
        return ESP_ERR_NOT_FOUND;
    }

    *count = header.count;
    ESP_LOGI(TAG, "Firmware storage contains %u entries", *count);
    return ESP_OK;
}

esp_err_t firmware_storage_get_entry(uint32_t index, firmware_storage_entry_t* entry)
{
    if (!entry) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read header to get count
    firmware_storage_header_t header;
    esp_err_t ret = esp_flash_read(NULL, &header, FIRMWARE_STORAGE_OFFSET, sizeof(header));
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
    // Header starts at FIRMWARE_STORAGE_OFFSET
    // Entries start after header (sizeof(header) + sizeof(entries))
    size_t header_size = sizeof(firmware_storage_header_t);
    size_t entry_offset = FIRMWARE_STORAGE_OFFSET + header_size + (index * sizeof(firmware_storage_entry_t));

    // Read entry
    ret = esp_flash_read(NULL, entry, entry_offset, sizeof(firmware_storage_entry_t));
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
    // Firmware data starts after header + entries
    size_t header_size = sizeof(firmware_storage_header_t);
    size_t entries_size = entry->offset;  // Entry's offset field points to data
    uint32_t firmware_offset = FIRMWARE_STORAGE_OFFSET + header_size + entries_size;

    ESP_LOGI(TAG, "Reading firmware from flash: 0x%X (%u bytes)", firmware_offset, entry->size);

    // Read firmware data
    esp_err_t ret = esp_flash_read(NULL, buffer, firmware_offset, entry->size);
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
    esp_err_t ret = esp_flash_read(NULL, &header, FIRMWARE_STORAGE_OFFSET, sizeof(header));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read firmware storage header");
        return ret;
    }

    // Check if storage is initialized
    if (memcmp(header.magic, FIRMWARE_STORAGE_MAGIC, 4) != 0) {
        ESP_LOGI(TAG, "Initializing new firmware storage");
        memset(&header, 0, sizeof(header));
        memcpy(header.magic, FIRMWARE_STORAGE_MAGIC, 4);
        header.version = FIRMWARE_STORAGE_VERSION;
        header.count = 0;
        header.header_size = sizeof(firmware_storage_header_t);

        // Erase the firmware storage region before initialization
        // Firmware storage header + max entries fits in one sector (4KB)
        ESP_LOGI(TAG, "Erasing firmware storage region at 0x%X", FIRMWARE_STORAGE_OFFSET);
        ret = esp_flash_erase_region(NULL, FIRMWARE_STORAGE_OFFSET, 4096);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase firmware storage region: %s", esp_err_to_name(ret));
            return ret;
        }

        // Yield after erase operation to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(50));

        // Write initial header (count=0)
        ret = esp_flash_write(NULL, &header, FIRMWARE_STORAGE_OFFSET, sizeof(header));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write initial header: %s", esp_err_to_name(ret));
            return ret;
        }
    } else if (header.version != FIRMWARE_STORAGE_VERSION) {
        ESP_LOGE(TAG, "Firmware storage version mismatch: %u", header.version);
        return ESP_ERR_NOT_SUPPORTED;
    } else if (header.count >= MAX_FIRMWARE_ENTRIES) {
        ESP_LOGE(TAG, "Firmware storage full (%u entries)", header.count);
        return ESP_ERR_NO_MEM;
    }

    // Calculate where to write the new entry
    // Entries are stored sequentially after the header
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

    // Write entry to flash
    ESP_LOGI(TAG, "Writing entry %u at offset 0x%X: %s (%u bytes, CRC32: 0x%08X)",
             header.count, (unsigned int)entry_offset, entry.name, entry.size, entry.crc32);

    ret = esp_flash_write(NULL, &entry, entry_offset, sizeof(entry));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write firmware entry: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update header
    header.count++;

    // Read entire sector (4KB) to preserve entries
    uint8_t sector_buffer[4096];
    ret = esp_flash_read(NULL, sector_buffer, FIRMWARE_STORAGE_OFFSET, 4096);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sector for update: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update header in the sector buffer
    firmware_storage_header_t* sector_header = (firmware_storage_header_t*)sector_buffer;
    sector_header->count = header.count;

    // Erase and rewrite entire sector
    ESP_LOGI(TAG, "Erasing sector to update header count to %u", header.count);
    ret = esp_flash_erase_region(NULL, FIRMWARE_STORAGE_OFFSET, 4096);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase sector: %s", esp_err_to_name(ret));
        return ret;
    }

    // Yield after erase to prevent watchdog timeout
    vTaskDelay(pdMS_TO_TICKS(50));

    // Write entire sector back
    ESP_LOGI(TAG, "Writing updated sector to flash");
    ret = esp_flash_write(NULL, sector_buffer, FIRMWARE_STORAGE_OFFSET, 4096);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write sector: %s", esp_err_to_name(ret));
        return ret;
    }

    // Verify write by reading back
    firmware_storage_header_t verify_header;
    ret = esp_flash_read(NULL, &verify_header, FIRMWARE_STORAGE_OFFSET, sizeof(verify_header));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Verified header after write: magic='%.4s' version=%u count=%u",
                 verify_header.magic, verify_header.version, verify_header.count);
    } else {
        ESP_LOGW(TAG, "Could not verify header write: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "✓ Firmware entry added: %s (total: %u entries)", entry.name, header.count);
    return ESP_OK;
}
