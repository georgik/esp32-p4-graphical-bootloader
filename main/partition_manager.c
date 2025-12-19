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
#include "esp_flash.h"
#include "mbedtls/md5.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <arpa/inet.h>  // For htonl function

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

// ESP32-P4 System partition definitions - from esp32-image-composer-rs
// Note: factory_app is NOT included here because it's an OTA partition that should be managed
static const partition_info_t system_partitions[] = {
    {"bootloader",     PARTITION_TYPE_BOOTLOADER,      0,                     BOOTLOADER_OFFSET,      BOOTLOADER_SIZE,      0,      false, true,  false, NULL},
    {"partition-table",PARTITION_TYPE_PARTITION_TABLE, 0,                     PARTITION_TABLE_OFFSET,  PARTITION_TABLE_SIZE,  0,      false, true,  false, NULL},
    {"nvs",           PARTITION_TYPE_NVS,             PARTITION_SUBTYPE_DATA_NVS,  NVS_OFFSET,             FIRMWARE_REGISTRY_SIZE, 0,      false, false, false, NULL},
    {"firmware-reg",  PARTITION_TYPE_FIRMWARE_REGISTRY,0,                     FIRMWARE_REGISTRY_OFFSET, FIRMWARE_REGISTRY_SIZE, 0,      false, false, false, NULL},
    {"ota_data",      PARTITION_TYPE_OTA_DATA,       ESP_PARTITION_SUBTYPE_DATA_OTA, OTA_DATA_OFFSET,        OTA_DATA_SIZE,        0,      false, false, false, NULL},
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
        case PARTITION_TYPE_OTA_DATA: return "OTA Data";
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

    // Validate ESP32-P4 system partition definitions
    // Note: ESP32-P4 uses specific offsets with intentional gaps, not contiguous layout
    for (uint32_t i = 0; i < system_partition_count; i++) {
        const partition_info_t* part = &system_partitions[i];

        // Validate ESP32-P4 alignment requirements
        uint32_t required_alignment = (part->type == PARTITION_TYPE_FACTORY_APP || part->is_ota) ?
                                     OTA_ALIGNMENT : DATA_ALIGNMENT;

        if (part->offset % required_alignment != 0) {
            ESP_LOGW(TAG, "System partition %s not properly aligned: offset=0x%08x, requires %d byte alignment",
                     part->name, part->offset, required_alignment);
        }

        ESP_LOGD(TAG, "System partition %s: offset=0x%08x, size=%d bytes",
                 part->name, part->offset, part->size);
    }

    ESP_LOGI(TAG, "Partition manager initialized successfully");
    ESP_LOGI(TAG, "Loaded %d ESP32-P4 system partitions", system_partition_count);

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
        requests[i].preferred_size = align_up(requests[i].min_size, OTA_ALIGNMENT);
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

    // Align current_offset to 64KB boundary for OTA partitions (ESP32-P4 requirement)
    current_offset = align_up(current_offset, OTA_ALIGNMENT);

    uint32_t available_space = FLASH_SIZE - current_offset;

    ESP_LOGI(TAG, "Starting allocation at offset 0x%08x (aligned from 0x%08x), available space: %d bytes",
             current_offset, layout->total_used_size, available_space);

    // Allocate partitions in order of preference
    uint32_t allocated_partitions = 0;
    uint32_t ota_slot_index = 0;

    for (uint32_t i = 0; i < request_count && layout->partition_count < MAX_PARTITIONS; i++) {
        const partition_allocation_request_t* req = &requests[i];

        // Calculate required size with ESP32-P4 OTA alignment
        uint32_t required_size = align_up(req->preferred_size, OTA_ALIGNMENT);

        // Check if we have enough space, if not, adjust size to fit
        uint32_t available_space = FLASH_SIZE - current_offset;
        if (current_offset + required_size > FLASH_SIZE) {
            uint32_t original_size = required_size;
            required_size = available_space;

            ESP_LOGW(TAG, "Firmware %s too large for available space", req->firmware->display_name);
            ESP_LOGW(TAG, "Original size: %d bytes, available: %d bytes, truncating to %d bytes",
                     original_size, available_space, required_size);
        }

        // Create partition
        partition_info_t* partition = &layout->partitions[layout->partition_count];
        memset(partition, 0, sizeof(partition_info_t));

        // Initialize truncated size (set if we're truncating)
        partition->truncated_size = (current_offset + required_size > FLASH_SIZE) ? required_size : 0;

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
        layout->total_used_size = current_offset;  // Keep total_used_size aligned
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

    // Calculate required size: partition entries + 1 MD5 entry
    size_t required_size = sizeof(esp_partition_info_t) * (layout->partition_count + 1);
    if (buffer_size < required_size) {
        ESP_LOGE(TAG, "Buffer too small: need %d bytes, have %d bytes", required_size, buffer_size);
        return ESP_ERR_NO_MEM;
    }

    // Initialize buffer with flash default value (0xFF)
    memset(buffer, 0xFF, buffer_size);

    // Create partition entries
    esp_partition_info_t* partition_entries = (esp_partition_info_t*)buffer;
    memset(partition_entries, 0, sizeof(esp_partition_info_t) * (layout->partition_count + 1));

    for (uint32_t i = 0; i < layout->partition_count; i++) {
        const partition_info_t* part = &layout->partitions[i];
        esp_partition_info_t* entry = &partition_entries[i];

        // CRITICAL: Set magic number for ESP32 partition table validation
        entry->magic = ESP_PARTITION_MAGIC; // 0x50AA

        // Copy partition information
        strncpy((char*)entry->label, part->name, sizeof(entry->label) - 1);
        entry->label[sizeof(entry->label) - 1] = '\0';

        entry->type = part->is_ota ? ESP_PARTITION_TYPE_APP : ESP_PARTITION_TYPE_DATA;
        entry->subtype = part->subtype;
        entry->pos.offset = part->offset;  // ESP32 is little-endian, no conversion needed

        // DEBUG: Log size before assignment
        ESP_LOGI(TAG, "DEBUG: Assigning size - part->size=%d (0x%08X), part->name='%s'",
                 part->size, part->size, part->name);

        entry->pos.size = part->size;  // ESP32 is little-endian, no conversion needed

        // DEBUG: Log size after assignment
        ESP_LOGI(TAG, "DEBUG: Assigned size - entry->pos.size=%d (0x%08X)",
                 entry->pos.size, entry->pos.size);

        entry->flags = 0;

        if (part->is_encrypted) {
            entry->flags |= PARTITION_ENCRYPTED;
        }

        ESP_LOGI(TAG, "Partition %d: %s @ 0x%08x, size=0x%08x, type=0x%02x, subtype=0x%02x, magic=0x%04X",
                 i, entry->label, entry->pos.offset, entry->pos.size, entry->type, entry->subtype, entry->magic);
    }

    // Create MD5 checksum entry with correct ESP-IDF format from esp-idf-part project
    esp_partition_info_t* md5_entry = &partition_entries[layout->partition_count];
    memset(md5_entry, 0xFF, sizeof(esp_partition_info_t)); // Start with 0xFF

    // CRITICAL: MD5 entry structure from esp-idf-part project:
    // - First 16 bytes: 0xEB, 0xEB, followed by 0xFF * 14
    // - Next 16 bytes: actual MD5 hash of all partition entries
    md5_entry->magic = ESP_PARTITION_MAGIC_MD5; // 0xEBEB for MD5 entry
    md5_entry->type = 0xFF;
    md5_entry->subtype = 0xFF;
    md5_entry->pos.offset = 0xFFFFFFFF;
    md5_entry->pos.size = 0xFFFFFFFF;
    md5_entry->flags = 0xFFFFFFFF;

    // MD5 magic pattern from esp-idf-part: {0xEB, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
    uint8_t md5_magic_pattern[16] = {
        0xEB, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    memcpy(md5_entry->label, md5_magic_pattern, sizeof(md5_magic_pattern));

    // Calculate proper MD5 checksum of all partition entries (excluding MD5 entry itself)
    mbedtls_md5_context md5_ctx;
    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);

    // Hash all partition entries (32 bytes each, layout->partition_count entries)
    mbedtls_md5_update(&md5_ctx, (const unsigned char*)partition_entries,
                      layout->partition_count * sizeof(esp_partition_info_t));

    unsigned char md5_hash[16];
    mbedtls_md5_finish(&md5_ctx, md5_hash);
    mbedtls_md5_free(&md5_ctx);

    // CRITICAL: Store MD5 hash in label + 16 (after the magic pattern)
    // This matches the esp-idf-part format: first 16 bytes magic, next 16 bytes hash
    memcpy(md5_entry->label + 16, md5_hash, 16);

    ESP_LOGI(TAG, "MD5 entry created with magic 0x%04X and proper checksum", md5_entry->magic);
    ESP_LOG_BUFFER_HEX(TAG, md5_hash, 16);

    *actual_size = required_size;

    ESP_LOGI(TAG, "Partition table binary created successfully: %d bytes (%d partitions + 1 MD5 entry)",
             *actual_size, layout->partition_count);

    // Verify the first entry has correct magic number
    ESP_LOGI(TAG, "Verification: First partition magic = 0x%04X (expected: 0x%04X)",
             partition_entries[0].magic, ESP_PARTITION_MAGIC);

    // Log complete partition table for validation
    ESP_LOGI(TAG, "=== GENERATED PARTITION TABLE DUMP ===");
    for (uint32_t i = 0; i <= layout->partition_count; i++) {
        esp_partition_info_t* entry = &partition_entries[i];
        ESP_LOGI(TAG, "Entry %d: magic=0x%04X, type=0x%02X, subtype=0x%02X, offset=0x%08X, size=0x%08X, label='%.16s'",
                 i, entry->magic, entry->type, entry->subtype, entry->pos.offset, entry->pos.size, entry->label);
    }

    // Validate that the head matches expected structure until OTA
    ESP_LOGI(TAG, "=== PARTITION TABLE VALIDATION ===");
    const char* expected_order[] = {"factory_app", "nvs", "bootdata", "bootloader_confi", "ota_0"};
    bool order_valid = true;

    for (uint32_t i = 0; i < layout->partition_count && i < 5; i++) {
        const partition_info_t* part = &layout->partitions[i];
        ESP_LOGI(TAG, "Position %d: expected='%s', actual='%s', offset=0x%08X, size=0x%08X",
                 i, expected_order[i], part->name, part->offset, part->size);

        if (strcmp(part->name, expected_order[i]) != 0) {
            ESP_LOGW(TAG, "ORDER MISMATCH at position %d: expected '%s', found '%s'",
                     i, expected_order[i], part->name);
            order_valid = false;
        }
    }

    if (order_valid) {
        ESP_LOGI(TAG, "✓ Partition table order validation PASSED");
    } else {
        ESP_LOGW(TAG, "✗ Partition table order validation FAILED");
    }
    ESP_LOGI(TAG, "=== END VALIDATION ===");

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

        // Check ESP32-P4 alignment
        uint32_t required_alignment = part1->is_ota ? OTA_ALIGNMENT : DATA_ALIGNMENT;
        if (part1->offset % required_alignment != 0) {
            ESP_LOGW(TAG, "Partition %s not properly aligned: offset=0x%08x, requires %d byte alignment",
                     part1->name, part1->offset, required_alignment);
        }

        // Check minimum size
        if (part1->is_ota && part1->size < MIN_OTA_PARTITION_SIZE) {
            ESP_LOGW(TAG, "OTA partition %s smaller than minimum: %d bytes < %d bytes",
                     part1->name, part1->size, MIN_OTA_PARTITION_SIZE);
        }

        // Only check overlaps involving OTA partitions
        // System partitions (bootloader, NVS, etc.) are known to be correct
        if (part1->is_ota) {
            for (uint32_t j = 0; j < layout->partition_count; j++) {
                if (i == j) continue;  // Skip self

                const partition_info_t* part2 = &layout->partitions[j];

                bool overlaps = (part1->offset < (part2->offset + part2->size)) &&
                               ((part1->offset + part1->size) > part2->offset);

                if (overlaps) {
                    ESP_LOGE(TAG, "OTA partition %s overlaps with %s: ota[0x%08x-0x%08x] vs %s[0x%08x-0x%08x]",
                             part1->name, part2->name,
                             part1->offset, part1->offset + part1->size,
                             part2->name, part2->offset, part2->offset + part2->size);
                    *is_valid = false;
                    return ESP_OK;
                }
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

    // Try to read current partition table (for backup)
    // Note: This might fail on first run or if no partition table partition exists
    const esp_partition_t* partition_table_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PARTITION_TABLE, "partition-table");

    if (!partition_table_partition) {
        ESP_LOGW(TAG, "No existing partition-table partition found - this is normal for first run");
        ESP_LOGI(TAG, "Skipping backup - proceeding with fresh partition table creation");
        *backup_size = 0;
        return ESP_OK;  // Success - no backup needed for first run
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

esp_err_t partition_manager_read_existing_table(partition_table_layout_t* layout)
{
    if (!layout) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Reading existing partition table from flash at offset 0x%08x", PARTITION_TABLE_OFFSET);

    // Allocate buffer on heap to avoid stack overflow
    uint8_t* raw_table = malloc(4096);  // One flash sector
    if (raw_table == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for partition table reading");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_flash_read(NULL, raw_table, PARTITION_TABLE_OFFSET, 4096);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read partition table from flash: %s", esp_err_to_name(ret));
        free(raw_table);
        return ret;
    }

    ESP_LOGI(TAG, "Successfully read partition table from flash");

    // Initialize layout
    memset(layout, 0, sizeof(partition_table_layout_t));
    uint32_t partition_count = 0;
    uint32_t total_used_size = 0;

    // Parse partition table entries
    esp_partition_info_t* entries = (esp_partition_info_t*)raw_table;

    ESP_LOGI(TAG, "=== EXISTING PARTITION TABLE DUMP ===");
    for (uint32_t i = 0; i < MAX_PARTITIONS; i++) {
        esp_partition_info_t* entry = &entries[i];

        // Check for MD5 entry (magic 0xEBEB) - stop here
        if (entry->magic == ESP_PARTITION_MAGIC_MD5) {
            ESP_LOGI(TAG, "MD5 entry found at position %d", i);
            break;
        }

        // Check for valid partition entry
        if (entry->magic != ESP_PARTITION_MAGIC) {
            ESP_LOGI(TAG, "Invalid magic 0x%04X at position %d, stopping", entry->magic, i);
            break;
        }

        // Log detailed partition information
        ESP_LOGI(TAG, "Partition %d: magic=0x%04X, type=0x%02X, subtype=0x%02X, offset=0x%08X, size=0x%08X, label='%.16s'",
                 i, entry->magic, entry->type, entry->subtype, entry->pos.offset, entry->pos.size, entry->label);

        // Convert to our partition_info_t structure
        if (partition_count < MAX_PARTITIONS) {
            partition_info_t* part = &layout->partitions[partition_count];

            // Copy name (truncate if necessary)
            strncpy(part->name, (char*)entry->label, sizeof(part->name) - 1);
            part->name[sizeof(part->name) - 1] = '\0';

            // Determine partition type and OTA status
            if (entry->type == ESP_PARTITION_TYPE_APP) {
                part->type = PARTITION_TYPE_FACTORY_APP;  // Default

                // Check specific OTA subtype - only OTA slots are considered OTA
                if (entry->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
                    part->type = PARTITION_TYPE_FACTORY_APP;
                    part->is_ota = false;  // Factory app is NOT OTA - should be preserved
                } else if (entry->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
                    part->type = PARTITION_TYPE_OTA_0;
                    part->is_ota = true;
                } else if (entry->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
                    part->type = PARTITION_TYPE_OTA_1;
                    part->is_ota = true;
                } else if (entry->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_2) {
                    part->type = PARTITION_TYPE_OTA_2;
                    part->is_ota = true;
                } else {
                    // Unknown APP subtype - check if it's factory_app by name
                    if (strcmp(part->name, "factory_app") == 0) {
                        part->type = PARTITION_TYPE_FACTORY_APP;
                        part->is_ota = false;  // factory_app is NOT OTA
                    } else {
                        part->is_ota = true;  // Other APP partitions are OTA
                    }
                }
            } else if (entry->type == ESP_PARTITION_TYPE_DATA) {
                part->is_ota = false;
                // Map data subtypes
                switch (entry->subtype) {
                    case ESP_PARTITION_SUBTYPE_DATA_NVS:
                        part->type = PARTITION_TYPE_NVS;
                        break;
                    case ESP_PARTITION_SUBTYPE_DATA_OTA:
                        part->type = PARTITION_TYPE_OTA_DATA;
                        break;
                    default:
                        part->type = PARTITION_TYPE_NVS;  // Default
                        break;
                }
            } else {
                part->is_ota = false;
                part->type = PARTITION_TYPE_NVS;  // Default unknown type
            }

            part->subtype = entry->subtype;  // Preserve original ESP32 subtype
            part->offset = entry->pos.offset;
            part->size = entry->pos.size;
            part->is_encrypted = (entry->flags & PARTITION_ENCRYPTED) != 0;
            part->firmware = NULL;

            ESP_LOGI(TAG, "Loaded partition %s: type=%d, is_ota=%d, offset=0x%08x, size=%d",
                     part->name, part->type, part->is_ota, part->offset, part->size);

            total_used_size += part->size;
            partition_count++;
        }
    }

    layout->partition_count = partition_count;
    layout->total_used_size = total_used_size;

    ESP_LOGI(TAG, "=== END PARTITION TABLE DUMP ===");
    ESP_LOGI(TAG, "Successfully loaded %d partitions from flash", partition_count);
    ESP_LOGI(TAG, "Total used space: %d bytes (%.2f MB)",
             total_used_size, (float)total_used_size / (1024 * 1024));

    // Clean up heap allocation
    free(raw_table);
    return ESP_OK;
}

esp_err_t partition_manager_generate_ota_only_layout(firmware_selector_t* selector,
                                                    partition_table_layout_t* layout)
{
    if (!selector || !layout) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Generating OTA-only partition layout for %d selected firmwares", selector->selected_count);

    // Step 1: Read existing partition table
    esp_err_t ret = partition_manager_read_existing_table(layout);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read existing partition table: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Remove all existing OTA partitions from layout
    uint32_t write_index = 0;
    uint32_t removed_ota_count = 0;

    for (uint32_t read_index = 0; read_index < layout->partition_count; read_index++) {
        const partition_info_t* part = &layout->partitions[read_index];

        // Keep all non-OTA partitions (factory_app, nvs, bootdata, bootloader_confi)
        if (!part->is_ota) {
            if (write_index != read_index) {
                layout->partitions[write_index] = layout->partitions[read_index];
            }
            write_index++;
        } else {
            removed_ota_count++;
            ESP_LOGI(TAG, "Removing existing OTA partition: %s (offset=0x%08x, size=%d)",
                     part->name, part->offset, part->size);
        }
    }

    layout->partition_count = write_index;
    ESP_LOGI(TAG, "Removed %d existing OTA partitions", removed_ota_count);

    ESP_LOGI(TAG, "=== PRESERVED PARTITIONS (NON-OTA) ===");
    for (uint32_t i = 0; i < layout->partition_count; i++) {
        const partition_info_t* part = &layout->partitions[i];
        ESP_LOGI(TAG, "Preserved %d: %s @ 0x%08x, size=%d, type=%d, is_ota=%d",
                 i, part->name, part->offset, part->size, part->type, part->is_ota);
    }
    ESP_LOGI(TAG, "=== END PRESERVED PARTITIONS ===");

    // Step 3: Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    ret = firmware_selector_get_selected(selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (ret != ESP_OK || selected_count == 0) {
        ESP_LOGE(TAG, "Failed to get selected firmwares: %s", esp_err_to_name(ret));
        return ESP_ERR_INVALID_ARG;
    }

    if (selected_count == 0) {
        ESP_LOGI(TAG, "No firmwares selected, keeping existing layout");
        return ESP_OK;
    }

    // Step 4: Start OTA partitions at fixed offset to match existing layout
    // ESP32-P4 OTA partitions start at 0x330000 (after bootloader_confi)
    const uint32_t OTA_START_OFFSET = 0x330000;
    uint32_t current_offset = OTA_START_OFFSET;

    ESP_LOGI(TAG, "Starting OTA allocation at offset 0x%08x (after all existing partitions)", current_offset);
    ESP_LOGI(TAG, "Available space: %d bytes", FLASH_SIZE - current_offset);

    // Step 5: Add new OTA partitions with dynamic size calculation
    for (uint32_t i = 0; i < selected_count && layout->partition_count < MAX_PARTITIONS; i++) {
        const firmware_info_t* firmware = selected_firmware[i];

        // Calculate required size with padding and alignment
        uint32_t required_size = firmware->size + 0x1000;  // Add 4KB padding for safety margin
        required_size = align_up(required_size, OTA_ALIGNMENT);

        // Ensure minimum OTA size (64KB for ESP32)
        if (required_size < MIN_OTA_PARTITION_SIZE) {
            required_size = MIN_OTA_PARTITION_SIZE;
        }

        // Calculate remaining available space for OTA partitions
        uint32_t ota_start_offset = 0x00330000;  // First OTA partition offset (from working partition table)
        uint32_t available_ota_space = FLASH_SIZE - ota_start_offset;

        // For multiple firmware, divide available space proportionally
        if (selected_count > 1) {
            // Calculate total firmware size to determine proportional allocation
            uint32_t total_firmware_size = 0;
            for (uint32_t j = 0; j < selected_count; j++) {
                uint32_t fw_size = selected_firmware[j]->size + 0x1000;
                fw_size = align_up(fw_size, OTA_ALIGNMENT);
                if (fw_size < MIN_OTA_PARTITION_SIZE) {
                    fw_size = MIN_OTA_PARTITION_SIZE;
                }
                total_firmware_size += fw_size;
            }

            // Proportional allocation with minimum space
            required_size = (firmware->size * available_ota_space) / total_firmware_size;
            required_size = align_up(required_size, OTA_ALIGNMENT);

            // Ensure minimum size and firmware fits
            uint32_t fw_aligned_size = firmware->size + 0x1000;
            fw_aligned_size = align_up(fw_aligned_size, OTA_ALIGNMENT);
            if (required_size < fw_aligned_size) {
                required_size = fw_aligned_size;
            }
        } else {
            // Single firmware: use all available OTA space (firmware size + padding + remaining space)
            uint32_t fw_aligned_size = firmware->size + 0x1000;
            fw_aligned_size = align_up(fw_aligned_size, OTA_ALIGNMENT);

            // Use firmware size + all remaining OTA space
            required_size = fw_aligned_size;
        }

        ESP_LOGI(TAG, "Dynamic OTA sizing for %s: firmware=%d, required=%d, available_ota=%d",
                 firmware->display_name, firmware->size, required_size, available_ota_space);

        // Check if we have enough space, if not, adjust size to fit
        uint32_t available_space = FLASH_SIZE - current_offset;
        if (current_offset + required_size > FLASH_SIZE) {
            uint32_t original_size = required_size;
            required_size = available_space;

            ESP_LOGW(TAG, "Firmware %s too large for available space", firmware->display_name);
            ESP_LOGW(TAG, "Original size: %d bytes, available: %d bytes, truncating to %d bytes",
                     original_size, available_space, required_size);
        }

        // Create new OTA partition
        partition_info_t* ota_partition = &layout->partitions[layout->partition_count];
        memset(ota_partition, 0, sizeof(partition_info_t));

        // Initialize truncated size (set if we're truncating)
        ota_partition->truncated_size = (current_offset + required_size > FLASH_SIZE) ? required_size : 0;

        // Set partition name and properties
        snprintf(ota_partition->name, sizeof(ota_partition->name), "ota_%" PRIu32, i);
        ota_partition->type = ESP_PARTITION_TYPE_APP;

        // Set OTA subtype
        if (i == 0) {
            ota_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
        } else if (i == 1) {
            ota_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
        } else if (i == 2) {
            ota_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_2;
        } else {
            ota_partition->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0 + i;
        }

        ota_partition->offset = current_offset;
        ota_partition->size = required_size;
        ota_partition->is_ota = true;
        ota_partition->is_encrypted = false;
        ota_partition->firmware = (void*)firmware;  // Store firmware reference

        ESP_LOGI(TAG, "Created OTA partition %s for %s: offset=0x%08x, size=%d bytes (0x%08X) (calc: %d + padding)",
                 ota_partition->name, firmware->display_name, ota_partition->offset, ota_partition->size, ota_partition->size, firmware->size);

        current_offset += required_size;
        layout->partition_count++;
    }

    // Update total used size
    layout->total_used_size = current_offset;

    ESP_LOGI(TAG, "OTA-only partition layout generated successfully:");
    ESP_LOGI(TAG, "  Total partitions: %d", layout->partition_count);
    ESP_LOGI(TAG, "  Total used space: %d bytes (%.2f MB)", layout->total_used_size,
             (float)layout->total_used_size / (1024 * 1024));
    ESP_LOGI(TAG, "  New OTA partitions: %d", selected_count);

    return ESP_OK;
}

esp_err_t partition_manager_cleanup(void)
{
    ESP_LOGI(TAG, "Partition manager cleanup completed");
    return ESP_OK;
}