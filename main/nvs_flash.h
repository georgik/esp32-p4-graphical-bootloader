/**
 * @file nvs_flash.h
 * @brief Shim for NVS flash
 */
#ifndef NVS_FLASH_H_SHIM
#define NVS_FLASH_H_SHIM
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
#ifdef __cplusplus
}
#endif
#endif
