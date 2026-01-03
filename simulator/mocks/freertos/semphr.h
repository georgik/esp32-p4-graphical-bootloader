/**
 * @file semphr.h
 * @brief FreeRTOS semphr.h wrapper for simulator
 */

#ifndef SEMPHR_H_MOCK
#define SEMPHR_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "freertos_mock.h"
#else
    #include_next "semphr.h"
#endif

#endif // SEMPHR_H_MOCK
