/*
 * Factory Application for ESP32-P4 Custom Bootloader
 * This is a minimal application that provides a fallback when no other app is selected
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "factory_app";

void app_main(void)
{
    ESP_LOGI(TAG, "Factory application started");
    ESP_LOGI(TAG, "This is the fallback application for the ESP32-P4 custom bootloader");
    ESP_LOGI(TAG, "Press reset button to return to bootloader");

    // Simple factory application loop
    int counter = 0;
    while (1) {
        ESP_LOGI(TAG, "Factory app loop iteration: %d", counter++);

        // Simple delay - in a real factory app, you might have:
        // - Diagnostic tests
        // - Device information display
        // - Recovery options
        // - System status indicators
        // - Device diagnostics
        // - Hardware testing
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}