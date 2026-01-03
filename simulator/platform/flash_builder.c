/**
 * @file flash_builder.c
 * @brief Flash image builder implementation
 */

#ifdef __SIMULATOR_BUILD__

#include "flash_builder.h"
#include "crc32.h"
#include "esp_log_mock.h"
#include "partition_table.h"
#include "firmware_storage_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

static const char* TAG = "flash_builder";

int flash_builder_exists(const char* flash_path) {
    if (!flash_path) {
        return 0;
    }

    struct stat st;
    if (stat(flash_path, &st) == 0) {
        return 1;
    }
    return 0;
}

long flash_builder_get_file_size(const char* filepath) {
    if (!filepath) {
        ESP_LOGE(TAG, "NULL filepath");
        return -1;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "Failed to stat file: %s", filepath);
        return -1;
    }

    return st.st_size;
}

ssize_t flash_builder_read_file(const char* filepath, void* buffer, size_t max_size) {
    if (!filepath || !buffer) {
        ESP_LOGE(TAG, "NULL filepath or buffer");
        return -1;
    }

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to get file size: %s", filepath);
        fclose(f);
        return -1;
    }

    // Check buffer size
    size_t read_size = (size_t)file_size;
    if (read_size > max_size) {
        ESP_LOGW(TAG, "File %s (size=%ld) exceeds buffer size=%zu, truncating",
                 filepath, file_size, max_size);
        read_size = max_size;
    }

    // Read file
    size_t bytes_read = fread(buffer, 1, read_size, f);
    fclose(f);

    if (bytes_read != read_size) {
        ESP_LOGE(TAG, "Failed to read complete file: %s (read=%zu, expected=%zu)",
                 filepath, bytes_read, read_size);
        return -1;
    }

    ESP_LOGI(TAG, "Read %zu bytes from %s", bytes_read, filepath);
    return (ssize_t)bytes_read;
}

/**
 * @brief Generate partition table with OTA partitions
 *
 * @param firmware_sizes Array of firmware sizes
 * @param firmware_count Number of firmwares
 * @param partition_table_out Output buffer for partition table (must be at least 6KB)
 * @param partition_table_size Output size of partition table
 * @param firmware_storage_offset_out Output offset for firmware storage
 * @return ESP_OK on success
 */
