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
#include <stdbool.h>
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

/**
 * @brief Firmware storage entry structure (96 bytes)
 */
typedef struct __attribute__((packed)) {
    uint32_t offset;        // Offset from firmware data start (4 bytes)
    uint32_t size;          // Firmware size in bytes (4 bytes)
    uint32_t crc32;         // CRC32 checksum (4 bytes)
    uint32_t flags;         // Flags (4 bytes)
    char name[64];          // Firmware filename (64 bytes)
    uint8_t reserved[12];   // Reserved for future (12 bytes)
    uint32_t next_offset;   // Offset to next entry (4 bytes)
} firmware_entry_t;

/**
 * @brief Firmware storage header structure (32 bytes)
 */
typedef struct __attribute__((packed)) {
    char magic[4];          // 'FWST' (Firmware Storage) (4 bytes)
    uint32_t version;       // Version 1 (4 bytes)
    uint32_t count;         // Number of firmwares (4 bytes)
    uint32_t header_size;   // Header size in bytes (4 bytes)
    uint8_t reserved[16];   // Reserved for future (16 bytes)
} firmware_storage_header_t;

/**
 * @brief Create flash image with multiple firmwares
 *
 * Creates a flash image containing:
 * - Bootloader at 0x2000
 * - Partition table at 0x10000
 * - Factory app at 0x20000
 * - Firmware storage area at 0xB0000 with multiple firmwares
 *
 * @param output_path Output filename
 * @param bootloader_path Bootloader binary path (or NULL for default)
 * @param partition_table_path Partition table path (or NULL for default)
 * @param factory_app_path Factory app path (or NULL for default)
 * @param firmware_paths Array of firmware file paths
 * @param firmware_names Array of firmware display names
 * @param firmware_count Number of firmwares
 * @param trim_zeros Trim trailing zeros from output
 * @param flash_size_mb Flash size in MB
 * @return FLASH_BUILDER_OK on success, error code otherwise
 */
flash_builder_err_t flash_builder_create_with_firmwares(
    const char* output_path,
    const char* bootloader_path,
    const char* partition_table_path,
    const char* factory_app_path,
    char** firmware_paths,
    char** firmware_names,
    int firmware_count,
    bool trim_zeros,
    int flash_size_mb
);

#endif // __SIMULATOR_BUILD__

#ifdef __cplusplus
}
#endif

#endif // FLASH_BUILDER_H
