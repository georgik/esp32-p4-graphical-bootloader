/**
 * @file main.c
 * @brief ESP32-P4 LVGL-based graphical bootloader
 *
 * Optimized for IRAM framebuffer to prevent PSRAM contention
 * during SD card operations.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp/esp-bsp.h"

// Include our modules
#include "board_init.h"
#include "lvgl_bootloader.h"
#include "sd_ota.h"
#include "vdma_protection.h"

static const char *TAG = "main";

// LVGL task handle
static TaskHandle_t lvgl_task_handle = NULL;
static TaskHandle_t ota_task_handle = NULL;

// Display update semaphore for dual-core operation
static SemaphoreHandle_t display_mutex = NULL;

// VDMA display protection state
static volatile bool display_protect_mode = false;
static uint32_t display_refresh_timestamp = 0;

// VDMA display protection functions (extern accessible)
void vdma_enable_display_protection(void)
{
    display_protect_mode = true;
    display_refresh_timestamp = xTaskGetTickCount();
    ESP_LOGD(TAG, "VDMA display protection enabled - blocking intensive operations");
}

void vdma_disable_display_protection(void)
{
    display_protect_mode = false;
    ESP_LOGD(TAG, "VDMA display protection disabled - allowing intensive operations");
}

bool vdma_is_display_protected(void)
{
    return display_protect_mode;
}

void vdma_ensure_display_refresh(uint32_t min_interval_ms)
{
    TickType_t current_tick = xTaskGetTickCount();
    TickType_t elapsed = current_tick - display_refresh_timestamp;

    if (elapsed < pdMS_TO_TICKS(min_interval_ms)) {
        // Display hasn't refreshed recently, wait for it
        TickType_t wait_time = pdMS_TO_TICKS(min_interval_ms) - elapsed;
        ESP_LOGD(TAG, "VDMA waiting %d ms for display refresh", wait_time);
        vTaskDelay(wait_time);
    }

    display_refresh_timestamp = xTaskGetTickCount();
}

// LVGL timer handler task
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started on core %d with priority %d", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    while (1) {
        // VDMA PROTECTION: Enable display protection during LVGL rendering
        vdma_enable_display_protection();

        // CRITICAL: Give LVGL highest priority for display stability
        lv_timer_handler();

        // VDMA PROTECTION: Allow display refresh to complete before yielding
        vTaskDelay(pdMS_TO_TICKS(5));  // Shorter delay for more responsive VDMA coordination

        // VDMA PROTECTION: Disable protection after LVGL completes
        vdma_disable_display_protection();

        // Additional small delay to ensure other tasks can run
        vTaskDelay(pdMS_TO_TICKS(3));  // Total 8ms delay for ~125fps with VDMA safety
    }
}

// OTA monitoring task
static void ota_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "OTA monitor task started on core %d", xPortGetCoreID());

    while (1) {
        // Update OTA progress if operation is in progress
        if (is_ota_in_progress()) {
            // VDMA AWARE: Check if display is protected before doing OTA monitoring
            if (vdma_is_display_protected()) {
                // Display is currently protected, wait longer
                vTaskDelay(pdMS_TO_TICKS(50));
            } else {
                // Display not protected, safe to do OTA monitoring
                vTaskDelay(pdMS_TO_TICKS(20));
            }

            // VDMA COORDINATION: Ensure display has time to refresh between checks
            vdma_ensure_display_refresh(16);  // Ensure at least 16ms between operations (~60fps)
        } else {
            // No OTA in progress, longer delay to save CPU
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// OTA progress callback
static void ota_progress_callback(uint8_t progress)
{
    update_progress_bar(progress);
}

// OTA status callback
static void ota_status_callback(const char *status)
{
    update_status(status);
}

static esp_err_t initialize_system(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-P4 LVGL bootloader...");

    // Initialize BSP (includes LVGL initialization)
    esp_err_t ret = board_init_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Display initialized successfully");

    // Initialize LVGL bootloader UI
    ret = lvgl_bootloader_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL bootloader: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize SD card OTA
    ret = sd_ota_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card OTA initialization failed: %s", esp_err_to_name(ret));
        update_status("Warning: SD card not available");
    } else {
        // Set OTA callbacks
        sd_ota_set_progress_callback(ota_progress_callback);
        sd_ota_set_status_callback(ota_status_callback);
        update_status("Ready - SD card available");
    }

    ESP_LOGI(TAG, "System initialization complete");
    return ESP_OK;
}

static void start_tasks(void)
{
    // Create display mutex
    display_mutex = xSemaphoreCreateMutex();
    if (!display_mutex) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return;
    }

    // Create LVGL task on Core 1 (HIGHEST priority for display stability)
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_task,
        "lvgl_task",
        4096,           // Stack size
        NULL,           // Parameters
        configMAX_PRIORITIES - 1,  // HIGHEST priority - display stability
        &lvgl_task_handle,
        1               // Core 1 for display operations
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return;
    }

    // Create OTA monitor task on Core 0 (AGGRESSIVE priority for display protection)
    ret = xTaskCreatePinnedToCore(
        ota_monitor_task,
        "ota_monitor",
        3072,           // Stack size
        NULL,           // Parameters
        configMAX_PRIORITIES - 10, // AGGRESSIVE: Much lower priority (was -5) for maximum display protection
        &ota_task_handle,
        0               // Core 0 for I/O operations
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA monitor task");
        return;
    }

    ESP_LOGI(TAG, "Tasks started successfully");
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 LVGL Graphical Bootloader starting...");
    ESP_LOGI(TAG, "Running on core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free IRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT));
    ESP_LOGI(TAG, "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize system
    esp_err_t ret = initialize_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Start background tasks
    start_tasks();

    ESP_LOGI(TAG, "Bootloader initialized successfully");
    ESP_LOGI(TAG, "System ready - awaiting user input");

    // Initial status update
    update_status("System ready - select a demo");

    // Main task just waits
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Log memory stats periodically
        static int counter = 0;
        if (++counter >= 30) {  // Every 30 seconds
            counter = 0;
            ESP_LOGD(TAG, "Memory stats - Free: %d, IRAM: %d, PSRAM: %d",
                    esp_get_free_heap_size(),
                    heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT),
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
}