static esp_err_t generate_partition_table(
    const size_t* firmware_sizes,
    int firmware_count,
    uint8_t** partition_table_out,
    size_t* partition_table_size,
    uint32_t* firmware_storage_offset_out)
{
    ESP_LOGI(TAG, "Generating partition table for %d firmwares...", firmware_count);

    // Calculate partition layout
    // factory_app: 0x20000 (fixed)
    // nvs, bootdata, bootloader_config: after factory_app (fixed offsets from partitions.csv)
    // OTA partitions: after bootloader_config

    uint32_t current_offset = 0x20000;  // factory_app offset
    uint32_t factory_app_size = 1 * 1024 * 1024;  // 1MB from partitions.csv

    // Align to next 64KB boundary after factory app
    current_offset = 0x120000;  // nvs offset (from partitions.csv)

    // Add NVS partition (32K)
    current_offset += 32 * 1024;  // nvs size

    // Add bootdata partition (12K)
    uint32_t bootdata_offset = current_offset;
    uint32_t bootdata_size = 12 * 1024;
    current_offset += bootdata_size;

    // Add bootloader_config partition (64KB - reasonable size for NVS storage)
    uint32_t bootloader_config_offset = current_offset;
    uint32_t bootloader_config_size = 64 * 1024;  // 64KB (reduced from 2MB)
    current_offset += bootloader_config_size;

    // Place firmware storage metadata at fixed offset (before OTA partitions)
    // This allows the bootloader to find it without scanning
    // Offset is defined in firmware_storage_config.h for DRY principle
    uint32_t firmware_storage_offset = FIRMWARE_STORAGE_OFFSET;

    // Now align to 64KB boundary for OTA partitions (after firmware storage metadata)
    current_offset = (current_offset + 0xFFFF) & ~0xFFFF;  // Align to 64KB

    // Ensure we leave room for firmware storage metadata
    if (current_offset < 0x140000) {
        current_offset = 0x140000;  // Start OTA partitions at 0x140000
    }
    uint32_t ota_start_offset = current_offset;

    // Allocate partition table buffer (max 6KB for 95 entries)
    size_t pt_size = 6 * 1024;
    uint8_t* pt_buffer = (uint8_t*)calloc(1, pt_size);
    if (!pt_buffer) {
        ESP_LOGE(TAG, "Failed to allocate partition table buffer");
        return ESP_ERR_NO_MEM;
    }

    int entry_count = 0;
    partition_entry_t* entries = (partition_entry_t*)pt_buffer;

    // 1. factory_app partition
    entries[entry_count].magic = PARTITION_MAGIC;
    entries[entry_count].type = PART_TYPE_APP;
    entries[entry_count].subtype = PART_SUBTYPE_FACTORY;
    entries[entry_count].offset = 0x20000;
    entries[entry_count].size = factory_app_size;
    entries[entry_count].flags = 0;
    strncpy(entries[entry_count].name, "factory_app", 16);
    entry_count++;

    ESP_LOGI(TAG, "  [0] factory_app @ 0x%08X (%.2f MB)",
             0x20000, factory_app_size / (1024.0 * 1024.0));

    // 2. NVS partition
    entries[entry_count].magic = PARTITION_MAGIC;
    entries[entry_count].type = PART_TYPE_DATA;
    entries[entry_count].subtype = PART_SUBTYPE_NVS;
    entries[entry_count].offset = 0x120000;
    entries[entry_count].size = 32 * 1024;
    entries[entry_count].flags = 0;
    strncpy(entries[entry_count].name, "nvs", 16);
    entry_count++;

    ESP_LOGI(TAG, "  [1] nvs @ 0x%08X (32 KB)", 0x120000);

    // 3. bootdata partition
    entries[entry_count].magic = PARTITION_MAGIC;
    entries[entry_count].type = PART_TYPE_DATA;
    entries[entry_count].subtype = PART_SUBTYPE_NVS;
    entries[entry_count].offset = bootdata_offset;
    entries[entry_count].size = bootdata_size;
    entries[entry_count].flags = 0x1000;  // readonly flag
    strncpy(entries[entry_count].name, "bootdata", 16);
    entry_count++;

    ESP_LOGI(TAG, "  [2] bootdata @ 0x%08X (%.2f KB)",
             bootdata_offset, bootdata_size / 1024.0);

    // 4. bootloader_config partition
    entries[entry_count].magic = PARTITION_MAGIC;
    entries[entry_count].type = PART_TYPE_DATA;
    entries[entry_count].subtype = PART_SUBTYPE_SPIFFS;
    entries[entry_count].offset = bootloader_config_offset;
    entries[entry_count].size = bootloader_config_size;
    entries[entry_count].flags = 0;
    strncpy(entries[entry_count].name, "bootloader_config", 16);
    entry_count++;

    ESP_LOGI(TAG, "  [3] bootloader_config @ 0x%08X (%.2f MB)",
             bootloader_config_offset, bootloader_config_size / (1024.0 * 1024.0));

    // 5. OTA partitions (one per firmware)
    current_offset = ota_start_offset;
    for (int i = 0; i < firmware_count && i < 16; i++) {  // Max 16 OTA partitions
        // Calculate partition size (firmware size aligned to 64KB, no padding)
        size_t firmware_size = firmware_sizes[i];
        size_t partition_size = (firmware_size + 0xFFFF) & ~0xFFFF;  // Align to 64KB only

        // Minimum 1MB per OTA partition (ESP-IDF requirement)
        if (partition_size < 1 * 1024 * 1024) {
            partition_size = 1 * 1024 * 1024;
        }

        entries[entry_count].magic = PARTITION_MAGIC;
        entries[entry_count].type = PART_TYPE_APP;
        entries[entry_count].subtype = PART_SUBTYPE_OTA_0 + i;
        entries[entry_count].offset = current_offset;
        entries[entry_count].size = partition_size;
        entries[entry_count].flags = 0;
        snprintf(entries[entry_count].name, 16, "ota_%d", i);
        entry_count++;

        ESP_LOGI(TAG, "  [%d] ota_%d @ 0x%08X (%.2f MB, firmware: %.2f MB)",
                 entry_count - 1, i, current_offset,
                 partition_size / (1024.0 * 1024.0),
                 firmware_size / (1024.0 * 1024.0));

        current_offset += partition_size;
    }

    // Set output parameters (firmware_storage_offset is already fixed at 0x13C000)
    *partition_table_out = pt_buffer;
    *partition_table_size = entry_count * sizeof(partition_entry_t);
    *firmware_storage_offset_out = firmware_storage_offset;

    ESP_LOGI(TAG, "Partition table generated: %d entries, %zu bytes",
             entry_count, *partition_table_size);
    ESP_LOGI(TAG, "Firmware storage will be at: 0x%08X", firmware_storage_offset);

    return ESP_OK;
}

