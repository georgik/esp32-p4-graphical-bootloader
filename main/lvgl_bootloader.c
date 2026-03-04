/**
 * @file lvgl_bootloader.c
 * @brief LVGL-based graphical bootloader UI
 *
 * Optimized for ESP32-P4 with IRAM framebuffer and efficient rendering
 */

#include "lvgl_bootloader.h"
#include "firmware_selector.h"
#include "firmware_validator.h"
#include "board_init.h"
#include "firmware_storage.h"
#include "ui_update.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "soc/lp_system_reg.h"
#include "lvgl.h"
#include "sd_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __SIMULATOR_BUILD__
#include "vfs_mock.h"  // Must be included AFTER sys/stat.h to wrap stat() for simulator
#endif

static const char *TAG = "lvgl_bootloader";

// RTC register constants for bootloader communication
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic in ASCII

// Display mutex for thread safety (fallback if BSP doesn't provide lock)
static SemaphoreHandle_t lvgl_mutex = NULL;

// UI Elements
static lv_obj_t *main_screen = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *demo_btns[4] = {0};
lv_obj_t *status_label = NULL;  // Non-static for ui_update.c access
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *app_cont = NULL;

// Firmware selectors
static firmware_selector_t firmware_selector;  // For SD card (firmwares to flash)
static bool firmware_selector_initialized = false;

static firmware_selector_t boot_menu_selector;  // For firmware storage (installed firmwares)
static bool boot_menu_selector_initialized = false;

// Screen IDs
static int current_screen = SCREEN_MAIN;
static lv_obj_t *screens[SCREEN_COUNT] = {0}; // All screens including BOOT_MENU

// Style objects
static lv_style_t style_title;
static lv_style_t style_btn;
static lv_style_t style_btn_pressed;
static lv_style_t style_status;

// Progress tracking
static bool ota_in_progress = false;

// Forward declarations for callback functions
static void boot_firmware_cb(lv_event_t *e);

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
void lock_display(void)
{
    if (lvgl_mutex) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    }
}

// Unlock display
void unlock_display(void)
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

    ESP_LOGI(TAG, "Main button %lu pressed", btn_id);

    switch(btn_id) {
        case 0:
            // Select & Flash Firmware
            ESP_LOGI(TAG, "Opening firmware selector...");
            show_firmware_selector_screen();
            break;

        case 1:
            // Boot Menu - Show flashed applications
            ESP_LOGI(TAG, "Opening boot menu...");
            show_boot_menu_screen();
            break;

        case 2:
            // Settings
            switch_screen(SCREEN_SETTINGS);
            break;

        case 3:
            // Reboot to run application
            ESP_LOGI(TAG, "Rebooting system...");
            esp_restart();
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
    lv_obj_set_style_bg_color(main_screen, lv_color_black(), 0);

    // Create title - position higher and use smaller font
    title_label = lv_label_create(main_screen);
    lv_obj_add_style(title_label, &style_title, 0);
    lv_label_set_text(title_label, "Available Applications");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    // Create scrollable container for firmware applications list
    app_cont = lv_obj_create(main_screen);
    lv_obj_set_size(app_cont, 900, 450); // Large area for application list
    lv_obj_align(app_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_layout(app_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(app_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(app_cont, lv_color_hex(0x1a1a1a), 0);  // Dark gray background
    lv_obj_set_style_border_width(app_cont, 0, 0);  // Remove border
    lv_obj_set_style_pad_all(app_cont, 10, 0);  // Add some padding

    // "Load from SD Card" button is now hidden - firmware flashing from SD card is disabled
    demo_btns[0] = NULL;

    // Read firmware from firmware_storage partition and create boot buttons
    ESP_LOGI(TAG, "Reading firmware storage for MAIN SCREEN...");

    uint32_t firmware_count = 0;
    esp_err_t err = firmware_storage_get_count(&firmware_count);

    if (err == ESP_OK && firmware_count > 0) {
        ESP_LOGI(TAG, "Found %u firmware(s) in firmware storage", firmware_count);

        for (uint32_t i = 0; i < firmware_count; i++) {
            firmware_storage_entry_t entry;
            err = firmware_storage_get_entry(i, &entry);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read firmware entry %u: %s", i, esp_err_to_name(err));
                continue;
            }

            // Create boot button for this firmware
            lv_obj_t *btn = lv_btn_create(app_cont);
            lv_obj_add_style(btn, &style_btn, 0);
            lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 800, 80);

            // Store firmware index for boot callback (offset points to firmware data)
            uint32_t *stored_index = malloc(sizeof(uint32_t));
            *stored_index = i;
            lv_obj_set_user_data(btn, stored_index);

            // Create button label with firmware info
            char btn_text[256];
            char size_str[32];
            firmware_format_size(entry.size, size_str, sizeof(size_str));

            snprintf(btn_text, sizeof(btn_text), "%s\nSize: %s",
                     entry.name, size_str);

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, btn_text);
            lv_obj_center(label);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);

            lv_obj_add_event_cb(btn, boot_firmware_cb, LV_EVENT_CLICKED, NULL);

            ESP_LOGI(TAG, "Created boot button for %s at offset 0x%x", entry.name, entry.offset);
        }
    } else {
        ESP_LOGI(TAG, "No firmware found in storage (err=%d)", err);
        lv_obj_t *no_fw_label = lv_label_create(app_cont);
        lv_label_set_text(no_fw_label, "No firmware applications found.\nUse 'Load from SD Card' to flash firmware first.");
        lv_obj_set_style_text_align(no_fw_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Create status label at bottom with more space
    status_label = lv_label_create(main_screen);
    lv_obj_add_style(status_label, &style_status, 0);
    lv_label_set_text(status_label, "Select application to boot");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -50);

    ESP_LOGI(TAG, "Main screen created with applications list for 1024x600 display");
}

