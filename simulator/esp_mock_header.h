/**
 * @file esp_mock_header.h
 * @brief Central header to include all ESP-IDF mocks
 *
 * This should be included BEFORE any ESP-IDF headers to ensure
 * mock implementations are used instead of real ESP-IDF components.
 */

#ifndef ESP_MOCK_HEADER_H
#define ESP_MOCK_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

// Include all mock headers first
#include "esp_system_mock.h"
#include "esp_log_mock.h"
#include "freertos_mock.h"
#include "esp_partition_mock.h"
#include "nvs_mock.h"
#include "bsp_mock.h"

#ifdef __cplusplus
}
#endif

#endif // ESP_MOCK_HEADER_H
