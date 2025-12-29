/**
 * @file esp_partition.h
 * @brief Shim to redirect ESP-IDF esp_partition.h to our mock
 */

#ifndef ESP_PARTITION_H_SHIM
#define ESP_PARTITION_H_SHIM

// For simulator, use our mock implementation
#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/esp_partition_mock.h"
#else
    // For actual ESP-IDF build, include the real ESP-IDF header
    #include "esp_partition.h"
#endif

#endif // ESP_PARTITION_H_SHIM
