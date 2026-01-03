/**
 * @file firmware_flasher_mock.c
 * @brief Mock implementation of firmware flasher
 */

#include "esp_log_mock.h"
#include "esp_system_mock.h"

static const char* TAG = "firmware_flasher_mock";

esp_err_t firmware_flasher_init(void) {
    ESP_LOGI(TAG, "Mock firmware flasher initialized");
    return ESP_OK;
}

esp_err_t firmware_flasher_start(const char* firmware_path) {
    (void)firmware_path;
    ESP_LOGI(TAG, "Mock firmware flasher started for: %s", firmware_path);
    return ESP_OK;
}
