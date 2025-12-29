/**
 * @file FreeRTOS.h
 * @brief Shim to redirect FreeRTOS to our mock
 */

#ifndef FREERTOS_H_SHIM
#define FREERTOS_H_SHIM

// For simulator, use our mock implementation
#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/freertos_mock.h"
#else
    // For actual ESP-IDF build, include the real FreeRTOS
    #include "freertos/FreeRTOS.h"
#endif

#endif // FREERTOS_H_SHIM
