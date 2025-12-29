/**
 * @file esp_partition_mock.c
 * @brief Mock implementation of ESP partition operations with file backing
 */

#include "esp_partition_mock.h"
#include "esp_log_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char* TAG = "esp_partition_mock";

// Mock partition table matching ESP32-P4 layout
static const esp_partition_t mock_partitions[] = {
    {
        .type = ESP_PARTITION_TYPE_DATA,
        .subtype = ESP_PARTITION_SUBTYPE_DATA_NVS,
        .address = 0x9000,
        .size = 0x6000,
        .label = "nvs",
        .flags = 0,
        .next = NULL
    },
    {
        .type = ESP_PARTITION_TYPE_DATA,
        .subtype = ESP_PARTITION_SUBTYPE_DATA_PHY,
        .address = 0xf000,
        .size = 0x1000,
        .label = "phy_init",
        .flags = 0,
        .next = NULL
    },
    {
        .type = ESP_PARTITION_TYPE_APP,
        .subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY,
        .address = 0x20000,
        .size = 0x100000,
        .label = "factory",
        .flags = 0,
        .next = NULL
    },
    {
        .type = ESP_PARTITION_TYPE_APP,
        .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0,
        .address = 0x330000,
        .size = 0x4C0000,
        .label = "ota_0",
        .flags = 0,
        .next = NULL
    },
    {
        .type = ESP_PARTITION_TYPE_APP,
        .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1,
        .address = 0x800000,
        .size = 0x400000,
        .label = "ota_1",
        .flags = 0,
        .next = NULL
    },
    {
        .type = ESP_PARTITION_TYPE_APP,
        .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_2,
        .address = 0xC00000,
        .size = 0x400000,
        .label = "ota_2",
        .flags = 0,
        .next = NULL
    },
};

#define PARTITION_COUNT (sizeof(mock_partitions) / sizeof(mock_partitions[0]))

// Flash data directory
#define FLASH_DATA_DIR ".esp32-simulator/flash"

// Ensure flash data directory exists
static void ensure_flash_dir(void) {
    mkdir(".esp32-simulator", 0755);
    mkdir(FLASH_DATA_DIR, 0755);
}

// Get filename for partition data
static void get_partition_filename(const esp_partition_t* partition, char* filename, size_t max_len) {
    snprintf(filename, max_len, "%s/0x%08x.bin", FLASH_DATA_DIR, partition->address);
}

const esp_partition_t* esp_partition_find_first(
    esp_partition_type_t type,
    esp_partition_subtype_t subtype,
    const char* label) {

    for (size_t i = 0; i < PARTITION_COUNT; i++) {
        const esp_partition_t* part = &mock_partitions[i];

        // Check type
        if (part->type != type && subtype != ESP_PARTITION_SUBTYPE_ANY) {
            continue;
        }

        // Check subtype
        if (subtype != ESP_PARTITION_SUBTYPE_ANY && part->subtype != subtype) {
            continue;
        }

        // Check label
        if (label != NULL && strcmp(part->label, label) != 0) {
            continue;
        }

        ESP_LOGI(TAG, "Found partition: %s @ 0x%x, size 0x%x",
                 part->label, (unsigned int)part->address, (unsigned int)part->size);
        return part;
    }

    ESP_LOGW(TAG, "Partition not found: type=%d, subtype=%d, label=%s",
             type, subtype, label ? label : "any");
    return NULL;
}

const esp_partition_t* esp_partition_next(const esp_partition_t* partition) {
    if (!partition) return NULL;

    for (size_t i = 0; i < PARTITION_COUNT - 1; i++) {
        if (&mock_partitions[i] == partition) {
            return &mock_partitions[i + 1];
        }
    }

    return NULL;
}

