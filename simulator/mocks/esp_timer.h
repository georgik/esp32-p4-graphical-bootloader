/**
 * @file esp_timer.h
 * @brief Mock implementation of ESP timer APIs
 */

#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_timer* esp_timer_handle_t;

typedef enum {
    ESP_TIMER_TASK,
} esp_timer_dispatch_t;

typedef void (*esp_timer_cb_t)(void* arg);

typedef struct {
    esp_timer_cb_t callback;
    void* arg;
    esp_timer_dispatch_t dispatch_method;
    const char* name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

// Simple mock implementations
static inline int esp_timer_create(const esp_timer_create_args_t* args,
                                   esp_timer_handle_t* out_handle) {
    (void)args;
    (void)out_handle;
    return 0;  // Success
}

static inline int esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    (void)timer;
    (void)timeout_us;
    return 0;  // Success
}

static inline int esp_timer_stop(esp_timer_handle_t timer) {
    (void)timer;
    return 0;  // Success
}

static inline int esp_timer_delete(esp_timer_handle_t timer) {
    (void)timer;
    return 0;  // Success
}

static inline uint64_t esp_timer_get_time(void) {
    // Return time in microseconds
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // ESP_TIMER_H
