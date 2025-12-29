/**
 * @file esp_crc.h
 * @brief Shim for CRC operations
 */
#ifndef ESP_CRC_H_SHIM
#define ESP_CRC_H_SHIM
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t esp_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len);
#ifdef __cplusplus
}
#endif
#endif
