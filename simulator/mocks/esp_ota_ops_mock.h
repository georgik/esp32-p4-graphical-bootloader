/**
 * @file esp_ota_ops_mock.h
 * @brief Mock implementation of OTA operations
 */

#ifndef ESP_OTA_OPS_MOCK_H
#define ESP_OTA_OPS_MOCK_H

#include "esp_partition_mock.h"
#include "esp_system_mock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t esp_ota_handle_t;

// OTA size constants
#define OTA_SIZE_UNKNOWN (0xFFFFFFFF)

esp_err_t esp_ota_begin(const esp_partition_t* partition, uint32_t update_size, esp_ota_handle_t* out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start_from);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition);

#ifdef __cplusplus
}
#endif

#endif // ESP_OTA_OPS_MOCK_H
