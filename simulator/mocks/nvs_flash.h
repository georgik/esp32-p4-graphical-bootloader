/**
 * @file nvs_flash.h
 * @brief NVS flash definitions
 */

#ifndef NVS_FLASH_H_MOCK
#define NVS_FLASH_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "nvs_mock.h"
#else
    #include_next "nvs_flash.h"
#endif

#endif // NVS_FLASH_H_MOCK
