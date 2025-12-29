/**
 * @file esp_flash_partitions.h
 * @brief Shim for flash partitions
 */

#ifndef ESP_FLASH_PARTITIONS_H_SHIM
#define ESP_FLASH_PARTITIONS_H_SHIM

#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

// Minimal definitions for simulator
typedef struct {
    const char* label;
    uint32_t offset;
    uint32_t size;
} esp_partition_pos_t;

#ifdef __cplusplus
}
#endif

#endif // ESP_FLASH_PARTITIONS_H_SHIM
