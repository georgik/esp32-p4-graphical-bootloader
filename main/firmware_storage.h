/**
 * @file firmware_storage.h
 * @brief Firmware storage area definitions and scanning
 *
 * The firmware storage area contains pre-loaded firmware binaries that can be
 * selected and flashed without needing an SD card.
 */

#ifndef FIRMWARE_STORAGE_H
#define FIRMWARE_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "firmware_storage_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Firmware storage location is now defined in firmware_storage_config.h
// to ensure DRY principle across simulator and ESP-IDF code

/**
 * @brief Firmware storage entry structure (96 bytes)
 *
 * Each entry describes one firmware stored in the firmware storage area.
 */
typedef struct __attribute__((packed)) {
    uint32_t offset;        /* Offset from firmware data start (4 bytes) */
    uint32_t size;          /* Firmware size in bytes (4 bytes) */
    uint32_t crc32;         /* CRC32 checksum (4 bytes) */
    uint32_t flags;         /* Flags (4 bytes) */
    char name[64];          /* Firmware display name (64 bytes) */
    uint8_t reserved[12];   /* Reserved for future (12 bytes) */
    uint32_t next_offset;   /* Offset to next entry (4 bytes) */
} firmware_storage_entry_t;

/**
 * @brief Firmware storage header structure (32 bytes)
 *
 * Header at the start of the firmware storage area.
 */
typedef struct __attribute__((packed)) {
    char magic[4];          /* 'FWST' (Firmware Storage) (4 bytes) */
    uint32_t version;       /* Version 1 (4 bytes) */
    uint32_t count;         /* Number of firmwares (4 bytes) */
    uint32_t header_size;   /* Header size in bytes (4 bytes) */
    uint8_t reserved[16];   /* Reserved for future (16 bytes) */
} firmware_storage_header_t;

/**
 * @brief Check if firmware storage exists and is valid
 *
 * @param valid Output parameter set to true if storage is valid
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_storage_check_valid(bool* valid);

/**
 * @brief Get number of firmwares in storage
 *
 * @param count Output parameter for firmware count
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_storage_get_count(uint32_t* count);

/**
 * @brief Get firmware entry by index
 *
 * @param index Firmware index (0 to count-1)
 * @param entry Output parameter for firmware entry
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if index doesn't exist
 */
esp_err_t firmware_storage_get_entry(uint32_t index, firmware_storage_entry_t* entry);

/**
 * @brief Read firmware data from storage
 *
 * @param entry Firmware entry describing the firmware
 * @param buffer Output buffer (must be large enough for entry->size bytes)
 * @param buffer_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_storage_read_firmware(const firmware_storage_entry_t* entry,
                                         uint8_t* buffer,
                                         size_t buffer_size);

/**
 * @brief Add a firmware entry to storage
 *
 * @param name Firmware display name
 * @param offset Offset to firmware data from firmware storage start
 * @param size Firmware size in bytes
 * @param crc32 CRC32 checksum
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_storage_add_entry(const char* name,
                                     uint32_t offset,
                                     uint32_t size,
                                     uint32_t crc32);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_STORAGE_H
