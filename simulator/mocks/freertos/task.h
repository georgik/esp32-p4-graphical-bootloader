/**
 * @file task.h
 * @brief FreeRTOS task.h wrapper for simulator
 */

#ifndef TASK_H_MOCK
#define TASK_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "freertos_mock.h"
#else
    #include_next "task.h"
#endif

#endif // TASK_H_MOCK
