/**
 * @file lvgl_sdl_init.c
 * @brief LVGL SDL2 display driver implementation
 */

#include "lvgl_sdl_init.h"
#include "../mocks/bsp_mock.h"
#include "../mocks/esp_log_mock.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

// LVGL headers - include after mocks
#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>

static const char* TAG = "lvgl_sdl";

// Display dimensions (match ESP32-P4)
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 600

static lv_display_t* display = NULL;
static SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* texture = NULL;
static lv_indev_t* mouse_indev = NULL;

// Mouse state
static struct {
    int x;
    int y;
    bool left_button;
} mouse_state = {0, 0, false};

// Read mouse input for LVGL
static void mouse_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;

    data->point.x = mouse_state.x;
    data->point.y = mouse_state.y;
    data->state = mouse_state.left_button ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// Flush callback for LVGL
static void sdl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    static uint32_t flush_count = 0;
    flush_count++;

    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;

    // Log first 100 flushes with full details to debug partial updates
    if (flush_count <= 100) {
        ESP_LOGI(TAG, "Flush #%u: area=(%d,%d)-(%d,%d) size=%dx%d px_map=%p",
                 flush_count, area->x1, area->y1, area->x2, area->y2, width, height, (void*)px_map);
    }

    // Update the SDL texture with the new pixel data
    if (texture && renderer) {
        SDL_Rect rect;
        rect.x = area->x1;
        rect.y = area->y1;
        rect.w = width;
        rect.h = height;

        // CRITICAL FIX for DIRECT mode:
        // In DIRECT mode, px_map points to the ENTIRE screen buffer, not just the dirty region.
        // SDL_UpdateTexture expects the pointer to be at the START of the region being updated.
        // We must calculate the offset to the first pixel of the dirty region.
        uint8_t* region_start = px_map + (area->y1 * SCREEN_WIDTH + area->x1) * 2;

        int stride = SCREEN_WIDTH * 2;  // Full screen stride (2 bytes per pixel for RGB565)

        if (flush_count <= 100) {
            ESP_LOGI(TAG, "  → SDL_UpdateTexture(rect=%d,%d %dx%d, buf=%p, stride=%d)",
                     rect.x, rect.y, rect.w, rect.h, (void*)region_start, stride);
        }

        // CRITICAL: Update ONLY the dirty region
        // region_start points to the first pixel of the dirty region within the full buffer
        int ret = SDL_UpdateTexture(texture, &rect, region_start, stride);

        if (ret != 0 && flush_count <= 100) {
            ESP_LOGE(TAG, "  ✗ SDL_UpdateTexture FAILED: %s", SDL_GetError());
        }

        // Render the texture to the screen
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        if (flush_count <= 100) {
            ESP_LOGI(TAG, "  ← Render complete");
        }
    }

    lv_display_flush_ready(disp);
}

// Tick handler
static uint32_t tick_get_cb(void) {
    static uint64_t start_ms = 0;
    if (start_ms == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        start_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return now_ms - start_ms;
}

esp_err_t init_lvgl_sdl(void) {
    ESP_LOGI(TAG, "Initializing LVGL with SDL2 backend...");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        ESP_LOGE(TAG, "Failed to initialize SDL2: %s", SDL_GetError());
        return ESP_FAIL;
    }

    // Create window
    window = SDL_CreateWindow(
        "ESP32-P4 Bootloader Simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        ESP_LOGE(TAG, "Failed to create SDL window: %s", SDL_GetError());
        SDL_Quit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SDL2 window created: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        ESP_LOGE(TAG, "Failed to create SDL renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ESP_FAIL;
    }

    // Create texture for rendering (RGB565 format)
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (!texture) {
        ESP_LOGE(TAG, "Failed to create SDL texture: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SDL2 renderer and texture created");

    // Initialize LVGL
    lv_init();

    // Register tick callback
    lv_tick_set_cb(tick_get_cb);

    ESP_LOGW(TAG, "Creating LVGL display with software rendering");

    // Create display
    display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!display) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return ESP_FAIL;
    }

    // Set flush callback
    lv_display_set_flush_cb(display, sdl_flush_cb);

    // Create draw buffer - try DIRECT mode instead of PARTIAL to avoid rendering issues
    static uint8_t draw_buf[SCREEN_WIDTH * SCREEN_HEIGHT * 2];
    lv_display_set_buffers(display, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_DIRECT);

    ESP_LOGI(TAG, "Using DIRECT rendering mode to avoid partial buffer issues");

    // Set default display
    lv_display_set_default(display);

    // Set to BSP active display
    bsp_set_active_display((struct lv_display_t*)display);

    // Create mouse input device
    mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse_indev, mouse_read_cb);

    ESP_LOGI(TAG, "✅ LVGL initialized successfully");
    ESP_LOGI(TAG, "Display: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
    ESP_LOGI(TAG, "Input device: mouse pointer");

    return ESP_OK;
}

