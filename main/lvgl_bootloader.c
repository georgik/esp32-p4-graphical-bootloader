/**
 * @file lvgl_bootloader.c
 * @brief LVGL-based graphical bootloader UI
 *
 * Optimized for ESP32-P4 with IRAM framebuffer and efficient rendering
 */

#include "lvgl_bootloader.h"
#include "board_init.h"
#include "esp_log.h"
#include "lvgl.h"
#include "sd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "lvgl_bootloader";

// Display mutex for thread safety (fallback if BSP doesn't provide lock)
static SemaphoreHandle_t lvgl_mutex = NULL;

// UI Elements
static lv_obj_t *main_screen = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *demo_btns[4] = {0};
static lv_obj_t *status_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;

// Screen IDs
static int current_screen = SCREEN_MAIN;
static lv_obj_t *screens[3] = {0}; // MAIN, DEMO, SETTINGS

// Style objects
static lv_style_t style_title;
static lv_style_t style_btn;
static lv_style_t style_btn_pressed;
static lv_style_t style_status;

// Progress tracking
static bool ota_in_progress = false;

// Initialize display mutex
static void init_display_mutex(void)
{
    if (!lvgl_mutex) {
        lvgl_mutex = xSemaphoreCreateMutex();
        if (!lvgl_mutex) {
            ESP_LOGE(TAG, "Failed to create LVGL mutex");
        }
    }
}

// Lock display for thread safety
static void lock_display(void)
{
    if (lvgl_mutex) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    }
}

// Unlock display
static void unlock_display(void)
{
    if (lvgl_mutex) {
        xSemaphoreGive(lvgl_mutex);
    }
}

// LVGL event callbacks
static void demo_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint32_t btn_id = (uint32_t)lv_obj_get_user_data(btn);

    ESP_LOGI(TAG, "Demo button %lu pressed", btn_id);

    switch(btn_id) {
        case 0:
            // Demo 1: SD Card OTA
            ESP_LOGI(TAG, "Starting SD Card OTA...");
            sd_ota_start();
            break;

        case 1:
            // Demo 2: Another application
            switch_screen(SCREEN_DEMO);
            break;

        case 2:
            // Demo 3: Another application
            switch_screen(SCREEN_DEMO);
            break;

        case 3:
            // Demo 4: Settings
            switch_screen(SCREEN_SETTINGS);
            break;
    }
}

static void back_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Back button pressed");
    switch_screen(SCREEN_MAIN);
}

// Screen management
static void create_main_screen(void)
{
    screens[SCREEN_MAIN] = lv_obj_create(NULL);
    main_screen = screens[SCREEN_MAIN];

    // Create title
    title_label = lv_label_create(main_screen);
    lv_obj_add_style(title_label, &style_title, 0);
    lv_label_set_text(title_label, "ESP32-P4 Bootloader");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);

    // Create demo buttons grid
    const char *demo_names[] = {
        "Demo 1\nSD Card OTA",
        "Demo 2\nApplication",
        "Demo 3\nApplication",
        "Demo 4\nSettings"
    };

    lv_coord_t btn_width = 140;
    lv_coord_t btn_height = 100;
    lv_coord_t btn_spacing = 20;
    lv_coord_t start_y = 120;

    for (int i = 0; i < 4; i++) {
        demo_btns[i] = lv_btn_create(main_screen);
        lv_obj_add_style(demo_btns[i], &style_btn, 0);
        lv_obj_add_style(demo_btns[i], &style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_size(demo_btns[i], btn_width, btn_height);

        // Position in 2x2 grid
        int row = i / 2;
        int col = i % 2;
        lv_coord_t x = (col == 0) ? -btn_width/2 - btn_spacing/2 : btn_width/2 + btn_spacing/2;
        lv_coord_t y = start_y + row * (btn_height + btn_spacing);
        lv_obj_align(demo_btns[i], LV_ALIGN_CENTER, x, y);

        // Create label for button
        lv_obj_t *label = lv_label_create(demo_btns[i]);
        lv_label_set_text(label, demo_names[i]);
        lv_obj_center(label);

        // Store button ID and add callback
        lv_obj_set_user_data(demo_btns[i], (void*)(uintptr_t)i);
        lv_obj_add_event_cb(demo_btns[i], demo_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }

    // Create status label
    status_label = lv_label_create(main_screen);
    lv_obj_add_style(status_label, &style_status, 0);
    lv_label_set_text(status_label, "Select a demo to continue");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -40);

    ESP_LOGI(TAG, "Main screen created");
}

static void create_demo_screen(void)
{
    screens[SCREEN_DEMO] = lv_obj_create(NULL);

    // Title
    lv_obj_t *title = lv_label_create(screens[SCREEN_DEMO]);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "Demo Application");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Content
    lv_obj_t *content = lv_label_create(screens[SCREEN_DEMO]);
    lv_label_set_text(content, "This is a demo application\n\nPress Back to return");
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(screens[SCREEN_DEMO]);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Demo screen created");
}

static void create_settings_screen(void)
{
    screens[SCREEN_SETTINGS] = lv_obj_create(NULL);

    // Title
    lv_obj_t *title = lv_label_create(screens[SCREEN_SETTINGS]);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Settings content
    lv_obj_t *content = lv_label_create(screens[SCREEN_SETTINGS]);
    lv_label_set_text(content, "Settings and configuration\n\nPress Back to return");
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(screens[SCREEN_SETTINGS]);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Settings screen created");
}