static void create_demo_screen(void)
{
    screens[SCREEN_DEMO] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screens[SCREEN_DEMO], lv_color_black(), 0);

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
    lv_obj_set_style_bg_color(screens[SCREEN_SETTINGS], lv_color_black(), 0);

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

// Boot menu event callback - uses RTC mechanism as PRIMARY boot method
static void boot_firmware_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint32_t* firmware_index = (uint32_t*)lv_obj_get_user_data(btn);

    if (firmware_index) {
        // Read firmware storage entry
        firmware_storage_entry_t entry;
        esp_err_t err = firmware_storage_get_entry(*firmware_index, &entry);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read firmware storage entry %u: %s",
                     *firmware_index, esp_err_to_name(err));
            free(firmware_index);
            return;
        }

        // Calculate the absolute address of the firmware data
        // The entry.offset can be either:
        // 1. Relative to FIRMWARE_STORAGE_OFFSET (when flashed by our bootloader)
        // 2. Absolute OTA address (when created by simulator)
        // Detect which case by checking if offset is >= OTA_0 start (0x140000)
        uint32_t firmware_address;
        if (entry.offset >= 0x140000) {
            // Already an absolute OTA address (simulator-generated image)
            firmware_address = entry.offset;
        } else {
            // Relative offset (flashed by our bootloader)
            firmware_address = FIRMWARE_STORAGE_OFFSET + entry.offset;
        }

        ESP_LOGI(TAG, "Booting firmware %u: %s @ address 0x%x",
                 *firmware_index, entry.name, firmware_address);

        ESP_LOGI(TAG, "Searching for partition containing address 0x%x...", firmware_address);

        // Find the OTA partition that contains this address
        const esp_partition_t* partition = NULL;
        esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_ANY,
                                                        NULL);

        ESP_LOGI(TAG, "Partition iterator created: %p", (void*)it);

        if (it) {
            int iter_count = 0;
            ESP_LOGI(TAG, "Starting partition iteration...");

            // Iterate through partitions using the iterator directly
            while (it != NULL) {
                partition = esp_partition_get(it);
                if (partition == NULL) {
                    break;
                }

                iter_count++;
                ESP_LOGI(TAG, "  [%d] Checking partition: %s (0x%x - 0x%x, size=0x%x)",
                         iter_count, partition->label, partition->address,
                         partition->address + partition->size, partition->size);

                // Check if firmware_address falls within this partition
                if (firmware_address >= partition->address &&
                    firmware_address < partition->address + partition->size) {
                    ESP_LOGI(TAG, "Found matching partition: %s (0x%x - 0x%x)",
                             partition->label, partition->address,
                             partition->address + partition->size);
                    break;
                }

                // Move to next partition
                it = esp_partition_next(it);
            }
            ESP_LOGI(TAG, "Partition iteration completed (%d iterations)", iter_count);
            // Release the iterator
            if (it) {
                esp_partition_iterator_release(it);
            }
            ESP_LOGI(TAG, "Partition iterator released");
        } else {
            ESP_LOGE(TAG, "Failed to create partition iterator!");
        }

        if (!partition) {
            ESP_LOGE(TAG, "No partition found for firmware at address 0x%x", firmware_address);
            free(firmware_index);
            return;
        }

        ESP_LOGI(TAG, "Partition found successfully, continuing with RTC boot...");

        ESP_LOGI(TAG, "Booting from partition: %s (subtype: %d)",
                 partition->label, partition->subtype);

        // RTC boot mechanism - PRIMARY METHOD (THE KEY FEATURE!)
        // Map partition subtype to partition type number
        uint32_t partition_type = 0;
        if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
            partition_type = 1;  // OTA_0 = partition_type 1
        } else if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
            partition_type = 2;  // OTA_1 = partition_type 2
        } else if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_2) {
            partition_type = 3;  // OTA_2 = partition_type 3
        } else if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_3) {
            partition_type = 4;  // OTA_3 = partition_type 4
        } else if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            partition_type = 0;  // FACTORY = partition_type 0
        } else {
            ESP_LOGW(TAG, "Unsupported partition subtype: %d, attempting RTC boot anyway", partition->subtype);
            partition_type = 0;  // Default to factory for unknown types
        }

        // Write boot request to RTC register for bootloader to read
        // This does NOT modify the original app (unlike esp_ota_set_boot_partition)
        uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (partition_type << 24);
        REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);

        ESP_LOGI(TAG, "RTC register updated: 0x%08x for partition type %d (%s)",
                 rtc_value, partition_type, partition->label);
        ESP_LOGI(TAG, "System will boot from %s after restart (one-time boot via RTC)",
                 partition->label);

        free(firmware_index);

        // Add delay to show booting animation
        vTaskDelay(pdMS_TO_TICKS(2000));

        esp_restart();
    }
}

