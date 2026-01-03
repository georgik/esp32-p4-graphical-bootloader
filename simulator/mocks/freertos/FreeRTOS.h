/**
 * @file FreeRTOS.h
 * @brief FreeRTOS wrapper for simulator
 */

#ifndef FREERTOS_H_MOCK
#define FREERTOS_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "freertos_mock.h"
#else
    #include_next "FreeRTOS.h"
#endif

#endif // FREERTOS_H_MOCK