void switch_screen(screen_id_t screen_id)
{
    if (screen_id >= SCREEN_COUNT) {
        ESP_LOGE(TAG, "Invalid screen ID: %d", screen_id);
        return;
    }

    if (!screens[screen_id]) {
        ESP_LOGE(TAG, "Screen %d not created", screen_id);
        return;
    }

    lv_screen_load(screens[screen_id]);
    current_screen = screen_id;
    ESP_LOGI(TAG, "Switched to screen %d", screen_id);
}

// Progress bar for OTA operations
void create_progress_bar(void)
{
    if (!main_screen) return;

    progress_bar = lv_bar_create(main_screen);
    lv_obj_set_size(progress_bar, 300, 20);
    lv_obj_align(progress_bar, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

    progress_label = lv_label_create(main_screen);
    lv_obj_add_style(progress_label, &style_status, 0);
    lv_label_set_text(progress_label, "0%");
    lv_obj_align_to(progress_label, progress_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    ESP_LOGI(TAG, "Progress bar created");
}

void update_progress_bar(uint8_t percent)
{
    if (!progress_bar || !progress_label) {
        create_progress_bar();
    }

    lv_bar_set_value(progress_bar, percent, LV_ANIM_OFF);

    char progress_text[16];
    snprintf(progress_text, sizeof(progress_text), "%d%%", percent);
    lv_label_set_text(progress_label, progress_text);

    // Update LVGL display
    lock_display();
    lv_timer_handler();
    unlock_display();

    ESP_LOGD(TAG, "Progress updated: %d%%", percent);
}

void show_progress(bool show)
{
    if (show) {
        if (!progress_bar) {
            create_progress_bar();
        }
        lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(progress_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (progress_bar) {
            lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
        if (progress_label) {
            lv_obj_add_flag(progress_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Force LVGL update
    lock_display();
    lv_timer_handler();
    unlock_display();
}

void update_status(const char* status)
{
    if (!status_label) return;

    lv_label_set_text(status_label, status);

    // Force LVGL update
    lock_display();
    lv_timer_handler();
    unlock_display();

    ESP_LOGI(TAG, "Status updated: %s", status);
}

void set_ota_in_progress(bool in_progress)
{
    ota_in_progress = in_progress;

    if (in_progress) {
        show_progress(true);
        update_status("SD Card OTA in progress...");
        // Disable buttons during OTA
        for (int i = 0; i < 4; i++) {
            if (demo_btns[i]) {
                lv_obj_add_state(demo_btns[i], LV_STATE_DISABLED);
            }
        }
    } else {
        show_progress(false);
        update_status("OTA completed. Select another demo or restart.");
        // Re-enable buttons after OTA
        for (int i = 0; i < 4; i++) {
            if (demo_btns[i]) {
                lv_obj_clear_state(demo_btns[i], LV_STATE_DISABLED);
            }
        }
    }

    // Force LVGL update
    lock_display();
    lv_timer_handler();
    unlock_display();
}

bool is_ota_in_progress(void)
{
    return ota_in_progress;
}

static void init_styles(void)
{
    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_20);
    lv_style_set_text_color(&style_title, lv_color_hex(0x00AA00));
    lv_style_set_text_align(&style_title, LV_TEXT_ALIGN_CENTER);

    // Button style
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x2196F3));
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x1976D2));
    lv_style_set_border_color(&style_btn, lv_color_hex(0x0D47A1));
    lv_style_set_border_width(&style_btn, 2);
    lv_style_set_radius(&style_btn, 8);
    lv_style_set_text_color(&style_btn, lv_color_white());
    lv_style_set_text_font(&style_btn, &lv_font_montserrat_14);

    // Button pressed style
    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0x0D47A1));
    lv_style_set_border_color(&style_btn_pressed, lv_color_hex(0x1565C0));

    // Status style
    lv_style_init(&style_status);
    lv_style_set_text_font(&style_status, &lv_font_montserrat_12);
    lv_style_set_text_color(&style_status, lv_color_hex(0x666666));
    lv_style_set_text_align(&style_status, LV_TEXT_ALIGN_CENTER);

    ESP_LOGI(TAG, "LVGL styles initialized");
}

esp_err_t lvgl_bootloader_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL bootloader...");

    // Initialize display mutex
    init_display_mutex();

    // Lock display for thread-safe LVGL operations
    lock_display();

    // Initialize styles
    init_styles();

    // Create screens
    create_main_screen();
    create_demo_screen();
    create_settings_screen();

    // Load main screen
    lv_screen_load(screens[SCREEN_MAIN]);

    unlock_display();

    ESP_LOGI(TAG, "LVGL bootloader initialized successfully");
    return ESP_OK;
}

void lvgl_bootloader_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing LVGL bootloader...");

    // Clean up styles
    lv_style_reset(&style_title);
    lv_style_reset(&style_btn);
    lv_style_reset(&style_btn_pressed);
    lv_style_reset(&style_status);

    // Clean up screens
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (screens[i]) {
            lv_obj_del(screens[i]);
            screens[i] = NULL;
        }
    }

    // Reset pointers
    main_screen = NULL;
    title_label = NULL;
    status_label = NULL;
    progress_bar = NULL;
    progress_label = NULL;
    for (int i = 0; i < 4; i++) {
        demo_btns[i] = NULL;
    }

    ESP_LOGI(TAG, "LVGL bootloader deinitialized");
}