/**
 * @file firmware_validator.h
 * @brief Firmware validation and integrity checking utilities
 *
 * Provides functions for validating ESP32 firmware binaries, calculating CRC32,
 * and checking file integrity for the multi-firmware bootloader.
 */

#ifndef FIRMWARE_VALIDATOR_H
#define FIRMWARE_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

// ESP32 firmware binary constants
#define ESP_APP_IMAGE_MAGIC           0xE9
#define ESP_APP_IMAGE_MAGIC_WORD     0xfeeddead
#define ESP_APP_IMAGE_MAX_SIZE       (16 * 1024 * 1024)  // 16MB max
#define ESP_APP_IMAGE_MIN_SIZE       0x1000              // 4KB min
#define ESP_APP_IMAGE_HEADER_SIZE    0x18

/**
 * @brief Firmware validation result structure
 */
typedef struct {
    bool is_valid;                  // Overall validation status
    bool has_magic;                 // ESP32 magic byte present
    bool has_correct_size;          // Size is within acceptable range
    bool has_valid_header;          // Header checksum is valid
    bool crc32_valid;               // CRC32 checksum matches
    uint32_t file_size;            // Actual file size
    uint32_t calculated_crc32;     // Calculated CRC32
    const char* error_message;      // Validation error description
} firmware_validation_result_t;

/**
 * @brief Validate ESP32 firmware binary
 *
 * @param file_path Path to firmware file
 * @param result Pointer to validation result structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_validate(const char* file_path, firmware_validation_result_t* result);

/**
 * @brief Calculate CRC32 of a file
 *
 * @param file_path Path to file
 * @param crc32 Pointer to store calculated CRC32
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_calculate_crc32(const char* file_path, uint32_t* crc32);

/**
 * @brief Verify CRC32 of a file
 *
 * @param file_path Path to file
 * @param expected_crc32 Expected CRC32 value
 * @param is_valid Pointer to store validation result
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_verify_crc32(const char* file_path, uint32_t expected_crc32, bool* is_valid);

/**
 * @brief Quick validation - check file existence and basic properties
 *
 * @param file_path Path to firmware file
 * @param file_size Pointer to store file size
 * @param is_valid Pointer to store quick validation result
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_quick_validate(const char* file_path, uint32_t* file_size, bool* is_valid);

/**
 * @brief Check if file has valid ESP32 firmware extension
 *
 * @param filename File name to check
 * @return true if file has .bin extension, false otherwise
 */
bool firmware_has_valid_extension(const char* filename);

/**
 * @brief Extract firmware display name from filename
 *
 * Removes directory path and .bin extension to create user-friendly name.
 * Example: "/sdcard/firmwares/app_v1.0.bin" -> "app_v1.0"
 *
 * @param file_path Full file path
 * @param display_name Buffer to store display name
 * @param buffer_size Size of display_name buffer
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_extract_display_name(const char* file_path, char* display_name, size_t buffer_size);

/**
 * @brief Format file size for display
 *
 * @param size_bytes Size in bytes
 * @param formatted_size Buffer to store formatted size string
 * @param buffer_size Size of formatted_size buffer
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_format_size(uint32_t size_bytes, char* formatted_size, size_t buffer_size);

/**
 * @brief Get human readable validation status
 *
 * @param result Validation result structure
 * @param status_buffer Buffer to store status string
 * @param buffer_size Size of status_buffer buffer
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_get_validation_status(const firmware_validation_result_t* result,
                                        char* status_buffer,
                                        size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_VALIDATOR_H