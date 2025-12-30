/**
 * @file flash_emulator.h
 * @brief Flash write emulation with visualization callbacks and mmap support
 */

#ifndef FLASH_EMULATOR_H
#define FLASH_EMULATOR_H

#include "esp_system_mock.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

/**
 * @brief Initialize flash emulator with mmap
 *
 * Maps the simulated-flash.bin file into memory for fast access.
 * Must be called before any read/write operations.
 *
 * @param flash_path Path to flash image file (e.g., "simulated-flash.bin")
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t flash_emulator_init(const char* flash_path);

/**
 * @brief Cleanup flash emulator
 *
 * Unmaps the flash image and closes file descriptors.
 */
void flash_emulator_deinit(void);

/**
 * @brief Check if flash emulator is initialized
 *
 * @return true if initialized, false otherwise
 */
bool flash_emulator_is_initialized(void);

/**
 * @brief Read from flash image
 *
 * @param offset Offset in bytes from start of flash
 * @param buffer Output buffer
 * @param size Number of bytes to read
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t flash_emulator_read(uint32_t offset, void* buffer, size_t size);

/**
 * @brief Write to flash image
 *
 * @param offset Offset in bytes from start of flash
 * @param data Input data
 * @param size Number of bytes to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t flash_emulator_write(uint32_t offset, const void* data, size_t size);

/**
 * @brief Erase region of flash (fill with 0xFF)
 *
 * @param offset Offset in bytes from start of flash
 * @param size Number of bytes to erase
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t flash_emulator_erase(uint32_t offset, size_t size);

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
