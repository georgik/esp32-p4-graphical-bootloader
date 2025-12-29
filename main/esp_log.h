/**
 * @file esp_log.h
 * @brief Shim to redirect ESP-IDF esp_log.h to our mock
 */

#ifndef ESP_LOG_H_SHIM
#define ESP_LOG_H_SHIM

// For simulator, use our mock implementation
#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/esp_log_mock.h"
#else
    // For actual ESP-IDF build, include the real ESP-IDF header
    #include "esp_log.h"
#endif

#endif // ESP_LOG_H_SHIM
