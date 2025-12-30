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

    esp_err_t esp_flash_read(esp_flash_t chip, void* dst, size_t src_addr, size_t size);
    esp_err_t esp_flash_write(esp_flash_t chip, size_t dst_addr, const void* src, size_t size);
    esp_err_t esp_flash_erase_region(size_t start_addr, size_t size);

    #ifdef __cplusplus
    }
    #endif

#else
    #include_next "esp_flash.h"
#endif

#endif // ESP_FLASH_H_MOCK
