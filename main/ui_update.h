/**
 * @file ui_update.h
 * @brief UI update queue for decoupling background tasks from LVGL operations
 *
 * This module provides a thread-safe queue for sending UI updates from background
 * tasks (e.g., flash_task) to the LVGL task. This ensures LVGL functions are only
 * called from the dedicated LVGL task, preventing race conditions and watchdog timeouts.
 */

#ifndef UI_UPDATE_H
#define UI_UPDATE_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// UI update message types
typedef enum {
    UI_UPDATE_PROGRESS,           // Update progress bar and label
    UI_UPDATE_STATUS,             // Update status label only
    UI_UPDATE_SHOW_MODAL,         // Show modal dialog
    UI_UPDATE_HIDE_MODAL,         // Hide modal dialog
    UI_UPDATE_REFRESH_SCREEN,     // Refresh main screen
} ui_update_type_t;

// UI update message structure
typedef struct {
    ui_update_type_t type;
    union {
        struct {
            uint32_t firmware_index;    // 1-based index
            uint8_t percentage;          // 0-100
            const char* operation;       // "Erasing", "Flashing", "Finalizing", etc.
        } progress;
        struct {
            const char* message;         // Status message text
        } status;
        struct {
            const char* title;           // Modal title
            const char* message;         // Modal message
        } modal_show;
        struct {
            uint32_t delay_ms;           // Delay before refresh
        } refresh;
    } data;
} ui_update_message_t;

/**
 * @brief Initialize UI update system
 *
 * Creates the FreeRTOS queue for UI updates. Must be called before any other
 * ui_update functions.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ui_update_init(void);

/**
 * @brief Send UI update from any task (non-blocking)
 *
 * Queues a UI update message to be processed by the LVGL task. This function
 * is non-blocking with a 1ms timeout. If the queue is full, the message is
 * dropped and ESP_ERR_TIMEOUT is returned.
 *
 * @param msg Pointer to UI update message
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if queue full, error code otherwise
 */
esp_err_t ui_update_send(ui_update_message_t* msg);

/**
 * @brief Process pending UI updates (called from lvgl_task only)
 *
 * Processes all pending UI updates from the queue. Should only be called from
 * the LVGL task to ensure thread safety. Batches multiple updates together
 * for efficiency.
 */
void ui_update_process(void);

/**
 * @brief Deinitialize UI update system
 *
 * Deletes the UI update queue and frees resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ui_update_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // UI_UPDATE_H
