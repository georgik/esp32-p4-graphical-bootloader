/**
 * @file flash_builder.c
 * @brief Flash image builder implementation
 */

#ifdef __SIMULATOR_BUILD__

#include "flash_builder.h"
#include "esp_log_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#endif // __SIMULATOR_BUILD__
