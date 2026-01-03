/**
 * @file esp_system.h
 * @brief ESP system definitions
 */

#ifndef ESP_SYSTEM_H_MOCK
#define ESP_SYSTEM_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
#else
    #include_next "esp_system.h"
#endif

#endif // ESP_SYSTEM_H_MOCK
