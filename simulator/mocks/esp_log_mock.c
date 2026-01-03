/**
 * @file esp_log_mock.c
 * @brief Mock implementation of ESP logging functions
 */

#include "esp_log_mock.h"
#include <string.h>

static esp_log_level_t global_log_level = ESP_LOG_INFO;

void esp_log_level_set(const char* tag, esp_log_level_t level) {
    // In simulator, just set global level
    // In real implementation, would maintain per-tag levels
    global_log_level = level;
}