static void create_boot_menu_screen(void)
{
    screens[SCREEN_BOOT_MENU] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screens[SCREEN_BOOT_MENU], lv_color_black(), 0);

    // Title
    lv_obj_t *title = lv_label_create(screens[SCREEN_BOOT_MENU]);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "Boot Menu - Select Application");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Scrollable container for firmware list
    lv_obj_t *cont = lv_obj_create(screens[SCREEN_BOOT_MENU]);
    lv_obj_set_size(cont, 900, 400); // Large area for firmware list
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1a1a1a), 0);  // Dark gray background
    lv_obj_set_style_border_width(cont, 0, 0);  // Remove border
    lv_obj_set_style_pad_all(cont, 10, 0);  // Add some padding

    // Read firmware from firmware_storage partition and create boot buttons
    ESP_LOGI(TAG, "Reading firmware storage for BOOT MENU SCREEN...");

    uint32_t firmware_count = 0;
    esp_err_t err = firmware_storage_get_count(&firmware_count);

    if (err == ESP_OK && firmware_count > 0) {
        ESP_LOGI(TAG, "Found %u firmware(s) in firmware storage", firmware_count);

        for (uint32_t i = 0; i < firmware_count; i++) {
            firmware_storage_entry_t entry;
            err = firmware_storage_get_entry(i, &entry);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read firmware entry %u: %s", i, esp_err_to_name(err));
                continue;
            }

            // Create boot button for this firmware
            lv_obj_t *btn = lv_btn_create(cont);
            lv_obj_add_style(btn, &style_btn, 0);
            lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
            lv_obj_set_size(btn, 800, 80);

            // Store firmware index for boot callback
            uint32_t *stored_index = malloc(sizeof(uint32_t));
            *stored_index = i;
            lv_obj_set_user_data(btn, stored_index);

            // Create button label with firmware info
            char btn_text[256];
            char size_str[32];
            firmware_format_size(entry.size, size_str, sizeof(size_str));

            snprintf(btn_text, sizeof(btn_text), "%s\nSize: %s",
                     entry.name, size_str);

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, btn_text);
            lv_obj_center(label);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);

            lv_obj_add_event_cb(btn, boot_firmware_cb, LV_EVENT_CLICKED, NULL);

            ESP_LOGI(TAG, "Created boot button for %s at offset 0x%x", entry.name, entry.offset);
        }
    } else {
        ESP_LOGI(TAG, "No firmware found in storage (err=%d)", err);
        lv_obj_t *no_fw_label = lv_label_create(cont);
        lv_label_set_text(no_fw_label, "No firmware applications found.\nFlash firmware first using the \"Load from SD Card\" option.");
        lv_obj_set_style_text_align(no_fw_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Back button
    lv_obj_t *back_btn = lv_btn_create(screens[SCREEN_BOOT_MENU]);
    lv_obj_add_style(back_btn, &style_btn, 0);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Boot menu screen created");
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

void refresh_main_screen(void)
{
    ESP_LOGI(TAG, "Refreshing main screen to reload applications list...");

    if (!main_screen || !screens[SCREEN_MAIN]) {
        ESP_LOGE(TAG, "Main screen not initialized, cannot refresh");
        return;
    }

    ESP_LOGI(TAG, "Destroying existing main screen elements...");

    // Clear and destroy existing main screen elements
    // Use async deletion to avoid deleting objects during event processing
    if (app_cont) {
        lv_obj_del_async(app_cont);
        app_cont = NULL;
    }

    if (demo_btns[0]) {
        lv_obj_del_async(demo_btns[0]);
        demo_btns[0] = NULL;
    }

    if (title_label) {
        lv_obj_del_async(title_label);
        title_label = NULL;
    }

    if (status_label) {
        lv_obj_del_async(status_label);
        status_label = NULL;
    }

    // Clear all button references
    for (int i = 0; i < 4; i++) {
        demo_btns[i] = NULL;
    }

    // Recreate the main screen
    create_main_screen();

    ESP_LOGI(TAG, "Main screen refreshed successfully");

    // Ensure the main screen is set as current screen (in case LVGL lost track)
    lv_scr_load(main_screen);
    current_screen = SCREEN_MAIN;

    ESP_LOGI(TAG, "Main screen set as current screen");

    // NOTE: Don't call lv_timer_handler() here!
    // The lvgl_task is running separately and will process this update
    // Calling lv_timer_handler() from other tasks causes watchdog timeouts
    lv_tick_inc(lv_tick_get());

    // Small delay to ensure LVGL has time to process the screen changes
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Progress bar for OTA operations
void create_progress_bar(void)
{
    if (!main_screen) return;

    progress_bar = lv_bar_create(main_screen);
    lv_obj_set_size(progress_bar, 300, 20);
    lv_obj_align(progress_bar, LV_ALIGN_BOTTOM_RIGHT, -50, -30);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

    progress_label = lv_label_create(main_screen);
    lv_obj_add_style(progress_label, &style_status, 0);
    lv_label_set_text(progress_label, "0%");
    lv_obj_align_to(progress_label, progress_bar, LV_ALIGN_OUT_LEFT_MID, 10, 0);

    ESP_LOGI(TAG, "Progress bar created");
}

void update_progress_bar(uint8_t percent)
{
    if (!progress_bar || !progress_label) {
        create_progress_bar();
    }

    lock_display();
    lv_bar_set_value(progress_bar, percent, LV_ANIM_OFF);

    char progress_text[16];
    snprintf(progress_text, sizeof(progress_text), "%d%%", percent);
    lv_label_set_text(progress_label, progress_text);
    unlock_display();

    // NOTE: Don't call lv_timer_handler() here!
    // The lvgl_task is running separately and will process this update
    // Calling lv_timer_handler() from other tasks causes watchdog timeouts

    ESP_LOGD(TAG, "Progress updated: %d%%", percent);
}

void show_progress(bool show)
{
    lock_display();
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
    unlock_display();

    // NOTE: Don't call lv_timer_handler() here!
    // The lvgl_task is running separately and will process this update
    // Calling lv_timer_handler() from other tasks causes watchdog timeouts
}

// Public API: Send status update through queue (safe to call from any task)
void update_status(const char* status)
{
    if (!status_label) return;

    // CRITICAL: Don't call LVGL functions directly from other tasks!
    // Even with mutex, LVGL invalidations can cause watchdog timeouts
    // Always route through UI update queue
    ui_update_message_t msg = {
        .type = UI_UPDATE_STATUS,
        .data.status = {
            .message = status
        }
    };
    ui_update_send(&msg);

    ESP_LOGI(TAG, "Status update queued: %s", status);
}

void set_ota_in_progress(bool in_progress)
{
    ota_in_progress = in_progress;

    if (in_progress) {
        show_progress(true);
        update_status("SD Card OTA in progress...");
        // Disable buttons during OTA
        lock_display();
        for (int i = 0; i < 4; i++) {
            if (demo_btns[i]) {
                lv_obj_add_state(demo_btns[i], LV_STATE_DISABLED);
            }
        }
        unlock_display();
    } else {
        show_progress(false);
        update_status("OTA completed. Select another demo or restart.");
        // Re-enable buttons after OTA
        lock_display();
        for (int i = 0; i < 4; i++) {
            if (demo_btns[i]) {
                lv_obj_clear_state(demo_btns[i], LV_STATE_DISABLED);
            }
        }
        unlock_display();
    }

    // NOTE: Don't call lv_timer_handler() here!
    // The lvgl_task is running separately and will process this update
    // Calling lv_timer_handler() from other tasks causes watchdog timeouts
}

bool is_ota_in_progress(void)
{
    return ota_in_progress;
}

static void init_styles(void)
{
    // Title style - change to lighter green for black background
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_20);
    lv_style_set_text_color(&style_title, lv_color_hex(0x00FF00));
    lv_style_set_text_align(&style_title, LV_TEXT_ALIGN_CENTER);

    // Button style - darker blue for less brightness
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x0D47A1));  // Darker blue
    lv_style_set_border_color(&style_btn, lv_color_hex(0x0A3278));  // Even darker border
    lv_style_set_border_width(&style_btn, 2);
    lv_style_set_radius(&style_btn, 8);
    lv_style_set_text_color(&style_btn, lv_color_white());
    lv_style_set_text_font(&style_btn, &lv_font_montserrat_14);

    // Button pressed style - even darker when pressed
    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0x0A3278));  // Very dark blue
    lv_style_set_border_color(&style_btn_pressed, lv_color_hex(0x072550));

    // Status style - lighter text for black background
    lv_style_init(&style_status);
    lv_style_set_text_font(&style_status, &lv_font_montserrat_12);
    lv_style_set_text_color(&style_status, lv_color_hex(0xCCCCCC));  // Light gray instead of dark gray
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

    // Initialize boot menu selector (scans firmware storage for installed firmwares)
    ESP_LOGI(TAG, "Initializing boot menu selector (firmware storage)...");
    esp_err_t ret = firmware_selector_init(&boot_menu_selector);
    if (ret == ESP_OK) {
        ret = firmware_selector_scan_storage(&boot_menu_selector);
        if (ret == ESP_OK && boot_menu_selector.firmware_count > 0) {
            boot_menu_selector_initialized = true;
            ESP_LOGI(TAG, "Boot menu selector initialized: %u installed firmwares found",
                     boot_menu_selector.firmware_count);
        } else {
            ESP_LOGW(TAG, "No installed firmwares found in firmware storage");
        }
    } else {
        ESP_LOGW(TAG, "Failed to initialize boot menu selector: %s", esp_err_to_name(ret));
    }

    // Create screens
    create_main_screen();
    create_demo_screen();
    create_settings_screen();
    create_boot_menu_screen();

    // Load main screen
    lv_screen_load(screens[SCREEN_MAIN]);

    unlock_display();

    ESP_LOGI(TAG, "LVGL bootloader initialized successfully");
    return ESP_OK;
}

