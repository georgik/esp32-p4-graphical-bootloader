/**
 * @file esp_flash_partitions.h
 * @brief ESP flash partition definitions
 */

#ifndef ESP_FLASH_PARTITIONS_H_MOCK
#define ESP_FLASH_PARTITIONS_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    // Mock - not implemented in simulator
#else
    #include_next "esp_flash_partitions.h"
#endif

#endif // ESP_FLASH_PARTITIONS_H_MOCK
