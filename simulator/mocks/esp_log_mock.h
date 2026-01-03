/**
 * @file esp_log_mock.h
 * @brief Mock implementation of ESP logging functions
 */

#ifndef ESP_LOG_MOCK_H
#define ESP_LOG_MOCK_H

#include <stdio.h>
#include "esp_system_mock.h"

#ifdef __cplusplus
extern "C" {
#endif

// ANSI color codes for terminal output
#define ESP_LOG_COLOR_RESET   "\033[0m"
#define ESP_LOG_COLOR_ERROR   "\033[0;31m"
#define ESP_LOG_COLOR_WARN    "\033[0;33m"
#define ESP_LOG_COLOR_INFO    "\033[0;34m"
#define ESP_LOG_COLOR_DEBUG   "\033[0;90m"
#define ESP_LOG_COLOR_VERBOSE "\033[0;37m"

// Log levels
typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

// Logging macros with colors and timestamps
#define ESP_LOGE(tag, fmt, ...) \
    do { \
        printf(ESP_LOG_COLOR_ERROR "[E] %s: " fmt ESP_LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__); \
    } while(0)

#define ESP_LOGW(tag, fmt, ...) \
    do { \
        printf(ESP_LOG_COLOR_WARN "[W] %s: " fmt ESP_LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__); \
    } while(0)

#define ESP_LOGI(tag, fmt, ...) \
    do { \
        printf(ESP_LOG_COLOR_INFO "[I] %s: " fmt ESP_LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__); \
    } while(0)

#define ESP_LOGD(tag, fmt, ...) \
    do { \
        printf(ESP_LOG_COLOR_DEBUG "[D] %s: " fmt ESP_LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__); \
    } while(0)

#define ESP_LOGV(tag, fmt, ...) \
    do { \
        printf(ESP_LOG_COLOR_VERBOSE "[V] %s: " fmt ESP_LOG_COLOR_RESET "\n", tag, ##__VA_ARGS__); \
    } while(0)

// Buffer hex dump macro
#define ESP_LOG_BUFFER_HEX(tag, buffer, len) \
    do { \
        printf(ESP_LOG_COLOR_DEBUG "[D] %s: Buffer dump (%zu bytes)\n" ESP_LOG_COLOR_RESET, tag, (size_t)(len)); \
        const uint8_t* _buf = (const uint8_t*)(buffer); \
        size_t _len = (len); \
        for (size_t i = 0; i < _len; i += 16) { \
            printf("  %04zx: ", i); \
            for (size_t j = 0; j < 16 && i + j < _len; j++) { \
                printf("%02x ", _buf[i + j]); \
            } \
            printf("\n"); \
        } \
    } while(0)

// Error name conversion
static inline const char* esp_err_to_name(esp_err_t err) {
    switch(err) {
        case ESP_OK: return "OK";
        case ESP_FAIL: return "Fail";
        case ESP_ERR_NO_MEM: return "No memory";
        case ESP_ERR_INVALID_ARG: return "Invalid argument";
        case ESP_ERR_INVALID_STATE: return "Invalid state";
        case ESP_ERR_NOT_FOUND: return "Not found";
        case ESP_ERR_NOT_SUPPORTED: return "Not supported";
        case ESP_ERR_TIMEOUT: return "Timeout";
        case ESP_ERR_INVALID_SIZE: return "Invalid size";
        default: return "Unknown error";
    }
}

// Log level control
void esp_log_level_set(const char* tag, esp_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif // ESP_LOG_MOCK_H
