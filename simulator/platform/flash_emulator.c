/**
 * @file flash_emulator.c
 * @brief Flash write emulation with progress tracking
 */

#include "flash_emulator.h"
#include "../mocks/esp_partition_mock.h"
#include "../mocks/esp_log_mock.h"
#include <sys/time.h>
#include <string.h>

static const char* TAG = "flash_emulator";

static flash_progress_callback_t progress_callback = NULL;
static flash_stats_t stats = {0};

void flash_emulator_set_progress_callback(flash_progress_callback_t callback) {
    progress_callback = callback;
}

void flash_emulator_get_stats(flash_stats_t* out_stats) {
    if (out_stats) {
        memcpy(out_stats, &stats, sizeof(stats));
    }
}

void flash_emulator_reset_stats(void) {
    memset(&stats, 0, sizeof(stats));
}

esp_err_t flash_emulator_write_partition(
    const char* partition_name,
    uint32_t offset,
    const uint8_t* data,
    uint32_t size) {

    struct timeval start, end;
    gettimeofday(&start, NULL);

    // Find partition
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_ANY,
        partition_name
    );

    if (!partition) {
        ESP_LOGE(TAG, "Partition not found: %s", partition_name);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "📝 Flash write: %s @ 0x%x, size %lu bytes",
             partition_name, (unsigned int)offset, (unsigned long)size);

    // Call progress callback if set
    if (progress_callback) {
        progress_callback(FLASH_OP_WRITE, offset, size, partition->size, partition_name);
    }

    // Perform write operation
    esp_err_t ret = esp_partition_write(partition, offset, data, size);

    gettimeofday(&end, NULL);
    uint32_t elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                          (end.tv_usec - start.tv_usec) / 1000;

    if (ret == ESP_OK) {
        stats.bytes_written += size;
        stats.operation_count++;
        stats.total_time_ms += elapsed_ms;

        ESP_LOGI(TAG, "✅ Flash write complete in %ums", (unsigned int)elapsed_ms);
    } else {
        ESP_LOGE(TAG, "❌ Flash write failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t flash_emulator_erase_partition(
    const char* partition_name,
    uint32_t offset,
    uint32_t size) {

    struct timeval start, end;
    gettimeofday(&start, NULL);

    // Find partition
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_ANY,
        partition_name
    );

    if (!partition) {
        ESP_LOGE(TAG, "Partition not found: %s", partition_name);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "🧹 Flash erase: %s @ 0x%x, size %lu bytes",
             partition_name, (unsigned int)offset, (unsigned long)size);

    // Call progress callback if set
    if (progress_callback) {
        progress_callback(FLASH_OP_ERASE, offset, size, partition->size, partition_name);
    }

    // Perform erase operation
    esp_err_t ret = esp_partition_erase_range(partition, offset, size);

    gettimeofday(&end, NULL);
    uint32_t elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                          (end.tv_usec - start.tv_usec) / 1000;

    if (ret == ESP_OK) {
        stats.bytes_erased += size;
        stats.operation_count++;
        stats.total_time_ms += elapsed_ms;

        ESP_LOGI(TAG, "✅ Flash erase complete in %ums", (unsigned int)elapsed_ms);
    } else {
        ESP_LOGE(TAG, "❌ Flash erase failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
