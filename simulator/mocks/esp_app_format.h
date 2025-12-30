/**
 * @file esp_app_format.h
 * @brief ESP application format definitions
 */

#ifndef ESP_APP_FORMAT_H_MOCK
#define ESP_APP_FORMAT_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    // Mock - not implemented in simulator
#else
    #include_next "esp_app_format.h"
#endif

#endif // ESP_APP_FORMAT_H_MOCK
