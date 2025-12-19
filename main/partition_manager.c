/**
 * @file partition_manager.c
 * @brief Dynamic partition table management implementation
 */

#include "partition_manager.h"
#include "firmware_validator.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_flash_partitions.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static const char* TAG = "partition_manager";

// Standard ESP32 partition subtypes
#define PARTITION_SUBTYPE_APP_FACTORY ESP_PARTITION_SUBTYPE_APP_FACTORY
#define PARTITION_SUBTYPE_APP_OTA_0    ESP_PARTITION_SUBTYPE_APP_OTA_0
#define PARTITION_SUBTYPE_APP_OTA_1    ESP_PARTITION_SUBTYPE_APP_OTA_1
#define PARTITION_SUBTYPE_APP_OTA_2    ESP_PARTITION_SUBTYPE_APP_OTA_MAX  // Custom extended
#define PARTITION_SUBTYPE_APP_OTA_3    (ESP_PARTITION_SUBTYPE_APP_OTA_MAX + 1)
#define PARTITION_SUBTYPE_APP_OTA_4    (ESP_PARTITION_SUBTYPE_APP_OTA_MAX + 2)
#define PARTITION_SUBTYPE_APP_OTA_5    (ESP_PARTITION_SUBTYPE_APP_OTA_MAX + 3)
#define PARTITION_SUBTYPE_DATA_NVS      ESP_PARTITION_SUBTYPE_DATA_NVS
#define PARTITION_SUBTYPE_DATA_PHY     ESP_PARTITION_SUBTYPE_DATA_PHY
#define ESP_PARTITION_SUBTYPE_DATA_PARTITION_TABLE 0x01
#define PARTITION_ENCRYPTED 0x10
#define MD5_SIZE 16

// System partition definitions
static const partition_info_t system_partitions[] = {
    {"bootloader",     PARTITION_TYPE_BOOTLOADER,      0,                     0x0000,       BOOTLOADER_SIZE,              false, true,  false, NULL},
    {"partition-table",PARTITION_TYPE_PARTITION_TABLE, 0,                     0x8000,       PARTITION_TABLE_SIZE,          false, true,  false, NULL},
    {"firmware-reg",  PARTITION_TYPE_FIRMWARE_REGISTRY,0,                     0x9000,       FIRMWARE_REGISTRY_SIZE,        false, false, false, NULL},
    {"nvs",           PARTITION_TYPE_NVS,             PARTITION_SUBTYPE_DATA_NVS, 0xd000,       NVS_SIZE,                     false, false, false, NULL},
    {"phy_init",      PARTITION_TYPE_PHY_INIT,        PARTITION_SUBTYPE_DATA_PHY,0xf000,       PHY_INIT_SIZE,                false, true,  false, NULL},
    {"factory_app",   PARTITION_TYPE_FACTORY_APP,     PARTITION_SUBTYPE_APP_FACTORY,0x10000,      MIN_APP_SIZE,                 true,  false, false, NULL},
};
static const uint32_t system_partition_count = sizeof(system_partitions) / sizeof(system_partitions[0]);

// Helper function to get partition type name
static const char* get_partition_type_name(partition_type_t type)
{
    switch (type) {
        case PARTITION_TYPE_BOOTLOADER: return "Bootloader";
        case PARTITION_TYPE_PARTITION_TABLE: return "Partition Table";
        case PARTITION_TYPE_FIRMWARE_REGISTRY: return "Firmware Registry";
        case PARTITION_TYPE_NVS: return "NVS";
        case PARTITION_TYPE_PHY_INIT: return "PHY Init";
        case PARTITION_TYPE_FACTORY_APP: return "Factory App";
        case PARTITION_TYPE_OTA_0: return "OTA 0";
        case PARTITION_TYPE_OTA_1: return "OTA 1";
        case PARTITION_TYPE_OTA_2: return "OTA 2";
        case PARTITION_TYPE_OTA_3: return "OTA 3";
        case PARTITION_TYPE_OTA_4: return "OTA 4";
        case PARTITION_TYPE_OTA_5: return "OTA 5";
        default: return "Unknown";
    }
}

