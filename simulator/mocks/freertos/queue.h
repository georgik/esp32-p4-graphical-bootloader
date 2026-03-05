/**
 * @file queue.h
 * @brief FreeRTOS queue.h wrapper for simulator
 */

#ifndef QUEUE_H_MOCK
#define QUEUE_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "freertos_mock.h"
#else
    #include_next "queue.h"
#endif

#endif // QUEUE_H_MOCK
