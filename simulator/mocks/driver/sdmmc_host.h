/**
 * @file sdmmc_host.h
 * @brief SDMMC host driver definitions
 */

#ifndef SDMMC_HOST_H_MOCK
#define SDMMC_HOST_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    // Mock - not implemented in simulator
#else
    #include_next "driver/sdmmc_host.h"
#endif

#endif // SDMMC_HOST_H_MOCK
