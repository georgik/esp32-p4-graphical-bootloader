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
#include <stddef.h>
#include "esp_err.h"

// Get UBaseType_t definition from FreeRTOS
#ifdef __SIMULATOR_BUILD__
    #include "freertos_mock.h"
#else
    #include "freertos/FreeRTOS.h"
    #include "freertos/projdefs.h"
#endif

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
 * @brief Get current queue depth
 *
 * Returns the current number of messages in the UI update queue.
 * Useful for monitoring queue usage and preventing overflow.
 *
 * @return Number of messages currently in queue
 */
UBaseType_t ui_update_get_depth(void);

/**
 * @brief Get queue statistics
 *
 * Returns statistics about queue operations for debugging.
 *
 * @param[out] total_sent Total messages sent
 * @param[out] total_dropped Total messages dropped (queue full or skipped)
 * @param[out] total_processed Total messages processed
 * @param[out] max_depth Maximum queue depth observed
 * @return ESP_OK on success
 */
esp_err_t ui_update_get_stats(uint32_t* total_sent, uint32_t* total_dropped,
                               uint32_t* total_processed, UBaseType_t* max_depth);

/**
 * @brief Reset queue statistics
 *
 * Resets all statistics counters to zero. Useful for tracking individual operations.
 */
void ui_update_reset_stats(void);

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