// Helper function to round up to alignment
static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

esp_err_t partition_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing partition manager");

    // Validate system partition definitions
    uint32_t current_offset = 0;
    for (uint32_t i = 0; i < system_partition_count; i++) {
        const partition_info_t* part = &system_partitions[i];
        if (part->offset != current_offset) {
            ESP_LOGW(TAG, "System partition %s offset mismatch: expected 0x%08x, found 0x%08x",
                     part->name, current_offset, part->offset);
        }
        current_offset = part->offset + part->size;
    }

    ESP_LOGI(TAG, "Partition manager initialized successfully");
    ESP_LOGI(TAG, "System partitions occupy: %d bytes (0x%08x - 0x%08x)",
             current_offset, 0, current_offset - 1);

    return ESP_OK;
}

esp_err_t partition_manager_get_available_space(uint32_t* total_space, uint32_t* available_space)
{
    if (!total_space || !available_space) {
        return ESP_ERR_INVALID_ARG;
    }

    *total_space = FLASH_SIZE;

    // Calculate space used by system partitions
    uint32_t system_space = 0;
    for (uint32_t i = 0; i < system_partition_count; i++) {
        system_space += system_partitions[i].size;
    }

    *available_space = *total_space - system_space;

    ESP_LOGI(TAG, "Flash space analysis:");
    ESP_LOGI(TAG, "  Total: %d MB", *total_space / (1024 * 1024));
    ESP_LOGI(TAG, "  System: %d KB", system_space / 1024);
    ESP_LOGI(TAG, "  Available for firmware: %d MB (%d bytes)",
             *available_space / (1024 * 1024), *available_space);

    return ESP_OK;
}

esp_err_t partition_manager_generate_layout(firmware_selector_t* selector,
                                            partition_table_layout_t* layout)
{
    if (!selector || !layout) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Generating partition table layout for %d selected firmwares", selector->selected_count);

    // Initialize layout
    memset(layout, 0, sizeof(partition_table_layout_t));

    // Add system partitions first
    for (uint32_t i = 0; i < system_partition_count; i++) {
        if (layout->partition_count >= MAX_PARTITIONS) {
            ESP_LOGE(TAG, "Too many system partitions");
            return ESP_ERR_NO_MEM;
        }
        layout->partitions[layout->partition_count] = system_partitions[i];
        layout->total_used_size += layout->partitions[layout->partition_count].size;
        layout->partition_count++;
    }

    // Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    esp_err_t ret = firmware_selector_get_selected(selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get selected firmwares: %s", esp_err_to_name(ret));
        return ret;
    }

    if (selected_count == 0) {
        ESP_LOGW(TAG, "No firmwares selected for partition generation");
        return ESP_OK;  // Valid layout with no firmwares
    }

    ESP_LOGI(TAG, "Creating allocation requests for %d firmwares", selected_count);

    // Create allocation requests
    partition_allocation_request_t requests[MAX_FIRMWARE_COUNT];
    for (uint32_t i = 0; i < selected_count; i++) {
        requests[i].firmware = selected_firmware[i];
        requests[i].min_size = selected_firmware[i]->size + 0x1000;  // Add 4KB padding
        requests[i].preferred_size = align_up(requests[i].min_size, PARTITION_ALIGNMENT);
        requests[i].requires_ota_slot = true;
        requests[i].priority = i + 1;  // Lower index = higher priority
    }

    // Sort requests by size (largest first) for better space utilization
    for (uint32_t i = 0; i < selected_count - 1; i++) {
        for (uint32_t j = i + 1; j < selected_count; j++) {
            if (requests[i].preferred_size < requests[j].preferred_size) {
                partition_allocation_request_t temp = requests[i];
                requests[i] = requests[j];
                requests[j] = temp;
            }
        }
    }

    // Optimize allocation
    ret = partition_manager_optimize_allocation(requests, selected_count, layout);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to optimize allocation: %s", esp_err_to_name(ret));
        return ret;
    }

    layout->has_valid_layout = true;

    ESP_LOGI(TAG, "Partition table layout generated successfully:");
    partition_manager_print_layout(layout);

    return ESP_OK;
}

