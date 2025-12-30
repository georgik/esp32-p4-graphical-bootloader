/**
 * @file flash_emulator.c
 * @brief Flash write emulation with progress tracking and mmap support
 */

#include "flash_emulator.h"
#include "../mocks/esp_partition_mock.h"
#include "../mocks/esp_log_mock.h"
#include "flash_builder.h"  // For SIMULATED_FLASH_SIZE
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static const char* TAG = "flash_emulator";

// mmap state
static uint8_t* flash_mapped_base = NULL;
static size_t flash_mapped_size = 0;
static int flash_fd = -1;

static flash_progress_callback_t progress_callback = NULL;
static flash_stats_t stats = {0};

esp_err_t flash_emulator_init(const char* flash_path) {
    if (flash_mapped_base != NULL) {
        ESP_LOGW(TAG, "Flash emulator already initialized");
        return ESP_OK;
    }

    if (!flash_path) {
        ESP_LOGE(TAG, "NULL flash_path");
        return ESP_ERR_INVALID_ARG;
    }

    // Open flash image file
    flash_fd = open(flash_path, O_RDWR);
    if (flash_fd < 0) {
        ESP_LOGE(TAG, "Failed to open flash image: %s", flash_path);
        return ESP_FAIL;
    }

    // Get file size
    struct stat st;
    if (fstat(flash_fd, &st) != 0) {
        ESP_LOGE(TAG, "Failed to stat flash image");
        close(flash_fd);
        flash_fd = -1;
        return ESP_FAIL;
    }

    flash_mapped_size = st.st_size;

    // Map file into memory
    flash_mapped_base = mmap(NULL, flash_mapped_size,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED, flash_fd, 0);

    if (flash_mapped_base == MAP_FAILED) {
        ESP_LOGE(TAG, "Failed to mmap flash image");
        close(flash_fd);
        flash_fd = -1;
        flash_mapped_base = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Flash image mapped: %s (%zu bytes @ %p)",
             flash_path, flash_mapped_size, flash_mapped_base);

    return ESP_OK;
}

void flash_emulator_deinit(void) {
    if (flash_mapped_base != NULL) {
        munmap(flash_mapped_base, flash_mapped_size);
        flash_mapped_base = NULL;
        flash_mapped_size = 0;
    }

    if (flash_fd >= 0) {
        close(flash_fd);
        flash_fd = -1;
    }

    ESP_LOGI(TAG, "Flash emulator deinitialized");
}

bool flash_emulator_is_initialized(void) {
    return flash_mapped_base != NULL;
}

esp_err_t flash_emulator_read(uint32_t offset, void* buffer, size_t size) {
    if (!flash_mapped_base) {
        ESP_LOGE(TAG, "Flash emulator not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!buffer || size == 0) {
        ESP_LOGE(TAG, "Invalid buffer or size");
        return ESP_ERR_INVALID_ARG;
    }

    // Check bounds
    if (offset + size > flash_mapped_size) {
        ESP_LOGE(TAG, "Read out of bounds: offset=%u + size=%zu > flash_size=%zu",
                 offset, size, flash_mapped_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Read from mapped memory
    memcpy(buffer, flash_mapped_base + offset, size);

    ESP_LOGD(TAG, "Read %zu bytes @ 0x%x", size, offset);
    return ESP_OK;
}

esp_err_t flash_emulator_write(uint32_t offset, const void* data, size_t size) {
    if (!flash_mapped_base) {
        ESP_LOGE(TAG, "Flash emulator not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || size == 0) {
        ESP_LOGE(TAG, "Invalid data or size");
        return ESP_ERR_INVALID_ARG;
    }

    // Check bounds
    if (offset + size > flash_mapped_size) {
        ESP_LOGE(TAG, "Write out of bounds: offset=%u + size=%zu > flash_size=%zu",
                 offset, size, flash_mapped_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Write to mapped memory
    memcpy(flash_mapped_base + offset, data, size);

    // Flush to disk (msync)
    msync(flash_mapped_base + offset, size, MS_ASYNC);

    ESP_LOGD(TAG, "Wrote %zu bytes @ 0x%x", size, offset);
    return ESP_OK;
}

esp_err_t flash_emulator_erase(uint32_t offset, size_t size) {
    if (!flash_mapped_base) {
        ESP_LOGE(TAG, "Flash emulator not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (size == 0) {
        return ESP_OK;
    }

    // Check bounds
    if (offset + size > flash_mapped_size) {
        ESP_LOGE(TAG, "Erase out of bounds: offset=%u + size=%zu > flash_size=%zu",
                 offset, size, flash_mapped_size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Fill with 0xFF (erased flash state)
    memset(flash_mapped_base + offset, 0xFF, size);

    // Flush to disk
    msync(flash_mapped_base + offset, size, MS_ASYNC);

    ESP_LOGD(TAG, "Erased %zu bytes @ 0x%x", size, offset);
    return ESP_OK;
}

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
