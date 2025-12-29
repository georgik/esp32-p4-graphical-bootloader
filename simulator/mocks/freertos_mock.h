/**
 * @file freertos_mock.h
 * @brief Mock implementation of FreeRTOS using pthreads
 */

#ifndef FREERTOS_MOCK_H
#define FREERTOS_MOCK_H

#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Type definitions
typedef void* TaskHandle_t;
typedef void* TimerHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* EventGroupHandle_t;
typedef void* QueueHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

// Constants
#define portMAX_DELAY        UINT32_MAX
#define pdMS_TO_TICKS(ms)    ((ms) / portTICK_PERIOD_MS)
#define pdTICKS_TO_MS(ms)    ((ms) * portTICK_PERIOD_MS)
#define portTICK_PERIOD_MS   1  // 1ms tick
#define configTICK_RATE_HZ   1000

// Return values
#define pdPASS               1
#define pdFAIL               0
#define pdTRUE               1
#define pdFALSE              0
#define errQUEUE_FULL        2

// Task priorities
#define tskIDLE_PRIORITY     0
#define configMAX_PRIORITIES 7

// Task function prototype
typedef void (*TaskFunction_t)(void* pvParameters);

// Task creation
BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,
    const char* const pcName,
    const uint32_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask
);

BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t pxTaskCode,
    const char* const pcName,
    const uint32_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask,
    const BaseType_t xCoreID
);

void vTaskDelay(const TickType_t xTicksToDelay);
TickType_t xTaskGetTickCount(void);
void vTaskDelete(TaskHandle_t xTask);
void taskYIELD(void);

// Task utilities
char* pcTaskGetName(TaskHandle_t xTask);
UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask);
void vTaskSuspend(TaskHandle_t xTask);
void vTaskResume(TaskHandle_t xTask);

// Semaphores
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount);

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t* pxHigherPriorityTaskWoken);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

// Queue management
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);
void vQueueDelete(QueueHandle_t xQueue);

// CPU utilities
UBaseType_t xPortGetCoreID(void);

// Critical sections (simplified - use mutex)
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);

// Task scheduler (simplified - pthreads are always running)
void vTaskStartScheduler(void);
void vTaskEndScheduler(void);

#ifdef __cplusplus
}
#endif

#endif // FREERTOS_MOCK_H