void lvgl_tick_handler(void) {
    static uint32_t tick_count = 0;
    static uint32_t last_handler_duration_ms = 0;
    tick_count++;

    // Log first 20 ticks with detailed timing to detect early freeze
    if (tick_count <= 20) {
        ESP_LOGI(TAG, "LVGL tick #%u starting", tick_count);
    }

    // Log every 10000 ticks (every ~1 second)
    if (tick_count % 10000 == 0) {
        ESP_LOGD(TAG, "LVGL tick handler %u times (last lv_timer_handler took %u ms)",
                 tick_count, last_handler_duration_ms);
    }

    // LVGL tick is handled via callback (tick_get_cb)
    lv_tick_inc(5);

    // Track how long lv_timer_handler takes - detect infinite loops
    struct timespec ts_before, ts_after;
    clock_gettime(CLOCK_MONOTONIC, &ts_before);
    uint64_t before_ms = ts_before.tv_sec * 1000 + ts_before.tv_nsec / 1000000;

    // This is where LVGL processes all tasks, animations, redraws, etc.
    lv_timer_handler();

    clock_gettime(CLOCK_MONOTONIC, &ts_after);
    uint64_t after_ms = ts_after.tv_sec * 1000 + ts_after.tv_nsec / 1000000;
    last_handler_duration_ms = (uint32_t)(after_ms - before_ms);

    // Log first 20 ticks with timing
    if (tick_count <= 20) {
        ESP_LOGI(TAG, "LVGL tick #%u completed in %u ms", tick_count, last_handler_duration_ms);
    }

    // Warn if lv_timer_handler takes more than 100ms - indicates infinite loop
    if (last_handler_duration_ms > 100) {
        ESP_LOGW(TAG, "⚠️  lv_timer_handler() took %u ms (tick=%u) - possible infinite loop!",
                 last_handler_duration_ms, tick_count);
    }

    // Critical warning if it takes more than 1 second
    if (last_handler_duration_ms > 1000) {
        ESP_LOGE(TAG, "🚨 CRITICAL: lv_timer_handler() took %u ms - LVGL is HUNG!",
                 last_handler_duration_ms);
    }
}

struct lv_display_t* lvgl_sdl_get_display(void) {
    return (struct lv_display_t*)display;
}

// Process SDL events
bool lvgl_sdl_process_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        else if (event.type == SDL_MOUSEMOTION) {
            mouse_state.x = event.motion.x;
            mouse_state.y = event.motion.y;
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouse_state.left_button = true;
                mouse_state.x = event.button.x;
                mouse_state.y = event.button.y;
            }
        }
        else if (event.type == SDL_MOUSEBUTTONUP) {
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouse_state.left_button = false;
                mouse_state.x = event.button.x;
                mouse_state.y = event.button.y;
            }
        }
    }
    return true;
}

// Cleanup SDL resources
void lvgl_sdl_cleanup(void) {
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}
