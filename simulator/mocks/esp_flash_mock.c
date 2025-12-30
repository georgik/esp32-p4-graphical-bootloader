/**
 * @file esp_flash_mock.c
 * @brief Mock implementation of ESP flash operations using flash emulator
 */

#include "esp_flash.h"
#include "esp_log_mock.h"
#include "../platform/flash_emulator.h"
#include <string.h>

static const char* TAG = "esp_flash_mock";

esp_err_t esp_flash_read(void* chip, void* dst, size_t src_addr, size_t size) {
    (void)chip;

    // Use flash emulator to read
    esp_err_t ret = flash_emulator_read(src_addr, dst, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash read failed: offset=0x%08x size=%u", (unsigned int)src_addr, (unsigned int)size);
        return ret;
    }

    ESP_LOGD(TAG, "Flash read: offset=0x%08x size=%u", (unsigned int)src_addr, (unsigned int)size);
    return ESP_OK;
}

esp_err_t esp_flash_write(void* chip, const void* src, size_t dst_addr, size_t size) {
    (void)chip;

    // Use flash emulator to write
    esp_err_t ret = flash_emulator_write(dst_addr, src, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash write failed: offset=0x%08x size=%u", (unsigned int)dst_addr, (unsigned int)size);
        return ret;
    }

    ESP_LOGD(TAG, "Flash write: offset=0x%08x size=%u", (unsigned int)dst_addr, (unsigned int)size);
    return ESP_OK;
}

esp_err_t esp_flash_erase_region(void* chip, size_t start_addr, size_t size) {
    (void)chip;

    // Use flash emulator to erase
    esp_err_t ret = flash_emulator_erase(start_addr, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flash erase failed: offset=0x%08x size=%u", (unsigned int)start_addr, (unsigned int)size);
        return ret;
    }

    ESP_LOGD(TAG, "Flash erase: offset=0x%08x size=%u", (unsigned int)start_addr, (unsigned int)size);
    return ESP_OK;
}
