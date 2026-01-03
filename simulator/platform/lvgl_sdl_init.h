/**
 * @file lvgl_sdl_init.h
 * @brief LVGL SDL2 display initialization for simulator
 */

#ifndef LVGL_SDL_INIT_H
#define LVGL_SDL_INIT_H

#include "esp_system_mock.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for LVGL display
struct lv_display_t;

// Initialize LVGL with SDL2 display driver
esp_err_t init_lvgl_sdl(void);

// Tick handler for LVGL
void lvgl_tick_handler(void);

// Get the active SDL display
struct lv_display_t* lvgl_sdl_get_display(void);

// Process SDL events (returns false if quit requested)
bool lvgl_sdl_process_events(void);

// Cleanup SDL resources
void lvgl_sdl_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // LVGL_SDL_INIT_H