flash_builder_err_t flash_builder_validate(const char* flash_path) {
    if (!flash_path) {
        ESP_LOGE(TAG, "NULL flash_path");
        return FLASH_BUILDER_ERR_INVALID_ARGS;
    }

    // Check if file exists
    struct stat st;
    if (stat(flash_path, &st) != 0) {
        ESP_LOGE(TAG, "Flash image does not exist: %s", flash_path);
        return FLASH_BUILDER_ERR_MISSING_FILE;
    }

    // Check size
    if (st.st_size != SIMULATED_FLASH_SIZE) {
        ESP_LOGE(TAG, "Flash image has wrong size: %jd (expected %d)",
                 (intmax_t)st.st_size, SIMULATED_FLASH_SIZE);
        return FLASH_BUILDER_ERR_INVALID_ARGS;
    }

    ESP_LOGI(TAG, "Flash image valid: %s (%jd bytes)", flash_path, (intmax_t)st.st_size);
    return FLASH_BUILDER_OK;
}

flash_builder_err_t flash_builder_create(const char* output_path,
                                        const char* esp_idf_build_dir) {
    if (!output_path || !esp_idf_build_dir) {
        ESP_LOGE(TAG, "NULL output_path or esp_idf_build_dir");
        return FLASH_BUILDER_ERR_INVALID_ARGS;
    }

    ESP_LOGI(TAG, "Creating simulated flash image...");
    ESP_LOGI(TAG, "  Output: %s", output_path);
    ESP_LOGI(TAG, "  ESP-IDF build dir: %s", esp_idf_build_dir);

    // Check if output already exists
    if (flash_builder_exists(output_path)) {
        ESP_LOGW(TAG, "Flash image already exists: %s", output_path);
        return FLASH_BUILDER_ERR_FILE_EXISTS;
    }

    // Build paths to input files
    char bootloader_path[512];
    char partition_table_path[512];
    char app_path[512];

    snprintf(bootloader_path, sizeof(bootloader_path),
             "%sbootloader/bootloader.bin", esp_idf_build_dir);
    snprintf(partition_table_path, sizeof(partition_table_path),
             "%spartition_table/partition-table.bin", esp_idf_build_dir);
    snprintf(app_path, sizeof(app_path),
             "%sesp32_p4_graphical_bootloader.bin", esp_idf_build_dir);

    ESP_LOGI(TAG, "Input files:");
    ESP_LOGI(TAG, "  Bootloader:       %s", bootloader_path);
    ESP_LOGI(TAG, "  Partition table:  %s", partition_table_path);
    ESP_LOGI(TAG, "  Application:      %s", app_path);

    // Read input files
    uint8_t* bootloader_data = malloc(256 * 1024);  // 256KB max for bootloader
    uint8_t* partition_table_data = malloc(64 * 1024);  // 64KB max for partition table
    uint8_t* app_data = malloc(8 * 1024 * 1024);  // 8MB max for app

    if (!bootloader_data || !partition_table_data || !app_data) {
        ESP_LOGE(TAG, "Failed to allocate buffers for input files");
        free(bootloader_data);
        free(partition_table_data);
        free(app_data);
        return FLASH_BUILDER_ERR_NO_MEM;
    }

    ssize_t bootloader_size = flash_builder_read_file(bootloader_path, bootloader_data, 256 * 1024);
    ssize_t partition_table_size = flash_builder_read_file(partition_table_path, partition_table_data, 64 * 1024);
    ssize_t app_size = flash_builder_read_file(app_path, app_data, 8 * 1024 * 1024);

    if (bootloader_size < 0 || partition_table_size < 0 || app_size < 0) {
        ESP_LOGE(TAG, "Failed to read input files");
        free(bootloader_data);
        free(partition_table_data);
        free(app_data);
        return FLASH_BUILDER_ERR_MISSING_FILE;
    }

    ESP_LOGI(TAG, "Input file sizes:");
    ESP_LOGI(TAG, "  Bootloader:       %zd bytes", bootloader_size);
    ESP_LOGI(TAG, "  Partition table:  %zd bytes", partition_table_size);
    ESP_LOGI(TAG, "  Application:      %zd bytes", app_size);

    // Create flash image filled with zeros
    uint8_t* flash_image = calloc(1, SIMULATED_FLASH_SIZE);
    if (!flash_image) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for flash image", SIMULATED_FLASH_SIZE);
        free(bootloader_data);
        free(partition_table_data);
        free(app_data);
        return FLASH_BUILDER_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Allocated %d bytes flash image", SIMULATED_FLASH_SIZE);

    // Write components to flash image at correct offsets
    ESP_LOGI(TAG, "Writing components to flash image:");
    ESP_LOGI(TAG, "  Bootloader       @ 0x%04x (size=%zd)", BOOTLOADER_OFFSET, bootloader_size);
    memcpy(flash_image + BOOTLOADER_OFFSET, bootloader_data, bootloader_size);

    ESP_LOGI(TAG, "  Partition table  @ 0x%04x (size=%zd)", PARTITION_TABLE_OFFSET, partition_table_size);
    memcpy(flash_image + PARTITION_TABLE_OFFSET, partition_table_data, partition_table_size);

    ESP_LOGI(TAG, "  Application      @ 0x%04x (size=%zd)", FACTORY_APP_OFFSET, app_size);
    memcpy(flash_image + FACTORY_APP_OFFSET, app_data, app_size);

    // Write flash image to file
    FILE* out = fopen(output_path, "wb");
    if (!out) {
        ESP_LOGE(TAG, "Failed to create output file: %s", output_path);
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(app_data);
        return FLASH_BUILDER_ERR_IO;
    }

    size_t written = fwrite(flash_image, 1, SIMULATED_FLASH_SIZE, out);
    fclose(out);

    if (written != SIMULATED_FLASH_SIZE) {
        ESP_LOGE(TAG, "Failed to write complete flash image (written=%zu, expected=%d)",
                 written, SIMULATED_FLASH_SIZE);
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(app_data);
        return FLASH_BUILDER_ERR_IO;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✓ Flash image created successfully!");
    ESP_LOGI(TAG, "  File: %s", output_path);
    ESP_LOGI(TAG, "  Size: %d bytes (%.2f MB)", SIMULATED_FLASH_SIZE,
             SIMULATED_FLASH_SIZE / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "  Layout:");
    ESP_LOGI(TAG, "    0x0000 - 0x%04x:   Padding (8KB)", BOOTLOADER_OFFSET);
    ESP_LOGI(TAG, "    0x%04x - 0x%04x: Bootloader (%.2f KB)",
             BOOTLOADER_OFFSET, BOOTLOADER_OFFSET + bootloader_size,
             bootloader_size / 1024.0);
    ESP_LOGI(TAG, "    0x%04x - 0x%04x: Partition table (%.2f KB)",
             PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + partition_table_size,
             partition_table_size / 1024.0);
    ESP_LOGI(TAG, "    0x%04x - 0x%04x: Application (%.2f KB)",
             FACTORY_APP_OFFSET, FACTORY_APP_OFFSET + app_size,
             app_size / 1024.0);

    // Cleanup
    free(flash_image);
    free(bootloader_data);
    free(partition_table_data);
    free(app_data);

    return FLASH_BUILDER_OK;
}

flash_builder_err_t flash_builder_create_with_firmwares(
    const char* output_path,
    const char* bootloader_path,
    const char* partition_table_path,
    const char* factory_app_path,
    char** firmware_paths,
    char** firmware_names,
    int firmware_count,
    bool trim_zeros,
    int flash_size_mb)
{
    if (!output_path) {
        ESP_LOGE(TAG, "NULL output_path");
        return FLASH_BUILDER_ERR_INVALID_ARGS;
    }

    // Use defaults if not specified
    const char* bl_path = bootloader_path ? bootloader_path : "../build/bootloader/bootloader.bin";
    const char* pt_path = partition_table_path ? partition_table_path : "../build/partition_table/partition-table.bin";
    const char* fa_path = factory_app_path ? factory_app_path : "../build/esp32_p4_graphical_bootloader.bin";

    ESP_LOGI(TAG, "Creating multi-firmware flash image...");
    ESP_LOGI(TAG, "  Output: %s", output_path);
    ESP_LOGI(TAG, "  Bootloader: %s", bl_path);
    ESP_LOGI(TAG, "  Partition table: %s", pt_path);
    ESP_LOGI(TAG, "  Factory app: %s", fa_path);
    ESP_LOGI(TAG, "  Firmware count: %d", firmware_count);
    ESP_LOGI(TAG, "  Flash size: %d MB", flash_size_mb);

    // Allocate flash buffer
    size_t flash_size = flash_size_mb * 1024 * 1024;
    uint8_t* flash_image = (uint8_t*)calloc(1, flash_size);
    if (!flash_image) {
        ESP_LOGE(TAG, "Failed to allocate flash buffer (%zu MB)", flash_size / (1024 * 1024));
        return FLASH_BUILDER_ERR_NO_MEM;
    }

    // Fill with 0xFF (erased flash state)
    memset(flash_image, 0xFF, flash_size);

    // Read input files
    uint8_t* bootloader_data = malloc(256 * 1024);
    uint8_t* partition_table_data = malloc(64 * 1024);
    uint8_t* factory_app_data = malloc(8 * 1024 * 1024);

    if (!bootloader_data || !partition_table_data || !factory_app_data) {
        ESP_LOGE(TAG, "Failed to allocate input buffers");
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_NO_MEM;
    }

    // Read bootloader
    ssize_t bl_size = flash_builder_read_file(bl_path, bootloader_data, 256 * 1024);
    if (bl_size < 0) {
        ESP_LOGE(TAG, "Failed to read bootloader");
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_MISSING_FILE;
    }

    // Validate bootloader has valid ESP image magic
    if (bl_size < 8 || bootloader_data[0] != 0xE9) {
        ESP_LOGE(TAG, "Invalid bootloader image: missing ESP magic byte 0xE9 (found 0x%02X)",
                 bootloader_data[0]);
        ESP_LOGE(TAG, "Bootloader file: %s", bl_path);
        ESP_LOGE(TAG, "Please ensure the bootloader is built properly");
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_INVALID_ARGS;
    }

    // Don't read the old partition table - we'll generate a new one
    // Store pt_path for backward compatibility if no firmwares
    bool use_generated_pt = (firmware_count > 0);

    // Read factory app
    ssize_t fa_size = flash_builder_read_file(fa_path, factory_app_data, 8 * 1024 * 1024);
    if (fa_size < 0) {
        ESP_LOGE(TAG, "Failed to read factory app");
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_MISSING_FILE;
    }

    // Validate factory app has valid ESP image magic
    if (fa_size < 8 || factory_app_data[0] != 0xE9) {
        ESP_LOGE(TAG, "Invalid factory app image: missing ESP magic byte 0xE9 (found 0x%02X)",
                 factory_app_data[0]);
        ESP_LOGE(TAG, "Factory app file: %s", fa_path);
        ESP_LOGE(TAG, "Please ensure the factory app is built properly");
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_INVALID_ARGS;
    }

    ESP_LOGI(TAG, "✓ Read and validated bootloader: %zd bytes", bl_size);
    ESP_LOGI(TAG, "✓ Read and validated factory app: %zd bytes", fa_size);

    // Partition table size (will be set based on generated or existing PT)
    ssize_t pt_size = 0;

    // Get firmware sizes for partition table generation
    size_t* firmware_sizes = NULL;
    if (firmware_count > 0) {
        firmware_sizes = (size_t*)calloc(firmware_count, sizeof(size_t));
        if (!firmware_sizes) {
            ESP_LOGE(TAG, "Failed to allocate firmware sizes array");
            free(flash_image);
            free(bootloader_data);
            free(partition_table_data);
            free(factory_app_data);
            return FLASH_BUILDER_ERR_NO_MEM;
        }

        for (int i = 0; i < firmware_count; i++) {
            long fw_size = flash_builder_get_file_size(firmware_paths[i]);
            if (fw_size < 0) {
                ESP_LOGE(TAG, "Failed to get firmware size: %s", firmware_paths[i]);
                free(firmware_sizes);
                free(flash_image);
                free(bootloader_data);
                free(partition_table_data);
                free(factory_app_data);
                return FLASH_BUILDER_ERR_MISSING_FILE;
            }
            firmware_sizes[i] = (size_t)fw_size;
            ESP_LOGI(TAG, "✓ Firmware %d: %s (%.2f MB)",
                     i, firmware_names[i], fw_size / (1024.0 * 1024.0));
        }
    }

    // Generate partition table with OTA partitions
    uint8_t* generated_pt = NULL;
    size_t generated_pt_size = 0;
    uint32_t firmware_storage_offset = FIRMWARE_STORAGE_OFFSET;  // From shared header

    if (use_generated_pt) {
        esp_err_t ret = generate_partition_table(firmware_sizes, firmware_count,
                                                   &generated_pt, &generated_pt_size,
                                                   &firmware_storage_offset);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to generate partition table");
            free(firmware_sizes);
            free(flash_image);
            free(bootloader_data);
            free(partition_table_data);
            free(factory_app_data);
            return FLASH_BUILDER_ERR_IO;
        }
        partition_table_data = generated_pt;
        pt_size = (ssize_t)generated_pt_size;
    } else {
        // Use existing partition table if no firmwares
        ssize_t pt_read_size = flash_builder_read_file(pt_path, partition_table_data, 64 * 1024);
        if (pt_read_size < 0) {
            ESP_LOGE(TAG, "Failed to read partition table");
            free(firmware_sizes);
            free(flash_image);
            free(bootloader_data);
            free(partition_table_data);
            free(factory_app_data);
            return FLASH_BUILDER_ERR_MISSING_FILE;
        }
        pt_size = pt_read_size;
    }

    ESP_LOGI(TAG, "✓ Partition table: %zd bytes", pt_size);

    // Track highest offset used by any firmware (for trimming)
    size_t highest_ota_end = 0;

    // Write bootloader at 0x2000
    memcpy(flash_image + 0x2000, bootloader_data, bl_size);
    ESP_LOGI(TAG, "✓ Bootloader written at 0x00002000 (%.2f KB)", bl_size / 1024.0);

    // Write partition table at 0x10000
    memcpy(flash_image + 0x10000, partition_table_data, pt_size);
    ESP_LOGI(TAG, "✓ Partition table written at 0x00010000 (%.2f KB)", pt_size / 1024.0);

    // Write factory app at 0x20000
    memcpy(flash_image + 0x20000, factory_app_data, fa_size);
    ESP_LOGI(TAG, "✓ Factory app written at 0x00020000 (%.2f KB)", fa_size / 1024.0);

    // Add firmware storage metadata if any firmwares specified
    if (firmware_count > 0) {
        ESP_LOGI(TAG, "✓ Firmware storage metadata at 0x%08x (pointing to OTA partitions)", firmware_storage_offset);

        // Calculate header and entries size (metadata only, no duplicated firmware data)
        size_t header_size = sizeof(firmware_storage_header_t);
        size_t entries_size = firmware_count * sizeof(firmware_entry_t);
        size_t total_header_size = header_size + entries_size;

        // Create header
        firmware_storage_header_t* header = (firmware_storage_header_t*)(flash_image + firmware_storage_offset);
        memcpy(header->magic, "FWST", 4);
        header->version = 1;
        header->count = firmware_count;
        header->header_size = header_size;  // Just the header size, not including entries
        memset(header->reserved, 0, 16);

        ESP_LOGI(TAG, "✓ Firmware storage metadata header written");

        // Read all firmwares and write to OTA partitions
        size_t total_firmware_size = 0;

        for (int i = 0; i < firmware_count; i++) {
            // Get firmware file size
            long fw_size = flash_builder_get_file_size(firmware_paths[i]);
            if (fw_size < 0) {
                ESP_LOGE(TAG, "Failed to get firmware size: %s", firmware_paths[i]);
                continue;
            }

            total_firmware_size += fw_size;

            ESP_LOGI(TAG, "Processing firmware %d/%d: %s",
                     i + 1, firmware_count, firmware_names[i]);
            ESP_LOGI(TAG, "  Size: %ld bytes (%.2f MB)",
                     fw_size, fw_size / (1024.0 * 1024.0));

            // Read firmware
            uint8_t* fw_buffer = malloc(fw_size);
            if (!fw_buffer) {
                ESP_LOGE(TAG, "  Failed to allocate buffer");
                continue;
            }

            ssize_t bytes_read = flash_builder_read_file(firmware_paths[i], fw_buffer, fw_size);
            if (bytes_read != fw_size) {
                ESP_LOGE(TAG, "  Failed to read firmware");
                free(fw_buffer);
                continue;
            }

            // Calculate CRC32
            uint32_t crc = crc32_calculate(fw_buffer, fw_size);
            ESP_LOGI(TAG, "  ✓ Calculated CRC32: 0x%08X", crc);

            // Parse partition table to find OTA partition offset for this firmware
            partition_entry_t* pt_entries = (partition_entry_t*)partition_table_data;
            int pt_entry_count = pt_size / sizeof(partition_entry_t);

            // Find the corresponding OTA partition (ota_0, ota_1, etc.)
            uint32_t ota_offset = 0;
            char ota_name[16];
            snprintf(ota_name, sizeof(ota_name), "ota_%d", i);

            for (int j = 0; j < pt_entry_count; j++) {
                if (strcmp(pt_entries[j].name, ota_name) == 0) {
                    ota_offset = pt_entries[j].offset;
                    break;
                }
            }

            if (ota_offset == 0) {
                ESP_LOGE(TAG, "  ✗ OTA partition %s not found in partition table!", ota_name);
                free(fw_buffer);
                continue;
            }

            // Write firmware to OTA partition location (THIS IS THE CRITICAL FIX!)
            if (ota_offset + fw_size > flash_size) {
                ESP_LOGE(TAG, "  ✗ Firmware too large for OTA partition: exceeds flash size");
                free(fw_buffer);
                continue;
            }

            ESP_LOGI(TAG, "  → Writing %zd bytes to OTA offset 0x%08X...", fw_size, ota_offset);
            ESP_LOGI(TAG, "    First 4 bytes of source: 0x%02X%02X%02X%02X",
                     fw_buffer[0], fw_buffer[1], fw_buffer[2], fw_buffer[3]);
            memcpy(flash_image + ota_offset, fw_buffer, fw_size);
            ESP_LOGI(TAG, "    First 4 bytes of target after memcpy: 0x%02X%02X%02X%02X",
                     flash_image[ota_offset], flash_image[ota_offset+1],
                     flash_image[ota_offset+2], flash_image[ota_offset+3]);
            ESP_LOGI(TAG, "  ✓ Written to OTA partition %s at 0x%08X (%.2f MB)",
                     ota_name, ota_offset, fw_size / (1024.0 * 1024.0));

            // Track highest OTA end position for trimming
            size_t ota_end = ota_offset + fw_size;
            if (ota_end > highest_ota_end) {
                highest_ota_end = ota_end;
            }

            // Create firmware entry for firmware storage (for GUI discovery)
            // POINT TO OTA PARTITION LOCATION instead of duplicating data
            firmware_entry_t* entry = (firmware_entry_t*)(flash_image + firmware_storage_offset + header_size + (i * sizeof(firmware_entry_t)));
            entry->offset = ota_offset;  // Point to OTA partition, not storage offset
            entry->size = fw_size;
            entry->crc32 = crc;
            entry->flags = 0;
            strncpy(entry->name, firmware_names[i], 63);
            entry->name[63] = '\0';
            memset(entry->reserved, 0, 12);
            entry->next_offset = (i < firmware_count - 1) ? 0 : 0;  // Not used when pointing to OTA

            ESP_LOGI(TAG, "  ✓ Firmware storage entry %d at offset 0x%08zX points to OTA partition at 0x%08X",
                     i, firmware_storage_offset + header_size + (i * sizeof(firmware_entry_t)), ota_offset);

            free(fw_buffer);

            // No need to update current_offset since we're not duplicating firmware data
        }

        ESP_LOGI(TAG, "✓ All firmware entries written");
        ESP_LOGI(TAG, "✓ Total firmware data: %zu bytes (%.2f MB)",
                 total_firmware_size, total_firmware_size / (1024.0 * 1024.0));
    }

    // Write to output file
    ESP_LOGI(TAG, "Writing flash image to %s...", output_path);

    // Verify OTA data is still in buffer before writing
    ESP_LOGI(TAG, "  → Verifying data in buffer before file write...");
    ESP_LOGI(TAG, "    OTA_0 at 0x330000: 0x%02X%02X%02X%02X",
             flash_image[0x330000], flash_image[0x330001],
             flash_image[0x330002], flash_image[0x330003]);
    ESP_LOGI(TAG, "    OTA_1 at 0xA20000: 0x%02X%02X%02X%02X",
             flash_image[0xA20000], flash_image[0xA20001],
             flash_image[0xA20002], flash_image[0xA20003]);

    FILE* out_file = fopen(output_path, "wb");
    if (!out_file) {
        ESP_LOGE(TAG, "Failed to open output file: %s", output_path);
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_IO;
    }

    // Write entire flash image
    size_t bytes_written = fwrite(flash_image, 1, flash_size, out_file);
    fclose(out_file);

    if (bytes_written != flash_size) {
        ESP_LOGE(TAG, "Failed to write complete flash image: %zu / %zu bytes",
                 bytes_written, flash_size);
        free(flash_image);
        free(bootloader_data);
        free(partition_table_data);
        free(factory_app_data);
        return FLASH_BUILDER_ERR_IO;
    }

    ESP_LOGI(TAG, "✓ Flash image created: %s (%.2f MB)",
             output_path, bytes_written / (1024.0 * 1024.0));

    // Trim trailing zeros/0xFF if requested
    if (trim_zeros) {
        ESP_LOGI(TAG, "Trimming trailing empty space (0x00/0xFF)...");

        // Calculate minimum size needed (last OTA partition or firmware storage metadata)
        size_t min_size = flash_size;
        if (firmware_count > 0) {
            // Must include up to the end of the last OTA partition
            // NOT just firmware storage metadata!
            if (highest_ota_end > 0) {
                // Highest OTA partition end position
                min_size = highest_ota_end;
                ESP_LOGI(TAG, "  Highest OTA partition ends at: 0x%08zX (%.2f MB)",
                         min_size, min_size / (1024.0 * 1024.0));
            } else {
                // Fallback: firmware storage metadata only
                size_t header_size = sizeof(firmware_storage_header_t);
                size_t entries_size = firmware_count * sizeof(firmware_entry_t);
                size_t total_metadata_size = header_size + entries_size;
                min_size = firmware_storage_offset + total_metadata_size;
                ESP_LOGI(TAG, "  Firmware storage metadata: offset=0x%08x, size=%zu bytes (header=%zu, entries=%zu)",
                         firmware_storage_offset, total_metadata_size, header_size, entries_size);
            }
            ESP_LOGI(TAG, "  Minimum file size: %zu bytes (%.2f MB)",
                     min_size, min_size / (1024.0 * 1024.0));
        }

        // Find last non-empty byte (search from end of flash image)
        size_t last_non_empty = bytes_written - 1;

        // Only trim after min_size, don't trim into it
        if (last_non_empty >= min_size) {
            // Start looking from the end, but stop before min_size
            size_t search_start = (last_non_empty > min_size) ? last_non_empty : min_size;

            while (search_start > 0 &&
                   (flash_image[search_start] == 0 || flash_image[search_start] == 0xFF)) {
                search_start--;
            }

            // Don't trim before minimum size
            last_non_empty = (search_start < min_size - 1) ? min_size - 1 : search_start;
        } else {
            last_non_empty = min_size - 1;
        }

        // Rewrite trimmed file
        size_t trimmed_size = last_non_empty + 1;
        out_file = fopen(output_path, "wb");
        if (out_file) {
            fwrite(flash_image, 1, trimmed_size, out_file);
            fclose(out_file);

            size_t saved_bytes = bytes_written - trimmed_size;
            ESP_LOGI(TAG, "✓ Trimmed: original %.2f MB -> trimmed %.2f MB (saved %.2f MB, %.1f%%)",
                     bytes_written / (1024.0 * 1024.0),
                     trimmed_size / (1024.0 * 1024.0),
                     saved_bytes / (1024.0 * 1024.0),
                     (saved_bytes * 100.0) / bytes_written);
        }
    }

    // Print flashing instructions
    printf("\n");
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    printf("                    FLASH TO ESP32-P4 DEVICE\n");
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("Flash command:\n");
    printf("  python -m esptool --chip esp32p4 \\\n");
    printf("    -b 460800 \\\n");
    printf("    --before default_reset --after hard_reset \\\n");
    printf("    write_flash \\\n");
    printf("    --flash_mode dio --flash_size %dMB --flash_freq 80m \\\n", flash_size_mb);
    printf("    0x0 %s\n\n", output_path);
    printf("Or with faster baud rate:\n");
    printf("  python -m esptool --chip esp32p4 \\\n");
    printf("    -b 921600 \\\n");
    printf("    --before default_reset --after hard_reset \\\n");
    printf("    write_flash \\\n");
    printf("    --flash_mode dio --flash_size %dMB --flash_freq 80m \\\n", flash_size_mb);
    printf("    0x0 %s\n\n", output_path);
    printf("After flashing, the device will boot into the bootloader UI.\n");
    printf("You can then select which firmware to flash:\n");
    for (int i = 0; i < firmware_count; i++) {
        printf("  - %s\n", firmware_names[i]);
    }
    printf("\n");
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    printf("\n");

    // Cleanup
    free(flash_image);
    free(bootloader_data);
    free(factory_app_data);
    free(firmware_sizes);
    if (generated_pt) {
        free(generated_pt);
    } else {
        free(partition_table_data);
    }

    return FLASH_BUILDER_OK;
}

#endif // __SIMULATOR_BUILD__
