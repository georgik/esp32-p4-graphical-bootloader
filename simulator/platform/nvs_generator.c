/**
 * @file nvs_generator.c
 * @brief NVS partition generator for firmware metadata
 */

#ifdef __SIMULATOR_BUILD__

#include "nvs_generator.h"
#include "firmware_metadata.h"
#include "nvs_mock.h"
#include "esp_log_mock.h"
#include <string.h>

static const char* TAG = "nvs_generator";

int nvs_generate_firmware_metadata(
    uint8_t* flash_image,
    size_t flash_size,
    char** firmware_names,
    uint32_t* firmware_sizes,
    uint32_t* firmware_crcs,
    int firmware_count,
    uint32_t nvs_offset,
    size_t nvs_size)
{
    if (!flash_image || !firmware_names || !firmware_sizes || !firmware_crcs) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    if (nvs_offset + nvs_size > flash_size) {
        ESP_LOGE(TAG, "NVS partition out of bounds");
        return -1;
    }

    ESP_LOGI(TAG, "Generating NVS firmware metadata for %d firmwares", firmware_count);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return -1;
    }

    // Clear any existing metadata
    firmware_metadata_clear_all();

    // Store metadata for each firmware
    for (int i = 0; i < firmware_count; i++) {
        firmware_metadata_t metadata;
        memset(&metadata, 0, sizeof(metadata));

        // Copy filename
        strncpy(metadata.filename, firmware_names[i], sizeof(metadata.filename) - 1);
        metadata.filename[sizeof(metadata.filename) - 1] = '\0';

        // Set partition (all go to ota_0 for now)
        strncpy(metadata.partition, "ota_0", sizeof(metadata.partition) - 1);

        // Set offset (firmware storage area + data offset)
        // Firmware data starts after header (offset 0xE0)
        metadata.offset = 0x110000 + 0xE0 + (i * sizeof(firmware_entry_t));

        // For now, use the actual firmware storage offset
        // In reality, each firmware would be flashed to ota_0, not stored in firmware storage
        // But for pre-populating metadata, we mark them as available
        metadata.offset = 0x110000;  // Firmware storage base

        metadata.size = firmware_sizes[i];
        metadata.crc32 = firmware_crcs[i];
        metadata.is_valid = true;
        metadata.timestamp = 0;  // Will be set on actual flash

        ESP_LOGI(TAG, "Storing metadata for firmware %d:", i);
        ESP_LOGI(TAG, "  Name: %s", metadata.filename);
        ESP_LOGI(TAG, "  Size: %u bytes", metadata.size);
        ESP_LOGI(TAG, "  CRC32: 0x%08X", metadata.crc32);

        ret = firmware_metadata_set(i, &metadata);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set metadata for firmware %d", i);
            continue;
        }
    }

    // Set firmware count
    ret = firmware_metadata_set_count(firmware_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set firmware count: %s", esp_err_to_name(ret));
        nvs_flash_deinit();
        return -1;
    }

    // Commit all NVS data
    nvs_handle_t handle;
    ret = nvs_open("firmware_config", NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "✓ NVS metadata generated for %d firmwares", firmware_count);

    // NOTE: In the simulator, NVS data is stored in JSON format by the mock
    // This JSON is written to the flash emulator's simulated NVS partition
    // However, for the real device, we need actual NVS binary format
    //
    // For now, we'll leave the NVS partition as 0xFF (empty)
    // The bootloader will populate it when firmwares are actually flashed
    //
    // TODO: Implement proper NVS binary format generation for pre-seeding data

    // Deinitialize NVS
    nvs_flash_deinit();

    // Fill NVS partition with 0xFF (empty/erased state)
    memset(flash_image + nvs_offset, 0xFF, nvs_size);
    ESP_LOGI(TAG, "✓ NVS partition at 0x%X initialized (empty, awaiting runtime population)", nvs_offset);

    return 0;
}

#endif // __SIMULATOR_BUILD__
