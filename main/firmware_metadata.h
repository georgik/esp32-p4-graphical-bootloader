/**
 * @file firmware_metadata.h
 * @brief Firmware metadata persistence using NVS
 */

#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

#include "esp_system.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of firmware entries that can be stored
#define MAX_FIRMWARE_ENTRIES 10

// Firmware metadata structure
typedef struct {
    char filename[128];      // Filename from SD card
    char partition[16];      // Target partition name (ota_0, ota_1, ota_2)
    uint32_t offset;         // Flash offset
    uint32_t size;           // Firmware size in bytes
    uint32_t crc32;          // CRC32 checksum
    bool is_valid;           // Whether firmware passed validation
    uint32_t timestamp;      // When firmware was flashed
} firmware_metadata_t;

/**
 * @brief Initialize firmware metadata module
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_init(void);

/**
 * @brief Deinitialize firmware metadata module
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_deinit(void);

/**
 * @brief Get number of stored firmware entries
 * @param count Output parameter for firmware count
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_get_count(uint32_t* count);

/**
 * @brief Set firmware count
 * @param count Number of firmware entries
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_set_count(uint32_t count);

/**
 * @brief Get firmware metadata entry by index
 * @param index Firmware index (0 to MAX_FIRMWARE_ENTRIES-1)
 * @param metadata Output parameter for firmware metadata
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if index doesn't exist
 */
esp_err_t firmware_metadata_get(uint32_t index, firmware_metadata_t* metadata);

/**
 * @brief Set firmware metadata entry by index
 * @param index Firmware index (0 to MAX_FIRMWARE_ENTRIES-1)
 * @param metadata Firmware metadata to store
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_set(uint32_t index, const firmware_metadata_t* metadata);

/**
 * @brief Delete firmware metadata entry by index
 * @param index Firmware index to delete
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_delete(uint32_t index);

/**
 * @brief Clear all firmware metadata entries
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_clear_all(void);

/**
 * @brief Validate firmware metadata against actual flash contents
 * @param index Firmware index to validate
 * @param is_valid Output parameter for validation result
 * @return ESP_OK on success
 */
esp_err_t firmware_metadata_validate(uint32_t index, bool* is_valid);

/**
 * @brief Find firmware metadata by partition name
 * @param partition Partition name to search for
 * @param index Output parameter for found firmware index
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t firmware_metadata_find_by_partition(const char* partition, uint32_t* index);

/**
 * @brief Print all firmware metadata entries (for debugging)
 */
void firmware_metadata_print_all(void);

/**
 * @brief Scan firmware storage and populate NVS with firmware metadata
 *
 * This function scans the firmware storage area, reads all firmware entries,
 * and stores their metadata in NVS for later retrieval by the bootloader UI.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_metadata_scan_and_store(void);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_METADATA_H