// Firmware selector integration functions
esp_err_t init_firmware_selector_screen(void)
{
    if (!firmware_selector_initialized) {
        ESP_LOGI(TAG, "Initializing firmware selector...");

        esp_err_t ret = firmware_selector_init(&firmware_selector);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize firmware selector: %s", esp_err_to_name(ret));
            return ret;
        }

        // Try SD card scan first
        ret = firmware_selector_scan_directory(&firmware_selector);

        // If SD card scan fails or finds no firmwares, try firmware storage
        if (ret != ESP_OK || firmware_selector.firmware_count == 0) {
            ESP_LOGI(TAG, "No firmwares on SD card, scanning firmware storage...");
            ret = firmware_selector_scan_storage(&firmware_selector);

            // If firmware storage scan also fails, return error
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "No firmwares found on SD card or in firmware storage");
                return ret;
            }
        }

        ret = firmware_selector_create_ui(&firmware_selector);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create firmware selector UI: %s", esp_err_to_name(ret));
            return ret;
        }

        firmware_selector_initialized = true;
        ESP_LOGI(TAG, "Firmware selector initialized: %d firmware files found", firmware_selector.firmware_count);
    }

    return ESP_OK;
}

esp_err_t show_firmware_selector_screen(void)
{
    esp_err_t ret = init_firmware_selector_screen();
    if (ret != ESP_OK) {
        return ret;
    }

    return firmware_selector_show(&firmware_selector);
}

