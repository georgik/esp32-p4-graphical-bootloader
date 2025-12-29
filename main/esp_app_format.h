/**
 * @file esp_app_format.h
 * @brief Shim for ESP app format (minimal for simulator)
 */

#ifndef ESP_APP_FORMAT_H_SHIM
#define ESP_APP_FORMAT_H_SHIM

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal app format definitions for simulator
typedef struct {
    uint32_t magic;
    uint8_t  app_elf_sha256[32];
    uint32_t app_descaddr;
    uint8_t  spi_pin;
    uint32_t strapping;
} esp_image_header_t;

#ifdef __cplusplus
}
#endif

#endif // ESP_APP_FORMAT_H_SHIM
