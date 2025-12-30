/**
 * @file esp_flash.h
 * @brief ESP flash operations
 */

#ifndef ESP_FLASH_H_MOCK
#define ESP_FLASH_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    #include <stddef.h>
    #include <stdint.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    typedef void* esp_flash_t;

    // Note: Order is (chip, dst, src_addr, size) for read
    esp_err_t esp_flash_read(esp_flash_t chip, void* dst, size_t src_addr, size_t size);

    // Note: OLD API order is (chip, src, dst_addr, size) - used by firmware_flasher.c
    // This matches the older ESP-IDF API before it was changed
    esp_err_t esp_flash_write(esp_flash_t chip, const void* src, size_t dst_addr, size_t size);

    // Note: Order is (chip, start_addr, size) for erase
    esp_err_t esp_flash_erase_region(esp_flash_t chip, size_t start_addr, size_t size);

    #ifdef __cplusplus
    }
    #endif

#else
    #include_next "esp_flash.h"
#endif

#endif // ESP_FLASH_H_MOCK
