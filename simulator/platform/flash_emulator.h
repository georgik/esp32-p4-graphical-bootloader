/**
 * @file flash_emulator.h
 * @brief Flash write emulation with visualization callbacks
 */

#ifndef FLASH_EMULATOR_H
#define FLASH_EMULATOR_H

#include "esp_system_mock.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Flash operation types
typedef enum {
    FLASH_OP_READ,
    FLASH_OP_WRITE,
    FLASH_OP_ERASE,
} flash_op_type_t;

// Progress callback for visualization
typedef void (*flash_progress_callback_t)(
    flash_op_type_t operation,
    uint32_t offset,
    uint32_t size,
    uint32_t total_size,
    const char* partition_name
);

// Set progress callback
void flash_emulator_set_progress_callback(flash_progress_callback_t callback);

// Get flash operation statistics
typedef struct {
    uint32_t bytes_read;
    uint32_t bytes_written;
    uint32_t bytes_erased;
    uint32_t operation_count;
    uint32_t total_time_ms;
} flash_stats_t;

void flash_emulator_get_stats(flash_stats_t* stats);
void flash_emulator_reset_stats(void);

// Simulate flash write with progress tracking
esp_err_t flash_emulator_write_partition(
    const char* partition_name,
    uint32_t offset,
    const uint8_t* data,
    uint32_t size
);

// Simulate flash erase with progress tracking
esp_err_t flash_emulator_erase_partition(
    const char* partition_name,
    uint32_t offset,
    uint32_t size
);

#ifdef __cplusplus
}
#endif

#endif // FLASH_EMULATOR_H
