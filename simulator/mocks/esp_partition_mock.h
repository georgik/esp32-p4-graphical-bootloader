/**
 * @file esp_partition_mock.h
 * @brief Mock implementation of ESP partition operations
 */

#ifndef ESP_PARTITION_MOCK_H
#define ESP_PARTITION_MOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_system_mock.h"

#ifdef __cplusplus
extern "C" {
#endif

// Partition types
typedef enum {
    ESP_PARTITION_TYPE_APP = 0x00,
    ESP_PARTITION_TYPE_DATA = 0x01,
    ESP_PARTITION_TYPE_USER = 0x40,
    ESP_PARTITION_TYPE_CUSTOM = 0x41,
    ESP_PARTITION_TYPE_ANY = 0xFF,  // Wildcard for searching
} esp_partition_type_t;

// Partition subtypes
typedef enum {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00,
    ESP_PARTITION_SUBTYPE_APP_OTA_MIN = 0x10,  // Minimum OTA subtype
    ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
    ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_APP_OTA_2 = 0x12,
    ESP_PARTITION_SUBTYPE_APP_OTA_3 = 0x13,
    ESP_PARTITION_SUBTYPE_APP_OTA_4 = 0x14,
    ESP_PARTITION_SUBTYPE_APP_OTA_5 = 0x15,
    ESP_PARTITION_SUBTYPE_APP_OTA_MAX = 0x20,
    ESP_PARTITION_SUBTYPE_APP_TEST = 0x20,
    ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02,
    ESP_PARTITION_SUBTYPE_DATA_PHY = 0x01,
    ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM = 0x04,
    ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD = 0x05,
    ESP_PARTITION_SUBTYPE_DATA_FAT = 0x06,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x07,
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00,  // OTA data partition
    ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

// Partition flags
#define PART_FLAG_ENCRYPTED 0x1  // Partition is encrypted

// Partition structure
typedef struct esp_partition_t {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erased_size;
    char label[17];
    uint32_t flags;
    bool encrypted;  // Encryption flag (needed by firmware_flasher.c)
    struct esp_partition_t* next;
} esp_partition_t;

// Internal partition info structure (for partition table generation)
typedef struct {
    uint16_t magic;              // ESP_PARTITION_MAGIC
    uint8_t  type;               // Partition type
    uint8_t  subtype;            // Partition subtype
    uint32_t offset;             // Partition offset in flash
    uint32_t size;               // Partition size in bytes
    uint8_t  flags;              // Partition flags
    char     label[16];          // Partition name
    struct {
        uint32_t offset;         // Position in table (for sorting)
        uint32_t size;           // Size field for table
    } pos;                       // Position structure
    uint32_t checksum;           // Checksum of partition table
} esp_partition_info_t;

// Partition magic number
#define ESP_PARTITION_MAGIC 0x50AA
#define ESP_PARTITION_MAGIC_MD5 0xEBAA

// Partition operations
const esp_partition_t* esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char* label
);

const esp_partition_t* esp_partition_next(const esp_partition_t* partition);

esp_err_t esp_partition_read(
    const esp_partition_t* partition,
    size_t src_offset,
    void* dst,
    size_t size
);

esp_err_t esp_partition_write(
    const esp_partition_t* partition,
    size_t dst_offset,
    const void* src,
    size_t size
);

esp_err_t esp_partition_erase_range(
    const esp_partition_t* partition,
    size_t start_addr,
    size_t size
);

esp_err_t esp_partition_get_sha256(
    const esp_partition_t* partition,
    uint8_t* sha256_out
);

// Flash operations
uint32_t esp_partition_get_flash_size(const esp_partition_t* partition);

// Partition iterator (for iterating through all partitions)
typedef struct esp_partition_iterator* esp_partition_iterator_t;

esp_partition_iterator_t esp_partition_find(esp_partition_type_t type,
                                           esp_partition_subtype_t subtype,
                                           const char* label);

const esp_partition_t* esp_partition_get(esp_partition_iterator_t iterator);

void esp_partition_iterator_release(esp_partition_iterator_t iterator);

#ifdef __cplusplus
}
#endif

#endif // ESP_PARTITION_MOCK_H