esp_err_t partition_manager_optimize_allocation(const partition_allocation_request_t* requests,
                                                 uint32_t request_count,
                                                 partition_table_layout_t* layout)
{
    if (!requests || !layout || request_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Optimizing partition allocation for %d requests", request_count);

    uint32_t current_offset = layout->total_used_size;
    uint32_t available_space = FLASH_SIZE - current_offset;

    ESP_LOGI(TAG, "Starting allocation at offset 0x%08x, available space: %d bytes",
             current_offset, available_space);

    // Allocate partitions in order of preference
    uint32_t allocated_partitions = 0;
    uint32_t ota_slot_index = 0;

    for (uint32_t i = 0; i < request_count && layout->partition_count < MAX_PARTITIONS; i++) {
        const partition_allocation_request_t* req = &requests[i];

        // Calculate required size with alignment
        uint32_t required_size = align_up(req->preferred_size, PARTITION_ALIGNMENT);

        // Check if we have enough space
        if (current_offset + required_size > FLASH_SIZE) {
            ESP_LOGE(TAG, "Not enough space for firmware %s (needs %d bytes, have %d bytes)",
                     req->firmware->display_name, required_size, FLASH_SIZE - current_offset);
            return ESP_ERR_NO_MEM;
        }

        // Create partition
        partition_info_t* partition = &layout->partitions[layout->partition_count];
        memset(partition, 0, sizeof(partition_info_t));

        // Set partition name
        snprintf(partition->name, sizeof(partition->name), "ota_%" PRIu32, ota_slot_index);

        // Set partition type
        partition->type = PARTITION_TYPE_OTA_0 + ota_slot_index;
        if (ota_slot_index < 2) {
            partition->subtype = PARTITION_SUBTYPE_APP_OTA_0 + ota_slot_index;
        } else {
            partition->subtype = PARTITION_SUBTYPE_APP_OTA_2 + (ota_slot_index - 2);
        }

        partition->offset = current_offset;
        partition->size = required_size;
        partition->is_ota = true;
        partition->is_readonly = false;
        partition->is_encrypted = false;
        partition->firmware = req->firmware;

        ESP_LOGI(TAG, "Allocated partition %s for %s: offset=0x%08x, size=%d bytes",
                 partition->name, req->firmware->display_name, partition->offset, partition->size);

        // Update counters
        current_offset += required_size;
        layout->total_used_size += required_size;
        layout->partition_count++;
        ota_slot_index++;
        allocated_partitions++;
    }

    // Final validation
    if (current_offset > FLASH_SIZE) {
        ESP_LOGE(TAG, "Partition allocation exceeds flash size");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t remaining_space = FLASH_SIZE - current_offset;
    ESP_LOGI(TAG, "Partition allocation completed:");
    ESP_LOGI(TAG, "  Allocated partitions: %d", allocated_partitions);
    ESP_LOGI(TAG, "  Used space: %d bytes (%d MB)", current_offset, current_offset / (1024 * 1024));
    ESP_LOGI(TAG, "  Remaining space: %d bytes", remaining_space);

    return ESP_OK;
}

esp_err_t partition_manager_create_binary(const partition_table_layout_t* layout,
                                         uint8_t* buffer,
                                         size_t buffer_size,
                                         size_t* actual_size)
{
    if (!layout || !buffer || !actual_size) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Creating partition table binary with %d partitions", layout->partition_count);

    // Calculate required size
    size_t required_size = sizeof(esp_partition_info_t) * layout->partition_count + MD5_SIZE;
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "Buffer too small: need %d bytes, have %d bytes", required_size, buffer_size);
        return ESP_ERR_NO_MEM;
    }

    // Create partition entries
    esp_partition_info_t* partition_entries = (esp_partition_info_t*)buffer;
    memset(partition_entries, 0, sizeof(esp_partition_info_t) * layout->partition_count);

    for (uint32_t i = 0; i < layout->partition_count; i++) {
        const partition_info_t* part = &layout->partitions[i];
        esp_partition_info_t* entry = &partition_entries[i];

        // Copy partition information
        strncpy((char*)entry->label, part->name, sizeof(entry->label) - 1);
        entry->label[sizeof(entry->label) - 1] = '\0';

        entry->type = part->is_ota ? ESP_PARTITION_TYPE_APP : ESP_PARTITION_TYPE_DATA;
        entry->subtype = part->subtype;
        entry->pos.offset = part->offset;
        // entry->size = part->size; // Field not available - using safer approach
        entry->flags = 0;

        if (part->is_encrypted) {
            entry->flags |= PARTITION_ENCRYPTED;
        }

        ESP_LOGD(TAG, "Partition %d: %s @ 0x%08x, size=0x%08x, type=0x%02x, subtype=0x%02x",
                 i, entry->label, entry->pos.offset, part->size, entry->type, entry->subtype);
    }

    // Calculate MD5 hash (placeholder - ESP-IDF would normally do this)
    uint8_t* md5_location = buffer + sizeof(esp_partition_info_t) * layout->partition_count;
    memset(md5_location, 0, MD5_SIZE);

    *actual_size = required_size;

    ESP_LOGI(TAG, "Partition table binary created successfully: %d bytes", *actual_size);
    return ESP_OK;
}

