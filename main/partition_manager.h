/**
 * @file partition_manager.h
 * @brief Dynamic partition table management for ESP32-P4 multi-firmware bootloader
 *
 * Provides algorithms for creating optimized partition tables based on selected
 * firmware files, implementing logic from esp32-image-composer-rs.
 */

#ifndef PARTITION_MANAGER_H
#define PARTITION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "firmware_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

// Partition constants
#define MAX_PARTITIONS 16
#define PARTITION_ALIGNMENT 0x10000     // 64KB minimum alignment
#define OTA_PARTITION_SIZE 0x200000     // 2MB default OTA partition size
#define MIN_OTA_PARTITION_SIZE 0x100000  // 1MB minimum OTA partition size
#define MAX_PARTITION_NAME_LENGTH 16

// Flash layout constants (16MB total)
#define FLASH_SIZE (16 * 1024 * 1024)
#define BOOTLOADER_SIZE (32 * 1024)
#define PARTITION_TABLE_SIZE (4 * 1024)
#define FIRMWARE_REGISTRY_SIZE (8 * 1024)   // New partition for firmware registry
#define NVS_SIZE (8 * 1024)
#define PHY_INIT_SIZE (4 * 1024)
#define MIN_APP_SIZE (1 * 1024 * 1024)     // 1MB minimum for factory app
#define MAX_FIRMWARE_SIZE (4 * 1024 * 1024) // 4MB maximum per firmware

/**
 * @brief Partition type enumeration
 */
typedef enum {
    PARTITION_TYPE_BOOTLOADER = 0,
    PARTITION_TYPE_PARTITION_TABLE,
    PARTITION_TYPE_FIRMWARE_REGISTRY,
    PARTITION_TYPE_NVS,
    PARTITION_TYPE_PHY_INIT,
    PARTITION_TYPE_FACTORY_APP,
    PARTITION_TYPE_OTA_0,
    PARTITION_TYPE_OTA_1,
    PARTITION_TYPE_OTA_2,
    PARTITION_TYPE_OTA_3,
    PARTITION_TYPE_OTA_4,
    PARTITION_TYPE_OTA_5,
    PARTITION_TYPE_CUSTOM,
    PARTITION_TYPE_COUNT
} partition_type_t;

/**
 * @brief Partition information structure
 */
typedef struct {
    char name[MAX_PARTITION_NAME_LENGTH];
    partition_type_t type;
    uint32_t subtype;
    uint32_t offset;
    uint32_t size;
    bool is_ota;
    bool is_readonly;
    bool is_encrypted;
    firmware_info_t* firmware;  // Associated firmware (NULL for system partitions)
} partition_info_t;

/**
 * @brief Partition table layout
 */
typedef struct {
    partition_info_t partitions[MAX_PARTITIONS];
    uint32_t partition_count;
    uint32_t total_used_size;
    bool has_valid_layout;
} partition_table_layout_t;

/**
 * @brief Partition allocation request
 */
typedef struct {
    firmware_info_t* firmware;
    uint32_t min_size;
    uint32_t preferred_size;
    bool requires_ota_slot;
    uint8_t priority;  // 1=highest, 255=lowest
} partition_allocation_request_t;

/**
 * @brief Initialize partition manager
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_init(void);

/**
 * @brief Generate optimal partition table layout for selected firmwares
 *
 * Creates a partition table that accommodates all selected firmware files
 * while preserving existing system partitions and optimizing for OTA operations.
 *
 * @param selector Firmware selector with selected files
 * @param layout Output partition table layout
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_generate_layout(firmware_selector_t* selector,
                                            partition_table_layout_t* layout);

/**
 * @brief Create partition table binary data
 *
 * Generates the ESP32 partition table binary format that can be written
 * to the device's flash memory.
 *
 * @param layout Partition table layout
 * @param buffer Output buffer for partition table data
 * @param buffer_size Buffer size
 * @param actual_size Pointer to store actual size written
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_create_binary(const partition_table_layout_t* layout,
                                         uint8_t* buffer,
                                         size_t buffer_size,
                                         size_t* actual_size);

/**
 * @brief Validate partition table layout
 *
 * Checks if the partition layout is valid and will work on the device.
 *
 * @param layout Partition table layout to validate
 * @param is_valid Pointer to store validation result
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_validate_layout(const partition_table_layout_t* layout,
                                             bool* is_valid);

/**
 * @brief Find available space for partitions
 *
 * Calculates the available flash space for firmware partitions
 * after accounting for system partitions.
 *
 * @param total_space Pointer to store total available space
 * @param available_space Pointer to store available space for firmware
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_get_available_space(uint32_t* total_space,
                                                uint32_t* available_space);

/**
 * @brief Estimate partition table size
 *
 * @param layout Partition table layout
 * @param estimated_size Pointer to store estimated size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_estimate_size(const partition_table_layout_t* layout,
                                          uint32_t* estimated_size);

/**
 * @brief Optimize partition allocation
 *
 * Optimizes the allocation of firmware partitions to minimize fragmentation
 * and maximize available space.
 *
 * @param requests Array of partition allocation requests
 * @param request_count Number of requests
 * @param layout Output optimized layout
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_optimize_allocation(const partition_allocation_request_t* requests,
                                                 uint32_t request_count,
                                                 partition_table_layout_t* layout);

/**
 * @brief Get firmware partition mapping
 *
 * Maps firmware files to their assigned partitions in the layout.
 *
 * @param layout Partition table layout
 * @param firmware_index Index of firmware to find
 * @param partition_info Pointer to store partition information
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_get_firmware_partition(const partition_table_layout_t* layout,
                                                   uint32_t firmware_index,
                                                   partition_info_t** partition_info);

/**
 * @brief Backup current partition table
 *
 * Reads and backs up the current partition table for recovery purposes.
 *
 * @param backup_buffer Buffer to store backup data
 * @param buffer_size Buffer size
 * @param backup_size Pointer to store actual backup size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_backup_current(uint8_t* backup_buffer,
                                          size_t buffer_size,
                                          size_t* backup_size);

/**
 * @brief Restore partition table from backup
 *
 * Restores the partition table from a previously created backup.
 *
 * @param backup_buffer Backup data buffer
 * @param backup_size Size of backup data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_restore_from_backup(const uint8_t* backup_buffer,
                                                 size_t backup_size);

/**
 * @brief Print partition table layout (debug)
 *
 * @param layout Partition table layout to print
 */
void partition_manager_print_layout(const partition_table_layout_t* layout);

/**
 * @brief Cleanup partition manager resources
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_manager_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // PARTITION_MANAGER_H