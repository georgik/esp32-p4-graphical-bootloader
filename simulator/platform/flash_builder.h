/**
 * @file flash_builder.h
 * @brief Flash image builder for simulator
 *
 * Creates a simulated flash memory image from ESP-IDF build artifacts.
 * This allows the simulator to read/write flash operations without hardware.
 */

#ifndef FLASH_BUILDER_H
#define FLASH_BUILDER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>  // For ssize_t

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __SIMULATOR_BUILD__

/**
 * @brief Flash configuration constants
 */
#define SIMULATED_FLASH_SIZE        (16 * 1024 * 1024)  // 16MB
#define BOOTLOADER_OFFSET           0x2000
#define PARTITION_TABLE_OFFSET      0x10000
#define FACTORY_APP_OFFSET          0x20000

/**
 * @brief Result codes for flash builder operations
 */
typedef enum {
    FLASH_BUILDER_OK = 0,
    FLASH_BUILDER_ERR_FILE_EXISTS = -1,
    FLASH_BUILDER_ERR_NO_MEM = -2,
    FLASH_BUILDER_ERR_IO = -3,
    FLASH_BUILDER_ERR_INVALID_ARGS = -4,
    FLASH_BUILDER_ERR_MISSING_FILE = -5,
} flash_builder_err_t;

/**
 * @brief Build a simulated flash image from ESP-IDF build directory
 *
 * Creates a 16MB flash image file containing:
 * - Bootloader at 0x2000
 * - Partition table at 0x10000
 * - Application at 0x20000
 * - Rest filled with zeros
 *
 * @param output_path Path where flash image will be created (e.g., "simulated-flash.bin")
 * @param esp_idf_build_dir Path to ESP-IDF build directory (e.g., "../build/")
 * @return FLASH_BUILDER_OK on success, error code otherwise
 */
flash_builder_err_t flash_builder_create(const char* output_path,
                                        const char* esp_idf_build_dir);

/**
 * @brief Check if flash image file exists
 *
 * @param flash_path Path to flash image file
 * @return 1 if exists, 0 otherwise
 */
int flash_builder_exists(const char* flash_path);

/**
 * @brief Validate flash image integrity
 *
 * Checks that the flash image file exists and has the correct size.
 *
 * @param flash_path Path to flash image file
 * @return FLASH_BUILDER_OK if valid, error code otherwise
 */
flash_builder_err_t flash_builder_validate(const char* flash_path);

/**
 * @brief Get file size
 *
 * @param filepath Path to file
 * @return File size in bytes, or -1 on error
 */
long flash_builder_get_file_size(const char* filepath);

/**
 * @brief Read binary file into buffer
 *
 * @param filepath Path to file
 * @param buffer Output buffer (must be large enough)
 * @param max_size Maximum bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t flash_builder_read_file(const char* filepath, void* buffer, size_t max_size);

#endif // __SIMULATOR_BUILD__

#ifdef __cplusplus
}
#endif

#endif // FLASH_BUILDER_H
