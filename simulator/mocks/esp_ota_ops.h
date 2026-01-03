/**
 * @file esp_ota_ops.h
 * @brief ESP OTA operations
 */

#ifndef ESP_OTA_OPS_H_MOCK
#define ESP_OTA_OPS_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_ota_ops_mock.h"
#else
    #include_next "esp_ota_ops.h"
#endif

#endif // ESP_OTA_OPS_H_MOCK
