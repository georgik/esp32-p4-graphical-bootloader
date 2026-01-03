/**
 * @file partition_table.h
 * @brief ESP-IDF MD5 partition table format definitions
 *
 * This file defines the ESP-IDF partition table structures according to the
 * MD5 format specification. All partition entries are exactly 32 bytes.
 */

#ifndef PARTITION_TABLE_H
#define PARTITION_TABLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP-IDF Partition Table Entry (MD5 format)
 *
 * Structure layout (exactly 32 bytes):
 * - Offset 0-1:   Magic number (0x50AA)
 * - Offset 2:    Type (0x00=APP, 0x01=DATA)
 * - Offset 3:    Subtype (depends on type)
 * - Offset 4-7:  Partition offset (in bytes)
 * - Offset 8-11: Partition size (in bytes)
 * - Offset 12-27: Partition name (null-terminated)
 * - Offset 28-31: Flags
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;       ///< Magic number 0x50AA (little-endian: 0xAA50)
    uint8_t  type;        ///< Partition type (0x00=APP, 0x01=DATA)
    uint8_t  subtype;     ///< Partition subtype
    uint32_t offset;      ///< Partition offset in bytes
    uint32_t size;        ///< Partition size in bytes
    char     name[16];    ///< Partition name (15 chars + null terminator)
    uint32_t flags;       ///< Partition flags
} partition_entry_t;

/**
 * @brief Partition Table Header (MD5 format)
 *
 * Structure layout:
 * - Offset 0-1:   Magic number (0x50AA)
 * - Offset 2:    Entry size (typically 0x20 = 32 bytes)
 * - Offset 3:    Number of entries
 * - Offset 4-7:  Reserved (must be 0)
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;       ///< Magic number 0x50AA
    uint8_t  entry_size;  ///< Size of each entry in bytes (typically 0x20)
    uint8_t  num_entries; ///< Number of partition entries
    uint32_t reserved;    ///< Reserved field (must be 0)
} partition_table_header_t;

// Partition type constants
#define PART_TYPE_APP   0x00
#define PART_TYPE_DATA  0x01

// Partition subtype constants for APP type
#define PART_SUBTYPE_FACTORY  0x00  // Factory app
#define PART_SUBTYPE_TEST     0x20  // Test app
#define PART_SUBTYPE_OTA_0    0x10  // OTA mask + 0
#define PART_SUBTYPE_OTA_1    0x11  // OTA mask + 1
#define PART_SUBTYPE_OTA_2    0x12  // OTA mask + 2
#define PART_SUBTYPE_OTA_3    0x13  // OTA mask + 3
#define PART_SUBTYPE_OTA_MASK 0x10

// Partition subtype constants for DATA type
#define PART_SUBTYPE_NVS      0x02
#define PART_SUBTYPE_PHY      0x01
#define PART_SUBTYPE_SPIFFS   0x02
#define PART_SUBTYPE_CUSTOM   0x99

// Partition magic number
#define PARTITION_MAGIC 0x50AA

// Standard partition entry size
#define PARTITION_ENTRY_SIZE 32

#ifdef __cplusplus
}
#endif

#endif // PARTITION_TABLE_H
