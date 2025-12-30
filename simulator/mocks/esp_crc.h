/**
 * @file esp_crc.h
 * @brief ESP CRC definitions
 */

#ifndef ESP_CRC_H_MOCK
#define ESP_CRC_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    // Mock - not implemented in simulator
#else
    #include_next "esp_crc.h"
#endif

#endif // ESP_CRC_H_MOCK
