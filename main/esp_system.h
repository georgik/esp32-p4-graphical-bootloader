/**
 * @file esp_system.h
 * @brief Shim to redirect ESP-IDF esp_system.h to our mock
 */

#ifndef ESP_SYSTEM_H_SHIM
#define ESP_SYSTEM_H_SHIM

// For simulator, use our mock implementation
#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/esp_system_mock.h"
#else
    // For actual ESP-IDF build, include the real ESP-IDF header
    #include "esp_system.h"
#endif

#endif // ESP_SYSTEM_H_SHIM
