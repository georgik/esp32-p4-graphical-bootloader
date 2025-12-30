/**
 * @file esp_partition_mock.c
 * @brief Mock implementation of ESP partition operations using flash emulator
 */

#include "esp_partition_mock.h"
#include "esp_log_mock.h"
#include "../platform/flash_emulator.h"
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

    // Calculate absolute flash address
    uint32_t flash_addr = partition->address + src_offset;

    // Read from flash emulator
    esp_err_t ret = flash_emulator_read(flash_addr, dst, size);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from flash emulator @ 0x%x", flash_addr);
        return ret;
    }

    ESP_LOGD(TAG, "Read partition %s @ 0x%x (flash @ 0x%x), size %zu bytes",
             partition->label, (unsigned int)src_offset, flash_addr, size);

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

    // Calculate absolute flash address
    uint32_t flash_addr = partition->address + dst_offset;

    // Write to flash emulator
    esp_err_t ret = flash_emulator_write(flash_addr, src, size);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to flash emulator @ 0x%x", flash_addr);
        return ret;
    }

    ESP_LOGI(TAG, "✍️  Wrote partition %s @ 0x%x (flash @ 0x%x), size %zu bytes",
             partition->label, (unsigned int)dst_offset, flash_addr, size);

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

    if (start_addr + size > partition->size) {
        ESP_LOGE(TAG, "Erase out of bounds: offset=0x%x, size=0x%x, partition_size=0x%x",
                 (unsigned int)start_addr, (unsigned int)size, (unsigned int)partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Calculate absolute flash address
    uint32_t flash_addr = partition->address + start_addr;

    // Erase in flash emulator
    esp_err_t ret = flash_emulator_erase(flash_addr, size);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase flash emulator @ 0x%x", flash_addr);
        return ret;
    }

    ESP_LOGI(TAG, "🧹 Erased partition %s @ 0x%x (flash @ 0x%x), size %zu bytes",
             partition->label, (unsigned int)start_addr, flash_addr, size);

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
