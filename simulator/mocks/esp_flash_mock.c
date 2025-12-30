/**
 * @file esp_flash_mock.c
 * @brief Mock implementation of ESP flash operations
 */

#include "esp_flash.h"
#include "esp_log_mock.h"
#include <string.h>

static const char* TAG = "esp_flash_mock";

esp_err_t esp_flash_read(void* chip, void* dst, size_t src_addr, size_t size) {
    (void)chip;
    (void)dst;
    (void)src_addr;
    (void)size;

    // Simulator: Read from flash emulator
    ESP_LOGD(TAG, "Flash read: offset=0x%08x size=%u", (unsigned int)src_addr, (unsigned int)size);
    return ESP_OK;
}

esp_err_t esp_flash_write(void* chip, size_t dst_addr, const void* src, size_t size) {
    (void)chip;
    (void)dst_addr;
    (void)src;
    (void)size;

    // Simulator: Write to flash emulator
    ESP_LOGD(TAG, "Flash write: offset=0x%08x size=%u", (unsigned int)dst_addr, (unsigned int)size);
    return ESP_OK;
}

esp_err_t esp_flash_erase_region(size_t start_addr, size_t size) {
    ESP_LOGD(TAG, "Flash erase: offset=0x%08x size=%u", (unsigned int)start_addr, (unsigned int)size);
    return ESP_OK;
}
