/**
 * @file esp_ota_ops.h
 * @brief Shim for OTA operations
 */

#ifndef ESP_OTA_OPS_H_SHIM
#define ESP_OTA_OPS_H_SHIM

#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/esp_system_mock.h"
    #include "../simulator/mocks/esp_partition_mock.h"
    #include <stdint.h>
    #include <stddef.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    // OTA handle
    typedef uint32_t esp_ota_handle_t;

    // OTA size unknown
    #define OTA_SIZE_UNKNOWN UINT32_MAX

    // OTA operations
    const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start_from);
    esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition);
    esp_err_t esp_ota_begin(const esp_partition_t* partition, uint32_t update_size, esp_ota_handle_t* out_handle);
    esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size);
    esp_err_t esp_ota_end(esp_ota_handle_t handle);
    esp_err_t esp_ota_abort(esp_ota_handle_t handle);

    #ifdef __cplusplus
    }
    #endif

#else
    #include "esp_err.h"
    #include "esp_partition.h"

    #ifdef __cplusplus
    extern "C" {
    #endif

    const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start_from);
    esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition);

    #ifdef __cplusplus
    }
    #endif
#endif

#endif // ESP_OTA_OPS_H_SHIM