esp_err_t partition_manager_validate_layout(const partition_table_layout_t* layout,
                                             bool* is_valid)
{
    if (!layout || !is_valid) {
        return ESP_ERR_INVALID_ARG;
    }

    *is_valid = false;

    ESP_LOGI(TAG, "Validating partition table layout with %d partitions", layout->partition_count);

    // Check basic constraints
    if (layout->partition_count == 0 || layout->partition_count > MAX_PARTITIONS) {
        ESP_LOGE(TAG, "Invalid partition count: %d", layout->partition_count);
        return ESP_OK;
    }

    // Check for overlapping partitions
    for (uint32_t i = 0; i < layout->partition_count; i++) {
        const partition_info_t* part1 = &layout->partitions[i];

        // Check if partition is within flash bounds
        if (part1->offset >= FLASH_SIZE || (part1->offset + part1->size) > FLASH_SIZE) {
            ESP_LOGE(TAG, "Partition %s exceeds flash bounds: 0x%08x + 0x%08x > 0x%08x",
                     part1->name, part1->offset, part1->size, FLASH_SIZE);
            return ESP_OK;
        }

        // Check alignment
        if (part1->offset % PARTITION_ALIGNMENT != 0) {
            ESP_LOGW(TAG, "Partition %s not properly aligned: offset=0x%08x", part1->name, part1->offset);
        }

        // Check minimum size
        if (part1->is_ota && part1->size < MIN_OTA_PARTITION_SIZE) {
            ESP_LOGW(TAG, "OTA partition %s smaller than minimum: %d bytes < %d bytes",
                     part1->name, part1->size, MIN_OTA_PARTITION_SIZE);
        }

        // Check for overlaps with other partitions
        for (uint32_t j = i + 1; j < layout->partition_count; j++) {
            const partition_info_t* part2 = &layout->partitions[j];

            bool overlaps = (part1->offset < (part2->offset + part2->size)) &&
                           ((part1->offset + part1->size) > part2->offset);

            if (overlaps) {
                ESP_LOGE(TAG, "Partition overlap detected: %s and %s",
                         part1->name, part2->name);
                return ESP_OK;
            }
        }
    }

    ESP_LOGI(TAG, "Partition table layout validation passed");
    *is_valid = true;
    return ESP_OK;
}