esp_err_t esp_partition_read(
    const esp_partition_t* partition,
    size_t src_offset,
    void* dst,
    size_t size) {

    if (!partition) {
        ESP_LOGE(TAG, "NULL partition");
        return ESP_ERR_INVALID_ARG;
    }

    if (src_offset + size > partition->size) {
        ESP_LOGE(TAG, "Read out of bounds: offset=0x%x, size=0x%x, partition_size=0x%x",
                 (unsigned int)src_offset, (unsigned int)size, (unsigned int)partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    ensure_flash_dir();

    char filename[256];
    get_partition_filename(partition, filename, sizeof(filename));

    FILE* f = fopen(filename, "rb");
    if (!f) {
        // File doesn't exist yet, return zeros
        ESP_LOGD(TAG, "Partition file not found, returning zeros: %s", filename);
        memset(dst, 0xFF, size);  // Flash defaults to 0xFF
        return ESP_OK;
    }

    // Seek to offset
    if (fseek(f, src_offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek in partition file");
        fclose(f);
        return ESP_FAIL;
    }

    // Read data
    size_t bytes_read = fread(dst, 1, size, f);
    fclose(f);

    if (bytes_read != size) {
        // If we couldn't read enough, pad with 0xFF
        ESP_LOGD(TAG, "Partial read: %zu/%zu bytes, padding with 0xFF", bytes_read, size);
        memset((uint8_t*)dst + bytes_read, 0xFF, size - bytes_read);
    }

    ESP_LOGD(TAG, "Read partition %s @ 0x%x, size %zu bytes",
             partition->label, (unsigned int)src_offset, size);
    return ESP_OK;
}

esp_err_t esp_partition_write(
    const esp_partition_t* partition,
    size_t dst_offset,
    const void* src,
    size_t size) {

    if (!partition) {
        ESP_LOGE(TAG, "NULL partition");
        return ESP_ERR_INVALID_ARG;
    }

    if (dst_offset + size > partition->size) {
        ESP_LOGE(TAG, "Write out of bounds: offset=0x%x, size=0x%x, partition_size=0x%x",
                 (unsigned int)dst_offset, (unsigned int)size, (unsigned int)partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    ensure_flash_dir();

    char filename[256];
    get_partition_filename(partition, filename, sizeof(filename));

    // Open file for reading/writing
    FILE* f = fopen(filename, "r+b");
    if (!f) {
        // Create new file
        f = fopen(filename, "wb");
        if (!f) {
            ESP_LOGE(TAG, "Failed to create partition file: %s", filename);
            return ESP_FAIL;
        }

        // Pre-allocate file size
        fseek(f, partition->size - 1, SEEK_SET);
        fputc(0xFF, f);
        fseek(f, 0, SEEK_SET);
    }

    // Seek to offset
    if (fseek(f, dst_offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Failed to seek in partition file");
        fclose(f);
        return ESP_FAIL;
    }

    // Write data
    size_t bytes_written = fwrite(src, 1, size, f);
    fclose(f);

    if (bytes_written != size) {
        ESP_LOGE(TAG, "Failed to write complete data: %zu/%zu bytes", bytes_written, size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✍️  Wrote partition %s @ 0x%x, size %zu bytes",
             partition->label, (unsigned int)dst_offset, size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(
    const esp_partition_t* partition,
    size_t start_addr,
    size_t size) {

    if (!partition) {
        ESP_LOGE(TAG, "NULL partition");
        return ESP_ERR_INVALID_ARG;
    }

    ensure_flash_dir();

    char filename[256];
    get_partition_filename(partition, filename, sizeof(filename));

    // Create/overwrite file with 0xFF (erased flash state)
    FILE* f = fopen(filename, "r+b");
    if (!f) {
        f = fopen(filename, "wb");
    }

    if (!f) {
        ESP_LOGE(TAG, "Failed to open partition file for erase");
        return ESP_FAIL;
    }

    // Seek to start
    fseek(f, start_addr, SEEK_SET);

    // Write 0xFF for erased bytes
    uint8_t* erase_buffer = calloc(size, 1);
    memset(erase_buffer, 0xFF, size);
    fwrite(erase_buffer, 1, size, f);
    free(erase_buffer);
    fclose(f);

    ESP_LOGI(TAG, "🧹 Erased partition %s @ 0x%x, size %zu bytes",
             partition->label, (unsigned int)start_addr, size);
    return ESP_OK;
}

esp_err_t esp_partition_get_sha256(
    const esp_partition_t* partition,
    uint8_t* sha256_out) {

    // Simplified: just return zeros
    // In real implementation, would calculate SHA256 of partition
    memset(sha256_out, 0, 32);
    return ESP_OK;
}

uint32_t esp_partition_get_flash_size(const esp_partition_t* partition) {
    return partition ? partition->size : 0;
}
