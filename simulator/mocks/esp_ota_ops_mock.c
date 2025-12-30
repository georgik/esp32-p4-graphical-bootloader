/**
 * @file esp_ota_ops_mock.c
 * @brief Mock implementation of OTA operations
 */

#include "esp_ota_ops.h"
#include "esp_log_mock.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "esp_ota_ops_mock";

// Simple mock implementation
esp_err_t esp_ota_begin(const esp_partition_t* partition, uint32_t update_size, esp_ota_handle_t* out_handle) {
    (void)partition;
    (void)update_size;

    static uint32_t next_handle = 1;
    *out_handle = next_handle++;

    ESP_LOGI(TAG, "Mock OTA begin (handle: %u)", (unsigned int)*out_handle);
    return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size) {
    (void)handle;
    (void)data;
    (void)size;

    ESP_LOGI(TAG, "Mock OTA write: %zu bytes", size);
    return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    ESP_LOGI(TAG, "Mock OTA end (handle: %u)", (unsigned int)handle);
    return ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    ESP_LOGW(TAG, "Mock OTA abort (handle: %u)", (unsigned int)handle);
    return ESP_OK;
}

const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start_from) {
    (void)start_from;
    ESP_LOGI(TAG, "Mock get next update partition");
    // Return NULL - simulator doesn't have real partitions
    return NULL;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
    (void)partition;
    ESP_LOGI(TAG, "Mock set boot partition");
    return ESP_OK;
}