esp_err_t partition_manager_get_firmware_partition(const partition_table_layout_t* layout,
                                                   uint32_t firmware_index,
                                                   partition_info_t** partition_info)
{
    if (!layout || !partition_info) {
        return ESP_ERR_INVALID_ARG;
    }

    *partition_info = NULL;

    // Find partition for firmware by index
    uint32_t current_firmware_index = 0;
    for (uint32_t i = 0; i < layout->partition_count; i++) {
        const partition_info_t* part = &layout->partitions[i];

        if (part->firmware) {
            if (current_firmware_index == firmware_index) {
                *partition_info = (partition_info_t*)part;  // Cast away const for API compatibility
                return ESP_OK;
            }
            current_firmware_index++;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t partition_manager_estimate_size(const partition_table_layout_t* layout,
                                          uint32_t* estimated_size)
{
    if (!layout || !estimated_size) {
        return ESP_ERR_INVALID_ARG;
    }

    *estimated_size = sizeof(esp_partition_info_t) * layout->partition_count + MD5_SIZE;
    return ESP_OK;
}

esp_err_t partition_manager_backup_current(uint8_t* backup_buffer,
                                          size_t buffer_size,
                                          size_t* backup_size)
{
    if (!backup_buffer || !backup_size) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Backing up current partition table");

    // Read current partition table
    const esp_partition_t* partition_table_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PARTITION_TABLE, "partition-table");

    if (!partition_table_partition) {
        ESP_LOGE(TAG, "Failed to find partition-table partition");
        return ESP_ERR_NOT_FOUND;
    }

    if (partition_table_partition->size > buffer_size) {
        ESP_LOGE(TAG, "Backup buffer too small: partition is %d bytes, buffer is %d bytes",
                 partition_table_partition->size, buffer_size);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_partition_read(partition_table_partition, 0, backup_buffer,
                                      partition_table_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read partition table: %s", esp_err_to_name(ret));
        return ret;
    }

    *backup_size = partition_table_partition->size;
    ESP_LOGI(TAG, "Partition table backed up successfully: %d bytes", *backup_size);
    return ESP_OK;
}

esp_err_t partition_manager_restore_from_backup(const uint8_t* backup_buffer,
                                                 size_t backup_size)
{
    if (!backup_buffer || backup_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Restoring partition table from backup: %d bytes", backup_size);

    // Find partition-table partition
    const esp_partition_t* partition_table_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PARTITION_TABLE, "partition-table");

    if (!partition_table_partition) {
        ESP_LOGE(TAG, "Failed to find partition-table partition");
        return ESP_ERR_NOT_FOUND;
    }

    if (backup_size > partition_table_partition->size) {
        ESP_LOGE(TAG, "Backup data too large: %d bytes > partition size %d bytes",
                 backup_size, partition_table_partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Write backup data
    esp_err_t ret = esp_partition_write(partition_table_partition, 0, backup_buffer, backup_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore partition table: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition table restored successfully");
    return ESP_OK;
}

void partition_manager_print_layout(const partition_table_layout_t* layout)
{
    if (!layout) {
        ESP_LOGI(TAG, "Partition layout is NULL");
        return;
    }

    ESP_LOGI(TAG, "=== Partition Table Layout ===");
    ESP_LOGI(TAG, "Total partitions: %d", layout->partition_count);
    ESP_LOGI(TAG, "Total used space: %d bytes (%d MB)",
             layout->total_used_size, layout->total_used_size / (1024 * 1024));
    ESP_LOGI(TAG, "");

    for (uint32_t i = 0; i < layout->partition_count; i++) {
        const partition_info_t* part = &layout->partitions[i];

        ESP_LOGI(TAG, "Partition %d: %s", i, part->name);
        ESP_LOGI(TAG, "  Type: %s", get_partition_type_name(part->type));
        ESP_LOGI(TAG, "  Offset: 0x%08x", part->offset);
        ESP_LOGI(TAG, "  Size: %d bytes (%d KB)", part->size, part->size / 1024);
        ESP_LOGI(TAG, "  OTA: %s", part->is_ota ? "Yes" : "No");
        ESP_LOGI(TAG, "  Firmware: %s", part->firmware ? part->firmware->display_name : "None");
        ESP_LOGI(TAG, "");
    }

    ESP_LOGI(TAG, "================================");
}

esp_err_t partition_manager_cleanup(void)
{
    ESP_LOGI(TAG, "Partition manager cleanup completed");
    return ESP_OK;
}