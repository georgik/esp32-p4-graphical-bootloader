/**
 * @file board_init.h
 * @brief Board initialization interface for LVGL-based bootloader
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize display hardware and LVGL
 *
 * This function handles board-specific initialization including:
 * - Creating esp_lcd panel/io handles via BSP
 * - Initializing backlight
 * - Configuring LVGL with optimized IRAM buffers
 * - Setting up display for LVGL rendering
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t board_init_display(void);

/**
 * @brief Get LVGL display handle
 *
 * @return LVGL display handle or NULL if not initialized
 */
lv_display_t* board_get_lvgl_display(void);

/**
 * @brief Initialize LVGL port (if additional customization needed)
 *
 * BSP already initializes LVGL, but this function can be used for
 * additional custom configurations.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t board_init_lvgl_port(void);

#ifdef __cplusplus
}
#endif