esp_err_t hide_firmware_selector_screen(void)
{
    if (!firmware_selector_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return firmware_selector_hide(&firmware_selector);
}

esp_err_t show_boot_menu_screen(void)
{
    if (!screens[SCREEN_BOOT_MENU]) {
        ESP_LOGE(TAG, "Boot menu screen not created");
        return ESP_ERR_INVALID_STATE;
    }

    // Refresh the boot menu each time it's shown (in case firmware list changed)
    lv_obj_clean(screens[SCREEN_BOOT_MENU]);
    create_boot_menu_screen();

    lv_screen_load(screens[SCREEN_BOOT_MENU]);
    current_screen = SCREEN_BOOT_MENU;
    ESP_LOGI(TAG, "Boot menu screen shown");
    return ESP_OK;
}

esp_err_t hide_boot_menu_screen(void)
{
    if (!screens[SCREEN_BOOT_MENU]) {
        return ESP_ERR_INVALID_STATE;
    }

    // Return to main screen
    lv_screen_load(screens[SCREEN_MAIN]);
    current_screen = SCREEN_MAIN;
    ESP_LOGI(TAG, "Boot menu screen hidden");
    return ESP_OK;
}

esp_err_t boot_firmware_from_partition(const char* partition_name)
{
    if (!partition_name) {
        ESP_LOGE(TAG, "Partition name is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Attempting to boot firmware from partition: %s", partition_name);

    // Find the OTA partition to get subtype information
    const esp_partition_t* ota_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, partition_name);

    if (!ota_partition) {
        ESP_LOGE(TAG, "OTA partition '%s' not found", partition_name);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Found OTA partition: %s (subtype: %d, offset: 0x%08x, size: 0x%08x)",
             ota_partition->label, ota_partition->subtype, ota_partition->address, ota_partition->size);

    // RTC register constants for bootloader communication (must match bootloader_custom.c)
    #define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
    #define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic in ASCII

    // Determine partition type from subtype
    uint32_t partition_type = 0;
    if (ota_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        partition_type = 1;  // OTA_0 = partition_type 1
    } else if (ota_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        partition_type = 2;  // OTA_1 = partition_type 2
    } else if (ota_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_2) {
        partition_type = 3;  // OTA_2 = partition_type 3
    } else if (ota_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_3) {
        partition_type = 4;  // OTA_3 = partition_type 4
    } else if (ota_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        partition_type = 0;  // FACTORY = partition_type 0
    } else {
        ESP_LOGE(TAG, "Unsupported partition subtype: %d", ota_partition->subtype);
        return ESP_ERR_INVALID_ARG;
    }

    // Set boot partition using RTC register for one-time boot (like other demos)
    ESP_LOGI(TAG, "Setting RTC boot request for partition type %d (%s)...",
             partition_type, partition_name);

    // Write boot request to RTC register for bootloader to read
    uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (partition_type << 24);
    REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);

    ESP_LOGI(TAG, "RTC register updated: 0x%08x, system will boot from %s after restart",
             rtc_value, partition_name);

    ESP_LOGI(TAG, "Boot partition set successfully using RTC mechanism. Restarting to boot from %s...", partition_name);

    // Give some time for the log to be printed and then restart
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    // This should never be reached
    return ESP_OK;
}

void lvgl_bootloader_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing LVGL bootloader...");

    // Clean up firmware selector
    if (firmware_selector_initialized) {
        firmware_selector_cleanup(&firmware_selector);
        firmware_selector_initialized = false;
    }

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