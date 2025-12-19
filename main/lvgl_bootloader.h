/**
 * @file lvgl_bootloader.h
 * @brief LVGL-based graphical bootloader interface
 *
 * Optimized for ESP32-P4 with IRAM framebuffer and efficient rendering
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Screen IDs
typedef enum {
    SCREEN_MAIN = 0,
    SCREEN_DEMO,
    SCREEN_SETTINGS,
    SCREEN_FIRMWARE_SELECTOR,
    SCREEN_COUNT
} screen_id_t;

/**
 * @brief Initialize LVGL bootloader UI
 *
 * Creates all UI elements, styles, and sets up the main screen.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t lvgl_bootloader_init(void);

/**
 * @brief Deinitialize LVGL bootloader UI
 *
 * Cleans up all UI elements, styles, and frees memory.
 */
void lvgl_bootloader_deinit(void);

/**
 * @brief Switch to a different screen
 *
 * @param screen_id ID of the screen to switch to
 */
void switch_screen(screen_id_t screen_id);

/**
 * @brief Create and show progress bar for operations
 *
 * Creates a progress bar if it doesn't exist and shows it.
 */
void create_progress_bar(void);

/**
 * @brief Update progress bar value
 *
 * @param percent Progress percentage (0-100)
 */
void update_progress_bar(uint8_t percent);

/**
 * @brief Show or hide progress bar
 *
 * @param show True to show, false to hide
 */
void show_progress(bool show);

/**
 * @brief Update status text
 *
 * @param status New status text to display
 */
void update_status(const char* status);

/**
 * @brief Set OTA operation state
 *
 * Enables/disables UI elements based on OTA operation state.
 *
 * @param in_progress True if OTA is in progress
 */
void set_ota_in_progress(bool in_progress);

/**
 * @brief Check if OTA operation is in progress
 *
 * @return True if OTA is in progress
 */
bool is_ota_in_progress(void);

/**
 * @brief Initialize firmware selector screen
 *
 * Initializes the firmware selector and scans for firmware files.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t init_firmware_selector_screen(void);

/**
 * @brief Show firmware selector screen
 *
 * Displays the firmware selection interface.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t show_firmware_selector_screen(void);

/**
 * @brief Hide firmware selector screen
 *
 * Hides the firmware selection interface.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t hide_firmware_selector_screen(void);

#ifdef __cplusplus
}
#endif