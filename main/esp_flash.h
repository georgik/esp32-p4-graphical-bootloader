/**
 * @file esp_flash.h
 * @brief Shim for flash operations
 */
#ifndef ESP_FLASH_H_SHIM
#define ESP_FLASH_H_SHIM
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// Flash operations (with optional handle parameter)
esp_err_t esp_flash_read(void* chip, void* dst, size_t src_addr, size_t size);
esp_err_t esp_flash_write(void* chip, size_t dst_addr, const void* src, size_t size);
esp_err_t esp_flash_erase_region(size_t start_addr, size_t size);

#ifdef __cplusplus
}
#endif
#endif
