/**
 * @file esp_log.h
 * @brief ESP logging definitions
 */

#ifndef ESP_LOG_H_MOCK
#define ESP_LOG_H_MOCK

#ifdef __SIMULATOR_BUILD__
    // Use simulator's logging
    #include "esp_log_mock.h"
#else
    // For hardware, use real ESP-IDF header
    #include_next "esp_log.h"
#endif

#endif // ESP_LOG_H_MOCK
