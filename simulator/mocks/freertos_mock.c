/**
 * @file freertos_mock.c
 * @brief Mock implementation of FreeRTOS using pthreads
 */

#include "freertos_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

// Global tick counter
static uint64_t tick_counter = 0;
static pthread_mutex_t tick_mutex = PTHREAD_MUTEX_INITIALIZER;

// Get current tick count
TickType_t xTaskGetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);  // milliseconds
}

// Task creation
BaseType_t xTaskCreate(
    TaskFunction_t pxTaskCode,
    const char* const pcName,
    const uint32_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask) {

    return xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth,
                                   pvParameters, uxPriority,
                                   pxCreatedTask, 0);
}

BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t pxTaskCode,
    const char* const pcName,
    const uint32_t usStackDepth,
    void* const pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t* const pxCreatedTask,
    const BaseType_t xCoreID) {

    pthread_t* thread = malloc(sizeof(pthread_t));
    if (!thread) {
        fprintf(stderr, "[FreeRTOS Mock] Failed to allocate thread handle\n");
        return pdFAIL;
    }

    // Set up thread attributes
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // Set stack size
    if (usStackDepth > 0) {
        // usStackDepth is in words (4 bytes), convert to bytes
        size_t stack_size = usStackDepth * 4;
        pthread_attr_setstacksize(&attr, stack_size);
    }

    // Set priority (map FreeRTOS priority to pthread priority)
    struct sched_param sp;
    sp.sched_priority = uxPriority;
    pthread_attr_setschedparam(&attr, &sp);

    // Create thread
    int ret = pthread_create(thread, &attr, (void*(*)(void*))pxTaskCode, pvParameters);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        fprintf(stderr, "[FreeRTOS Mock] Failed to create task '%s': %d\n", pcName, ret);
        free(thread);
        return pdFAIL;
    }

    if (pxCreatedTask) {
        *pxCreatedTask = (TaskHandle_t)thread;
    }

    printf("[FreeRTOS Mock] ✅ Created task '%s' (priority %lu, core %d)\n",
           pcName, (unsigned long)uxPriority, xCoreID);

    return pdPASS;
}

// Task delay
void vTaskDelay(const TickType_t xTicksToDelay) {
    usleep(xTicksToDelay * 1000);  // Convert milliseconds to microseconds
}

// Task deletion
void vTaskDelete(TaskHandle_t xTask) {
    if (xTask) {
        pthread_t* thread = (pthread_t*)xTask;
        pthread_cancel(*thread);
        free(thread);
    }
}

// Task utilities
char* pcTaskGetName(TaskHandle_t xTask) {
    static char name[32] = "unknown";
    // In pthreads, we can't easily get the task name
    // Just return a placeholder
    snprintf(name, sizeof(name), "task_%p", xTask);
    return name;
}

UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask) {
    struct sched_param sp;
    int policy;
    pthread_t thread;

    if (xTask) {
        thread = *((pthread_t*)xTask);
    } else {
        thread = pthread_self();
    }

    pthread_getschedparam(thread, &policy, &sp);
    return sp.sched_priority;
}

void vTaskSuspend(TaskHandle_t xTask) {
    // pthreads doesn't support suspend/resume easily
    fprintf(stderr, "[FreeRTOS Mock] Warning: vTaskSuspend not fully implemented\n");
}

void vTaskResume(TaskHandle_t xTask) {
    fprintf(stderr, "[FreeRTOS Mock] Warning: vTaskResume not fully implemented\n");
}

// Semaphore creation
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    pthread_mutex_t* mutex = malloc(sizeof(pthread_mutex_t));
    if (!mutex) return NULL;

    pthread_mutex_init(mutex, NULL);
    return (SemaphoreHandle_t)mutex;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    // Binary semaphore can be implemented as a mutex
    return xSemaphoreCreateMutex();
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount) {
    // For now, just return a mutex (simplified)
    fprintf(stderr, "[FreeRTOS Mock] Warning: Counting semaphores not fully implemented\n");
    return xSemaphoreCreateMutex();
}

// Semaphore operations
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    if (!xSemaphore) return pdFAIL;

    pthread_mutex_t* mutex = (pthread_mutex_t*)xSemaphore;

    if (xTicksToWait == portMAX_DELAY) {
        pthread_mutex_lock(mutex);
        return pdPASS;
    } else if (xTicksToWait == 0) {
        return pthread_mutex_trylock(mutex) == 0 ? pdPASS : pdFAIL;
    } else {
        // macOS doesn't have pthread_mutex_timedlock, use trylock with delay
        // This is a simplification - for proper timed waits, we'd need a condvar
        uint32_t elapsed = 0;
        while (elapsed < xTicksToWait) {
            if (pthread_mutex_trylock(mutex) == 0) {
                return pdPASS;
            }
            usleep(1000);  // 1ms
            elapsed++;
        }
        return pdFAIL;
    }
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    if (!xSemaphore) return pdFAIL;
    pthread_mutex_t* mutex = (pthread_mutex_t*)xSemaphore;
    return pthread_mutex_unlock(mutex) == 0 ? pdPASS : pdFAIL;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t* pxHigherPriorityTaskWoken) {
    // In simulator, ISR context doesn't apply
    return xSemaphoreGive(xSemaphore);
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    if (xSemaphore) {
        pthread_mutex_t* mutex = (pthread_mutex_t*)xSemaphore;
        pthread_mutex_destroy(mutex);
        free(mutex);
    }
}

// Queue operations
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize) {
    // Simplified: not implementing full queue semantics
    fprintf(stderr, "[FreeRTOS Mock] Warning: Queues not fully implemented\n");
    return NULL;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait) {
    return pdFAIL;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait) {
    return pdFAIL;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue) {
    return 0;
}

void vQueueDelete(QueueHandle_t xQueue) {
    // Nothing to delete
}

// CPU ID
UBaseType_t xPortGetCoreID(void) {
    // macOS doesn't have core affinity in the same way
    // Just return 0
    return 0;
}

// Critical sections (simplified)
void vTaskSuspendAll(void) {
    // In a real implementation, this would disable task switching
}

BaseType_t xTaskResumeAll(void) {
    return pdTRUE;
}

// Scheduler (pthread runs tasks immediately, so this is simplified)
void vTaskStartScheduler(void) {
    printf("[FreeRTOS Mock] Scheduler started (pthread-based)\n");
}

void vTaskEndScheduler(void) {
    printf("[FreeRTOS Mock] Scheduler ended\n");
}

void taskYIELD(void) {
    // Hint to scheduler that we want to yield
    sched_yield();
}
