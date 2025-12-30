/**
 * @file esp_partition.h
 * @brief ESP partition definitions
 */

#ifndef ESP_PARTITION_H_MOCK
#define ESP_PARTITION_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_partition_mock.h"
#else
    #include_next "esp_partition.h"
#endif

#endif // ESP_PARTITION_H_MOCK
