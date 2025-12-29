/**
 * @file sdmmc_host.h
 * @brief Shim for SDMMC host driver
 */

#ifndef DRIVER_SDMMC_HOST_H
#define DRIVER_SDMMC_HOST_H

#ifdef __SIMULATOR_BUILD__
    // Empty mock - just provide the header guard

    #ifdef __cplusplus
    extern "C" {
    #endif

    // No types or functions needed for simulator

    #ifdef __cplusplus
    }
    #endif

#else
    #include "driver/sdmmc_host.h"
#endif

#endif // DRIVER_SDMMC_HOST_H
