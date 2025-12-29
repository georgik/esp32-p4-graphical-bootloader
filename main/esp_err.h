/**
 * @file esp_err.h
 * @brief Shim to redirect ESP-IDF esp_err.h to our mock
 *
 * This file exists so we don't have to modify the existing bootloader code.
 * It simply includes the mock esp_system_mock.h which defines esp_err_t.
 */

#ifndef ESP_ERR_H_SHIM
#define ESP_ERR_H_SHIM

// For simulator, use our mock implementation
#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/esp_system_mock.h"
#else
    // For actual ESP-IDF build, include the real ESP-IDF header
    #include "esp_err.h"
#endif

#endif // ESP_ERR_H_SHIM
