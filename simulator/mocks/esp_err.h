/**
 * @file esp_err.h
 * @brief ESP error code definitions
 */

#ifndef ESP_ERR_H_MOCK
#define ESP_ERR_H_MOCK

#ifdef __SIMULATOR_BUILD__
    // Use simulator's error definitions
    #include "esp_system_mock.h"
#else
    // For hardware, this header should not be used
    // Include the real ESP-IDF esp_err.h
    #include_next "esp_err.h"
#endif

#endif // ESP_ERR_H_MOCK
