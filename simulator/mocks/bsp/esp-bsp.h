/**
 * @file esp-bsp.h
 * @brief BSP (Board Support Package) wrapper header
 *
 * For simulator builds, includes bsp_mock.h
 * For ESP-IDF builds, includes the real bsp/esp-bsp.h
 */

#ifndef ESP_BSP_H_MOCK
#define ESP_BSP_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "bsp_mock.h"
#else
    #include_next "bsp/esp-bsp.h"
#endif

#endif // ESP_BSP_H_MOCK
