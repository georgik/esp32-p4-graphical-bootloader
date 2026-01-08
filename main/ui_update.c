/**
 * @file ui_update.c
 * @brief UI update queue implementation for decoupling background tasks from LVGL
 */

#include "ui_update.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl_bootloader.h"

static const char* TAG = "ui_update";

// Queue handle for UI updates
static QueueHandle_t ui_update_queue = NULL;
static const uint32_t UI_UPDATE_QUEUE_SIZE = 20;  // Queue depth

// Forward declarations for LVGL functions
extern void refresh_main_screen(void);
extern void update_progress_bar(uint8_t percent);

// LVGL objects (extern to access directly)
extern lv_obj_t* status_label;

// LVGL mutex (extern for locking)
extern void lock_display(void);
extern void unlock_display(void);

// Initialize UI update system
esp_err_t ui_update_init(void)
{
    if (ui_update_queue != NULL) {
        ESP_LOGW(TAG, "UI update queue already initialized");
        return ESP_OK;
    }

    ui_update_queue = xQueueCreate(UI_UPDATE_QUEUE_SIZE, sizeof(ui_update_message_t));
    if (ui_update_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI update queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UI update queue initialized (depth=%u)", UI_UPDATE_QUEUE_SIZE);
    return ESP_OK;
}

// Send UI update from any task (non-blocking)
esp_err_t ui_update_send(ui_update_message_t* msg)
{
    if (ui_update_queue == NULL) {
        ESP_LOGE(TAG, "UI update queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Non-blocking send with 1ms timeout
    // If queue is full, drop the update (better than blocking flash task)
    BaseType_t ret = xQueueSend(ui_update_queue, msg, pdMS_TO_TICKS(1));
    if (ret != pdPASS) {
        // Queue full - this is OK, means lvgl_task is slow
        // Don't log to avoid spam during rapid updates
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

// Process pending UI updates (called from lvgl_task only)
void ui_update_process(void)
{
    if (ui_update_queue == NULL) {
        return;
    }

    // Check queue depth - if too many messages, drop some to prevent overflow
    UBaseType_t queue_depth = uxQueueMessagesWaiting(ui_update_queue);
    if (queue_depth > 15) {  // 75% full
        ESP_LOGW(TAG, "UI update queue nearly full (%u messages), dropping oldest messages", queue_depth);

        // Drop oldest 5 messages to make room
        for (int i = 0; i < 5 && queue_depth > 0; i++) {
            ui_update_message_t dropped_msg;
            if (xQueueReceive(ui_update_queue, &dropped_msg, 0) == pdPASS) {
                queue_depth--;
                ESP_LOGD(TAG, "Dropped UI update type=%d", dropped_msg.type);
            }
        }
    }

    // Process all pending updates (drain queue)
    // This ensures we batch multiple updates together
    ui_update_message_t msg;
    int processed = 0;
    const int MAX_UPDATES_PER_CYCLE = 5;  // Reduced from 10 to prevent spending too long in LVGL

    while (processed < MAX_UPDATES_PER_CYCLE) {
        if (xQueueReceive(ui_update_queue, &msg, 0) != pdPASS) {
            break;  // Queue empty
        }

        // Process the message (call LVGL functions directly)
        switch (msg.type) {
            case UI_UPDATE_PROGRESS:
                {
                    // Throttle firmware selector updates to reduce LVGL load
                    // Only update every 5% to prevent invalidation storms
                    static uint8_t last_percentage = 0;
                    uint8_t current_percent = msg.data.progress.percentage;

                    // Always update main status bar (it's simpler)
                    update_progress_bar(current_percent);

                    // Only update firmware selector if percentage changed by 5% or at 0%/100%
                    if ((current_percent == 0) || (current_percent == 100) ||
                        (current_percent >= last_percentage + 5) ||
                        (current_percent < last_percentage && current_percent % 5 == 0)) {

                        extern void firmware_selector_update_progress_bar(uint8_t percentage);
                        firmware_selector_update_progress_bar(current_percent);
                        last_percentage = current_percent;
                    }
                }
                break;

            case UI_UPDATE_STATUS:
                // Update status label directly with mutex protection
                if (status_label) {
                    lock_display();
                    lv_label_set_text(status_label, msg.data.status.message);
                    unlock_display();
                }
                break;

            case UI_UPDATE_SHOW_MODAL:
                // TODO: Implement modal show
                ESP_LOGW(TAG, "Modal show not yet implemented");
                break;

            case UI_UPDATE_HIDE_MODAL:
                // TODO: Implement modal hide
                ESP_LOGW(TAG, "Modal hide not yet implemented");
                break;

            case UI_UPDATE_REFRESH_SCREEN:
                // Refresh main screen
                refresh_main_screen();
                break;

            default:
                ESP_LOGW(TAG, "Unknown UI update type: %d", msg.type);
                break;
        }

        processed++;
    }

    // Optional: Warn if queue is constantly full
    if (processed == MAX_UPDATES_PER_CYCLE) {
        UBaseType_t remaining = uxQueueMessagesWaiting(ui_update_queue);
        if (remaining > 0) {
            ESP_LOGW(TAG, "UI update queue has %u pending messages after processing %u",
                     remaining, MAX_UPDATES_PER_CYCLE);
        }
    }
}

// Deinitialize UI update system
esp_err_t ui_update_deinit(void)
{
    if (ui_update_queue != NULL) {
        vQueueDelete(ui_update_queue);
        ui_update_queue = NULL;
        ESP_LOGI(TAG, "UI update queue deinitialized");
    }
    return ESP_OK;
}
