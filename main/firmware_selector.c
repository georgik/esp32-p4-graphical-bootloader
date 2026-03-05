/**
 * @file firmware_selector.c
 * @brief Firmware selection and management implementation
 */

#include "firmware_selector.h"
#include "firmware_storage.h"
#include "firmware_storage_config.h"
#include "firmware_validator.h"
#include "partition_manager.h"
#include "firmware_flasher.h"
#include "lvgl_bootloader.h"
#include "partition_visualizer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef __SIMULATOR_BUILD__
#include "vfs_mock.h"  // For path translation - MUST be after system headers
#endif

static const char* TAG = "firmware_selector";

// Available flash space for firmwares (16MB total - bootloader - partitions)
#define AVAILABLE_FLASH_SPACE (16 * 1024 * 1024 - 0x100000)

// Track if flashing is in progress to disable UI controls
static bool flashing_in_progress = false;

// Global reference to currently active firmware selector for progress updates
firmware_selector_t* g_active_firmware_selector = NULL;

// LVGL event callbacks
static void fw_selector_list_event_cb(lv_event_t* e);
static void fw_selector_select_all_cb(lv_event_t* e);
static void fw_selector_clear_cb(lv_event_t* e);
static void fw_selector_view_partitions_cb(lv_event_t* e);
static void fw_selector_flash_cb(lv_event_t* e);
static void fw_selector_back_cb(lv_event_t* e);
static void fw_selector_modal_ok_cb(lv_event_t* e);
static void fw_flash_progress_callback(uint32_t current_firmware, uint32_t total_firmwares,
                                       uint32_t current_progress, uint32_t total_progress, const char* status_message);
static void fw_flash_status_callback(flash_state_t state, flash_result_t result, const char* status_message);

static void update_firmware_list_item(firmware_selector_t* selector, uint32_t index);
static void update_buttons_state(firmware_selector_t* selector);

esp_err_t firmware_selector_init(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing firmware selector");

    // Initialize structure
    memset(selector, 0, sizeof(firmware_selector_t));

    // Debug: Log SD card root directory contents
    ESP_LOGI(TAG, "Checking SD card root directory: /sdcard");
    DIR* root_dir = opendir("/sdcard");
    if (root_dir) {
        ESP_LOGI(TAG, "SD card root directory opened successfully");
        struct dirent* entry;
        int file_count = 0;
        while ((entry = readdir(root_dir)) != NULL && file_count < 20) {
            ESP_LOGI(TAG, "  - /sdcard/%s", entry->d_name);
            file_count++;
        }
        closedir(root_dir);
        ESP_LOGI(TAG, "Total %d entries in /sdcard root", file_count);
    } else {
        ESP_LOGE(TAG, "Failed to open SD card root directory: /sdcard");
    }

    // Ensure firmware directory exists
    struct stat st;
    if (stat(FIRMWARE_DIRECTORY, &st) != 0) {
        ESP_LOGW(TAG, "Firmware directory not found: %s", FIRMWARE_DIRECTORY);
        // Note: Directory creation could be added here if needed
    } else {
        ESP_LOGI(TAG, "Firmware directory exists: %s", FIRMWARE_DIRECTORY);
    }

    selector->is_initialized = true;
    ESP_LOGI(TAG, "Firmware selector initialized successfully");

    return ESP_OK;
}

esp_err_t firmware_selector_scan_directory(firmware_selector_t* selector)
{
    if (!selector || !selector->is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Scanning firmware directory: %s", FIRMWARE_DIRECTORY);

    DIR* dir = opendir(FIRMWARE_DIRECTORY);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open firmware directory: %s", FIRMWARE_DIRECTORY);
        ESP_LOGE(TAG, "  errno=%d (%s)", errno, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Firmware directory opened successfully, scanning for .bin files...");

    selector->firmware_count = 0;
    selector->selected_count = 0;
    selector->total_selected_size = 0;

    struct dirent* entry;
    int total_entries = 0;
    while ((entry = readdir(dir)) != NULL) {
        total_entries++;

        // Skip hidden files (starting with .) - macOS creates these metadata files
        if (entry->d_name[0] == '.') {
            ESP_LOGD(TAG, "Skipping hidden file: %s", entry->d_name);
            continue;
        }

        ESP_LOGI(TAG, "  Found file: %s", entry->d_name);

        // Check for .bin extension
        if (!firmware_has_valid_extension(entry->d_name)) {
            ESP_LOGD(TAG, "    Skipping (no .bin extension)");
            continue;
        }

        ESP_LOGI(TAG, "    Has .bin extension, adding to list");

        if (selector->firmware_count >= MAX_FIRMWARE_COUNT) {
            ESP_LOGW(TAG, "    Reached maximum firmware count (%d), skipping remaining files", MAX_FIRMWARE_COUNT);
            break;
        }

        // Build full file path
        firmware_info_t* fw = &selector->firmware_list[selector->firmware_count];
        int path_ret = snprintf(fw->file_path, sizeof(fw->file_path), "%s/%s", FIRMWARE_DIRECTORY, entry->d_name);
        if (path_ret >= (int)sizeof(fw->file_path)) {
            fw->file_path[sizeof(fw->file_path) - 1] = '\0';
        }

        // Copy filename safely
        size_t name_len = strlen(entry->d_name);
        size_t copy_len = name_len < sizeof(fw->filename) - 1 ? name_len : sizeof(fw->filename) - 1;
        memcpy(fw->filename, entry->d_name, copy_len);
        fw->filename[copy_len] = '\0';

        // Extract display name
        esp_err_t name_ret = firmware_extract_display_name(fw->file_path, fw->display_name, sizeof(fw->display_name));
        if (name_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to extract display name for: %s", fw->filename);
            continue;
        }

        // FAST SCAN: Get file size only - defer heavy validation to when user selects
        struct stat st;
        if (stat(fw->file_path, &st) == 0) {
            fw->size = st.st_size;
            // Basic size check - mark as potentially valid if reasonable size
            fw->is_valid = (fw->size >= 1024 && fw->size <= 16 * 1024 * 1024); // 1KB to 16MB

            // Fast CRC32 calculation using first/last block sampling instead of full file
            // This is much faster for large images and provides reasonable integrity checking
            esp_err_t crc_ret = firmware_calculate_fast_crc32(fw->file_path, fw->size, &fw->crc32);
            if (crc_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to calculate fast CRC32 for %s, using 0", fw->filename);
                fw->crc32 = 0;
            } else {
                ESP_LOGD(TAG, "Fast CRC32 calculated for %s: 0x%08X", fw->filename, fw->crc32);
            }
        } else {
            ESP_LOGW(TAG, "Cannot get file size for: %s", fw->filename);
            fw->is_valid = false;
            fw->size = 0;
            fw->crc32 = 0;
        }

        fw->is_selected = false;
        fw->assigned_partition = NULL;
        fw->list_item = NULL;

        selector->firmware_count++;
        ESP_LOGI(TAG, "Found firmware: %s (%d bytes, %s)",
                 fw->display_name, fw->size, fw->is_valid ? "valid" : "invalid");
    }

    closedir(dir);

    ESP_LOGI(TAG, "Firmware scan complete:");
    ESP_LOGI(TAG, "  Total entries scanned: %d", total_entries);
    ESP_LOGI(TAG, "  Valid .bin files found: %lu", (unsigned long)selector->firmware_count);
    if (selector->firmware_count == 0) {
        ESP_LOGW(TAG, "  No valid firmware files found in %s", FIRMWARE_DIRECTORY);
    }
    return ESP_OK;
}

static void fw_selector_list_event_cb(lv_event_t* e)
{
    // Extract firmware index from user data (passed as uintptr_t cast to void*)
    uint32_t firmware_idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    // Get the active selector from global reference
    extern firmware_selector_t* g_active_firmware_selector;
    if (!g_active_firmware_selector) {
        ESP_LOGE("firmware_selector", "No active firmware selector");
        return;
    }

    // Toggle selection for this firmware
    firmware_selector_toggle_selection(g_active_firmware_selector, firmware_idx);
}

static void fw_selector_select_all_cb(lv_event_t* e)
{
    firmware_selector_t* selector = (firmware_selector_t*)lv_event_get_user_data(e);
    if (selector) {
        firmware_selector_select_all(selector);
    }
}

static void fw_selector_clear_cb(lv_event_t* e)
{
    firmware_selector_t* selector = (firmware_selector_t*)lv_event_get_user_data(e);
    if (selector) {
        firmware_selector_clear_selection(selector);
    }
}

static void fw_selector_view_partitions_cb(lv_event_t* e)
{
    ESP_LOGI(TAG, "View Partitions button clicked");

    (void)e;  // Unused

    // Show partition visualizer
    partition_visualizer_show();
}

static void fw_selector_flash_cb(lv_event_t* e)
{
    firmware_selector_t* selector = (firmware_selector_t*)lv_event_get_user_data(e);
    if (selector) {
        ESP_LOGI(TAG, "Flash button pressed - Starting partition management and flashing");

        // Check if any firmwares are selected
        if (selector->selected_count == 0) {
            ESP_LOGW(TAG, "No firmware files selected for flashing");
            return;
        }

        // Check space availability and warn about truncation if needed
        bool fits_in_flash = false;
        esp_err_t ret = firmware_selector_check_space(selector, &fits_in_flash);
        if (!fits_in_flash) {
            ESP_LOGW(TAG, "Selected firmwares are too large - will truncate to fit available flash space");
            ESP_LOGW(TAG, "Some firmware assets may be truncated, but core functionality should work");
            // Continue with flashing - partition manager will handle truncation
        }

        ESP_LOGI(TAG, "Starting partition generation and flashing for %d firmwares (%lu total bytes)",
                 selector->selected_count, (unsigned long)selector->total_selected_size);

        // Set flashing in progress state and disable UI controls
        flashing_in_progress = true;
        update_buttons_state(selector);

        // Initialize partition manager and flasher
        extern esp_err_t partition_manager_init(void);
        extern esp_err_t firmware_flasher_init(void);

        ret = partition_manager_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize partition manager");
            return;
        }

        ret = firmware_flasher_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize firmware flasher");
            return;
        }

        // Generate partition layout
        partition_table_layout_t layout;
        ret = partition_manager_generate_layout(selector, &layout);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to generate partition layout: %s", esp_err_to_name(ret));
            return;
        }

        // Configure flash operation - copy layout to prevent stack corruption
        static flash_config_t flash_config = {0};
        flash_config.firmware_selector = selector;
        flash_config.partition_layout = layout;  // Copy the layout structure
        flash_config.enable_backup = true;
        flash_config.enable_verification = true;
        flash_config.enable_optimized_chunking = true;
        flash_config.chunk_size = 0;  // Auto-detect
        flash_config.progress_callback = fw_flash_progress_callback;  // LVGL progress updates
        flash_config.status_callback = fw_flash_status_callback;   // Handle completion events

        // Start flashing
        ret = firmware_flasher_start(&flash_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start firmware flashing: %s", esp_err_to_name(ret));
            return;
        }

        ESP_LOGI(TAG, "Firmware flashing operation started");
    }
}

// Modal OK button callback
static void fw_selector_modal_ok_cb(lv_event_t* e)
{
    firmware_selector_t* selector = (firmware_selector_t*)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Modal OK button clicked, selector=%p", selector);

    if (!selector) {
        ESP_LOGE(TAG, "Modal OK: selector is NULL!");
        return;
    }

    if (selector->completion_modal) {
        // Hide modal
        lv_obj_add_flag(selector->completion_modal, LV_OBJ_FLAG_HIDDEN);

        // Ensure flashing state is cleared (defensive fix)
        if (flashing_in_progress) {
            ESP_LOGW(TAG, "Modal OK: clearing flashing_in_progress flag that was still set");
            flashing_in_progress = false;
        }

        // ALWAYS update button state when modal is closed
        ESP_LOGI(TAG, "Modal OK: updating button states, selected_count=%u",
                 selector->selected_count);
        update_buttons_state(selector);

        // Log button state after update
        if (selector->flash_btn) {
            bool is_disabled = lv_obj_has_state(selector->flash_btn, LV_STATE_DISABLED);
            ESP_LOGI(TAG, "Modal OK: Flash button is now %s (state check)",
                     is_disabled ? "DISABLED" : "ENABLED");
        }

        // Switch back to main screen and refresh it
        switch_screen(SCREEN_MAIN);

        refresh_main_screen();

        ESP_LOGI(TAG, "Modal closed, main screen refreshed");
    } else {
        ESP_LOGE(TAG, "Modal OK: completion_modal is NULL!");
    }
}

// LVGL status callback for firmware flashing completion
static void fw_flash_status_callback(flash_state_t state, flash_result_t result, const char* status_message)
{
    ESP_LOGI(TAG, "Flash Status: state=%d, result=%d, message=%s", state, result, status_message ? status_message : "NULL");
    ESP_LOGD(TAG, "Active selector: %p, completion_modal: %p, completion_label: %p",
             g_active_firmware_selector,
             g_active_firmware_selector ? g_active_firmware_selector->completion_modal : NULL,
             g_active_firmware_selector ? g_active_firmware_selector->completion_label : NULL);

    // Update status label based on current state
    if (g_active_firmware_selector && g_active_firmware_selector->status_label) {
        switch (state) {
            case FLASH_STATE_INITIALIZING:
                lv_label_set_text(g_active_firmware_selector->status_label, "Initializing");
                break;
            case FLASH_STATE_BACKING_UP:
                lv_label_set_text(g_active_firmware_selector->status_label, "Backing up");
                break;
            case FLASH_STATE_WRITING_PARTITION_TABLE:
                lv_label_set_text(g_active_firmware_selector->status_label, "Writing partition table");
                break;
            case FLASH_STATE_FLASHING_FIRMWARE:
                // Use the status message for firmware flashing state if available
                if (status_message && strlen(status_message) > 0) {
                    lv_label_set_text(g_active_firmware_selector->status_label, status_message);
                } else {
                    lv_label_set_text(g_active_firmware_selector->status_label, "Flashing");
                }
                break;
            case FLASH_STATE_VERIFYING:
                lv_label_set_text(g_active_firmware_selector->status_label, "Verifying");
                break;
            case FLASH_STATE_CLEANING_UP:
                lv_label_set_text(g_active_firmware_selector->status_label, "Cleaning up");
                break;
            case FLASH_STATE_COMPLETED:
                lv_label_set_text(g_active_firmware_selector->status_label, "Ready");
                break;
            case FLASH_STATE_ERROR:
                lv_label_set_text(g_active_firmware_selector->status_label, "Error");
                break;
            default:
                lv_label_set_text(g_active_firmware_selector->status_label, "Ready");
                break;
        }
    }

    // NOTE: Completion handling moved to firmware_selector_handle_flash_complete()
    // This is now called via UI update queue to prevent LVGL calls from flash_task
}

// Handle flash completion - called from UI update queue in lvgl_task context
void firmware_selector_handle_flash_complete(bool success)
{
    ESP_LOGI(TAG, "Handling flash completion: success=%d", success);

    // Clear the flashing in progress state
    flashing_in_progress = false;
    ESP_LOGI(TAG, "flashing_in_progress set to false");

    // Re-enable flash button by updating button states
    if (g_active_firmware_selector) {
        ESP_LOGI(TAG, "Updating button states to re-enable flash button, selected_count=%u",
                 g_active_firmware_selector->selected_count);
        update_buttons_state(g_active_firmware_selector);

        // Log button state after update
        if (g_active_firmware_selector->flash_btn) {
            bool is_disabled = lv_obj_has_state(g_active_firmware_selector->flash_btn, LV_STATE_DISABLED);
            ESP_LOGI(TAG, "Flash button is now %s",
                     is_disabled ? "DISABLED" : "ENABLED");
        }

        // Hide progress bar and label when flashing is complete
        if (g_active_firmware_selector->progress_bar) {
            lv_obj_add_flag(g_active_firmware_selector->progress_bar, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Hiding progress bar");
        }
        if (g_active_firmware_selector->progress_label) {
            lv_obj_add_flag(g_active_firmware_selector->progress_label, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Hiding progress label");
        }

        // Reset progress bar to 0 for next use
        if (g_active_firmware_selector->progress_bar) {
            lv_bar_set_value(g_active_firmware_selector->progress_bar, 0, LV_ANIM_OFF);
        }
        if (g_active_firmware_selector->progress_label) {
            lv_label_set_text(g_active_firmware_selector->progress_label, "0%");
        }

        // When flashing completes successfully, show completion modal
        if (success) {
            ESP_LOGI(TAG, "Firmware flashing completed successfully");

            // TODO: Show completion modal and refresh firmware list
            // For now, the flash button is re-enabled which is the most important part
        }
    }
}

// Update firmware selector progress bar (called from lvgl_task only via ui_update)
void firmware_selector_update_progress_bar(uint8_t percentage)
{
    extern firmware_selector_t* g_active_firmware_selector;
    extern void lock_display(void);
    extern void unlock_display(void);

    if (g_active_firmware_selector && g_active_firmware_selector->progress_bar && g_active_firmware_selector->progress_label) {
        lock_display();

        // Show progress elements (they might be hidden from initial state)
        lv_obj_clear_flag(g_active_firmware_selector->progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_active_firmware_selector->progress_label, LV_OBJ_FLAG_HIDDEN);

        // Update progress bar
        lv_bar_set_value(g_active_firmware_selector->progress_bar, percentage, LV_ANIM_OFF);

        // Update percentage label
        char progress_text[16];
        snprintf(progress_text, sizeof(progress_text), "%d%%", percentage);
        lv_label_set_text(g_active_firmware_selector->progress_label, progress_text);

        unlock_display();
    }
}

// LVGL progress callback for firmware flashing (NO LONGER USED - replaced by queue mechanism)
static void fw_flash_progress_callback(uint32_t current_firmware, uint32_t total_firmwares,
                                   uint32_t current_progress, uint32_t total_progress, const char* status_message)
{
    ESP_LOGI(TAG, "Flash Progress: %lu/%lu, %lu/%lu - %s",
             (unsigned long)current_firmware, (unsigned long)total_firmwares,
             (unsigned long)current_progress, (unsigned long)total_progress, status_message ? status_message : "NULL");

    // Update firmware selector progress bar and percentage if available
    extern firmware_selector_t* g_active_firmware_selector; // Global reference to active selector
    ESP_LOGD(TAG, "Active selector: %p, progress_bar: %p, progress_label: %p",
             g_active_firmware_selector,
             g_active_firmware_selector ? g_active_firmware_selector->progress_bar : NULL,
             g_active_firmware_selector ? g_active_firmware_selector->progress_label : NULL);

    if (g_active_firmware_selector && g_active_firmware_selector->progress_bar && g_active_firmware_selector->progress_label) {
        // Show progress elements (they might be hidden from initial state)
        lv_obj_clear_flag(g_active_firmware_selector->progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_active_firmware_selector->progress_label, LV_OBJ_FLAG_HIDDEN);

        if (total_progress > 0) {
            uint8_t percentage = (uint8_t)((current_progress * 100) / total_progress);

            ESP_LOGD(TAG, "Updating progress bar to %d%%", percentage);

            // Update progress bar
            lv_bar_set_value(g_active_firmware_selector->progress_bar, percentage, LV_ANIM_OFF);

            // Update percentage label
            char progress_text[16];
            snprintf(progress_text, sizeof(progress_text), "%d%%", percentage);
            lv_label_set_text(g_active_firmware_selector->progress_label, progress_text);

            ESP_LOGD(TAG, "Progress updated: bar=%d, text=%s", percentage, progress_text);
        }
    } else {
        // Fallback to global progress bar if firmware selector not available
        if (total_progress > 0) {
            uint8_t percentage = (uint8_t)((current_progress * 100) / total_progress);
            update_progress_bar(percentage);
        }
    }

    // Update status message
    if (status_message) {
        char full_status[256];
        if (total_firmwares > 1) {
            snprintf(full_status, sizeof(full_status), "Flashing %lu/%lu: %s",
                     (unsigned long)current_firmware, (unsigned long)total_firmwares, status_message);
        } else {
            snprintf(full_status, sizeof(full_status), "%s", status_message);
        }
        update_status(full_status);
    }
}

static void fw_selector_back_cb(lv_event_t* e)
{
    firmware_selector_t* selector = (firmware_selector_t*)lv_event_get_user_data(e);
    if (selector) {
        ESP_LOGI(TAG, "Firmware selector back button pressed");
        firmware_selector_hide(selector);

        // Return to main screen by switching to SCREEN_MAIN
        switch_screen(SCREEN_MAIN);
    }
}

static void update_firmware_list_item(firmware_selector_t* selector, uint32_t index)
{
    if (!selector || index >= selector->firmware_count) {
        return;
    }

    firmware_info_t* fw = &selector->firmware_list[index];
    if (!fw->list_item) {
        return;
    }

    // Create item text with selection status and info - use smaller buffer
    char item_text[256];  // Reduced from 512 to prevent stack issues
    char size_str[32];

    // Only format size if file has a reasonable size
    if (fw->size > 0) {
        firmware_format_size(fw->size, size_str, sizeof(size_str));

        if (fw->is_valid) {
            snprintf(item_text, sizeof(item_text), "%s %s (%s)",
                     fw->is_selected ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                     fw->display_name,
                     size_str);
        } else {
            snprintf(item_text, sizeof(item_text), "%s %s (%s) Invalid",
                     fw->is_selected ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                     fw->display_name,
                     size_str);
        }
    } else {
        snprintf(item_text, sizeof(item_text), "%s %s",
                 fw->is_selected ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                 fw->display_name);
    }

    // Update list button text - safer approach
    lv_obj_t* label = lv_obj_get_child(fw->list_item, 0);
    if (label && lv_obj_check_type(label, &lv_label_class)) {
        lv_label_set_text(label, item_text);
    }

    // Update visual style for selected items
    if (fw->is_selected) {
        // Selected items get green background and white text
        lv_obj_set_style_bg_color(fw->list_item, lv_color_hex(0x00aa00), 0);
        lv_obj_set_style_border_color(fw->list_item, lv_color_hex(0x007700), 0);
        lv_obj_set_style_border_width(fw->list_item, 2, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    } else {
        // Unselected items get darker styling for black background
        lv_obj_set_style_bg_color(fw->list_item, lv_color_hex(0x2c2c2c), 0); // Dark gray
        lv_obj_set_style_border_color(fw->list_item, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(fw->list_item, 1, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xcccccc), 0); // Light text
    }
}

static void update_buttons_state(firmware_selector_t* selector)
{
    if (!selector) {
        ESP_LOGE(TAG, "update_buttons_state: selector is NULL!");
        return;
    }

    // Enable/disable flash button based on selection and flashing state
    bool has_selection = (selector->selected_count > 0);
    bool should_disable = !has_selection || flashing_in_progress;

    ESP_LOGI(TAG, "update_buttons_state: selected_count=%u, has_selection=%d, flashing_in_progress=%d, should_disable=%d",
             selector->selected_count, has_selection, flashing_in_progress, should_disable);

    if (selector->flash_btn) {
        // Use add/remove state for LVGL 9 (proper API)
        if (should_disable) {
            lv_obj_add_state(selector->flash_btn, LV_STATE_DISABLED);
            ESP_LOGI(TAG, "update_buttons_state: Flash button DISABLED (state added)");
        } else {
            lv_obj_remove_state(selector->flash_btn, LV_STATE_DISABLED);
            ESP_LOGI(TAG, "update_buttons_state: Flash button ENABLED (state removed)");
        }

        // Verify the change
        bool is_disabled = lv_obj_has_state(selector->flash_btn, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "update_buttons_state: Flash button state check: %s",
                 is_disabled ? "DISABLED" : "ENABLED");
    } else {
        ESP_LOGE(TAG, "update_buttons_state: flash_btn is NULL!");
    }

    // Update total size label
    char size_text[128];
    char total_size_str[32];
    firmware_format_size(selector->total_selected_size, total_size_str, sizeof(total_size_str));

    bool fits_in_flash;
    firmware_selector_check_space(selector, &fits_in_flash);

    snprintf(size_text, sizeof(size_text), "Selected: %lu/%lu, Total: %s%s",
             (unsigned long)selector->selected_count, (unsigned long)selector->firmware_count,
             total_size_str,
             fits_in_flash ? "" : " (Too large!)");

    lv_label_set_text(selector->total_size_label, size_text);
}

esp_err_t firmware_selector_create_ui(firmware_selector_t* selector)
{
    if (!selector || !selector->is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Creating firmware selection UI");

    // Create main screen
    selector->screen = lv_obj_create(NULL);
    lv_obj_set_size(selector->screen, FW_SELECTOR_SCREEN_WIDTH, FW_SELECTOR_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(selector->screen, lv_color_black(), 0);

    // Create title - larger and more prominent for 1024px screen
    lv_obj_t* title = lv_label_create(selector->screen);
    lv_label_set_text(title, "Select Firmware Files");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); // Larger font
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Create custom scrollable firmware list using flexbox (lv_list causes freeze on macOS)
    // Create scrollable container with flexbox layout
    selector->list = lv_obj_create(selector->screen);

    // Set size FIRST before adding any children (prevents LVGL freeze)
    lv_obj_set_size(selector->list, FW_SELECTOR_SCREEN_WIDTH - 40, FW_LIST_HEIGHT);
    lv_obj_align(selector->list, LV_ALIGN_TOP_MID, 0, 60);

    // Set up flexbox layout for vertical list
    lv_obj_set_layout(selector->list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(selector->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(selector->list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Enable scrolling
    lv_obj_set_scrollbar_mode(selector->list, LV_SCROLLBAR_MODE_AUTO);

    // Style the container - CRITICAL: Disable borders to prevent macOS crash
    lv_obj_set_style_bg_color(selector->list, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(selector->list, 0, 0);           // CRITICAL: No borders
    lv_obj_set_style_border_opa(selector->list, LV_OPA_TRANSP, 0); // CRITICAL: Transparent
    lv_obj_set_style_pad_all(selector->list, 5, 0);
    lv_obj_set_style_radius(selector->list, 10, 0); // Rounded corners

    // Add firmware items to list - using custom buttons (not lv_list_add_btn)
    ESP_LOGI(TAG, "Adding %lu firmware items to list...", (unsigned long)selector->firmware_count);

    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        firmware_info_t* fw = &selector->firmware_list[i];

        ESP_LOGD(TAG, "Adding firmware %lu: %s", (unsigned long)i, fw->display_name);

        // Create button - CRITICAL: Follow macOS LVGL guidelines
        lv_obj_t* btn = lv_btn_create(selector->list);
        lv_obj_set_size(btn, FW_SELECTOR_SCREEN_WIDTH - 60, 50);

        // CRITICAL: Disable borders FIRST before any other operations (prevents crash)
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);

        // Set background color
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0D47A1), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        // Create label for firmware name
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, fw->display_name);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
        lv_obj_set_style_text_color(label, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

        // Add click callback with user data pointing to firmware index
        lv_obj_add_event_cb(btn, fw_selector_list_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        fw->list_item = btn;

        // Small delay every few items to let LVGL breathe
        if (i % 5 == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Create info panel - better positioning for 1024px screen
    selector->total_size_label = lv_label_create(selector->screen);
    lv_obj_set_style_text_color(selector->total_size_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(selector->total_size_label, &lv_font_montserrat_14, 0);
    lv_obj_align(selector->total_size_label, LV_ALIGN_BOTTOM_LEFT, 20, -100);

    // Create status label - larger and more visible
    selector->status_label = lv_label_create(selector->screen);
    lv_obj_set_style_text_color(selector->status_label, lv_color_hex(0x333333), 0); // Dark gray text
    lv_obj_set_style_text_font(selector->status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(selector->status_label, "Ready");
    lv_obj_align(selector->status_label, LV_ALIGN_BOTTOM_LEFT, 20, -100);

    // Create progress bar (initially hidden)
    selector->progress_bar = lv_bar_create(selector->screen);
    lv_obj_set_size(selector->progress_bar, 200, 20);
    lv_obj_align(selector->progress_bar, LV_ALIGN_BOTTOM_LEFT, 20, -70);
    lv_bar_set_range(selector->progress_bar, 0, 100);
    lv_bar_set_value(selector->progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(selector->progress_bar, LV_OBJ_FLAG_HIDDEN); // Initially hidden

    // Create progress percentage label
    selector->progress_label = lv_label_create(selector->screen);
    lv_obj_set_style_text_color(selector->progress_label, lv_color_hex(0x333333), 0); // Dark gray text
    lv_obj_set_style_text_font(selector->progress_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(selector->progress_label, "0%");
    lv_obj_align_to(selector->progress_label, selector->progress_bar, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_flag(selector->progress_label, LV_OBJ_FLAG_HIDDEN); // Initially hidden

    // Create button container - wider and better positioned
    lv_obj_t* btn_cont = lv_obj_create(selector->screen);
    lv_obj_set_size(btn_cont, FW_SELECTOR_SCREEN_WIDTH - 40, FW_BUTTON_HEIGHT); // More padding
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);

    // Select All button - larger for better touch experience
    selector->select_all_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(selector->select_all_btn, 150, FW_BUTTON_HEIGHT);
    lv_obj_align(selector->select_all_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_add_event_cb(selector->select_all_btn, fw_selector_select_all_cb, LV_EVENT_CLICKED, selector);
    lv_obj_t* label = lv_label_create(selector->select_all_btn);
    lv_label_set_text(label, "Select All");
    lv_obj_center(label);

    // Clear button
    selector->clear_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(selector->clear_btn, 100, FW_BUTTON_HEIGHT);
    lv_obj_align(selector->clear_btn, LV_ALIGN_LEFT_MID, 190, 0);
    lv_obj_add_event_cb(selector->clear_btn, fw_selector_clear_cb, LV_EVENT_CLICKED, selector);
    label = lv_label_create(selector->clear_btn);
    lv_label_set_text(label, "Clear");
    lv_obj_center(label);

    // View Partitions button
    lv_obj_t* view_parts_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(view_parts_btn, 120, FW_BUTTON_HEIGHT);
    lv_obj_align(view_parts_btn, LV_ALIGN_LEFT_MID, 310, 0);
    lv_obj_add_event_cb(view_parts_btn, fw_selector_view_partitions_cb, LV_EVENT_CLICKED, selector);
    lv_obj_set_style_bg_color(view_parts_btn, lv_color_hex(0x0D47A1), 0);
    label = lv_label_create(view_parts_btn);
    lv_label_set_text(label, "View Parts");
    lv_obj_center(label);

    // Flash button - larger and more prominent
    selector->flash_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(selector->flash_btn, 150, FW_BUTTON_HEIGHT);
    lv_obj_align(selector->flash_btn, LV_ALIGN_RIGHT_MID, -170, 0);
    lv_obj_add_event_cb(selector->flash_btn, fw_selector_flash_cb, LV_EVENT_CLICKED, selector);
    lv_obj_set_style_bg_color(selector->flash_btn, lv_color_hex(0x00aa00), 0);
    lv_obj_set_style_bg_grad_color(selector->flash_btn, lv_color_hex(0x00dd00), 0); // Gradient effect
    label = lv_label_create(selector->flash_btn);
    lv_label_set_text(label, "Flash");
    lv_obj_center(label);

    // Back button
    selector->back_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(selector->back_btn, 100, FW_BUTTON_HEIGHT);
    lv_obj_align(selector->back_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(selector->back_btn, fw_selector_back_cb, LV_EVENT_CLICKED, selector);
    label = lv_label_create(selector->back_btn);
    lv_label_set_text(label, "Back");
    lv_obj_center(label);

    // Create completion modal (initially hidden)
    selector->completion_modal = lv_obj_create(selector->screen);
    lv_obj_set_size(selector->completion_modal, 400, 200);
    lv_obj_center(selector->completion_modal);
    lv_obj_set_style_bg_color(selector->completion_modal, lv_color_hex(0x2c2c2c), 0);
    lv_obj_set_style_border_color(selector->completion_modal, lv_color_hex(0x00aa00), 0);
    lv_obj_set_style_border_width(selector->completion_modal, 3, 0);
    lv_obj_set_style_radius(selector->completion_modal, 15, 0);
    lv_obj_add_flag(selector->completion_modal, LV_OBJ_FLAG_HIDDEN); // Initially hidden

    // Create completion label
    selector->completion_label = lv_label_create(selector->completion_modal);
    lv_obj_set_style_text_color(selector->completion_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(selector->completion_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(selector->completion_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(selector->completion_label, LV_ALIGN_TOP_MID, 0, 30);
    lv_label_set_text(selector->completion_label, "Flashing completed successfully!");

    // Create OK button for modal
    lv_obj_t* ok_btn = lv_btn_create(selector->completion_modal);
    lv_obj_set_size(ok_btn, 80, 40);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x00aa00), 0);
    lv_obj_add_event_cb(ok_btn, fw_selector_modal_ok_cb, LV_EVENT_CLICKED, selector);

    label = lv_label_create(ok_btn);
    lv_label_set_text(label, "OK");
    lv_obj_center(label);

    // Update UI state
    update_buttons_state(selector);

    ESP_LOGI(TAG, "Firmware selection UI created successfully");
    return ESP_OK;
}

esp_err_t firmware_selector_show(firmware_selector_t* selector)
{
    if (!selector || !selector->screen) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Showing firmware selection screen");

    // Set global reference for progress updates
    g_active_firmware_selector = selector;

    // Show progress bar and label (they might have been hidden from previous session)
    if (selector->progress_bar) {
        lv_obj_clear_flag(selector->progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (selector->progress_label) {
        lv_obj_clear_flag(selector->progress_label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_scr_load(selector->screen);
    return ESP_OK;
}

esp_err_t firmware_selector_hide(firmware_selector_t* selector)
{
    if (!selector || !selector->screen) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Hiding firmware selection screen");

    // Clear global reference when hiding
    if (g_active_firmware_selector == selector) {
        g_active_firmware_selector = NULL;
    }

    // Screen will be hidden when another screen is loaded
    return ESP_OK;
}

esp_err_t firmware_selector_toggle_selection(firmware_selector_t* selector, uint32_t index)
{
    if (!selector || index >= selector->firmware_count) {
        return ESP_ERR_INVALID_ARG;
    }

    firmware_info_t* fw = &selector->firmware_list[index];
    if (!fw->is_valid) {
        ESP_LOGW(TAG, "Cannot select invalid firmware: %s", fw->display_name);
        return ESP_ERR_INVALID_STATE;
    }

    // Toggle selection
    fw->is_selected = !fw->is_selected;

    // Update counters
    if (fw->is_selected) {
        selector->selected_count++;
        selector->total_selected_size += fw->size;
    } else {
        selector->selected_count--;
        selector->total_selected_size -= fw->size;
    }

    // Update UI
    update_firmware_list_item(selector, index);
    update_buttons_state(selector);

    ESP_LOGI(TAG, "Toggled selection for %s: %s, Total selected: %d (%d bytes)",
             fw->display_name, fw->is_selected ? "SELECTED" : "DESELECTED",
             selector->selected_count, selector->total_selected_size);

    return ESP_OK;
}

esp_err_t firmware_selector_select_all(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Selecting all valid firmware files");

    selector->selected_count = 0;
    selector->total_selected_size = 0;

    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        firmware_info_t* fw = &selector->firmware_list[i];
        if (fw->is_valid) {
            fw->is_selected = true;
            selector->selected_count++;
            selector->total_selected_size += fw->size;
            update_firmware_list_item(selector, i);
        }
    }

    update_buttons_state(selector);

    ESP_LOGI(TAG, "Selected all valid firmwares: %d files, %d bytes total",
             selector->selected_count, selector->total_selected_size);

    return ESP_OK;
}

esp_err_t firmware_selector_clear_selection(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Clearing all firmware selections");

    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        selector->firmware_list[i].is_selected = false;
        update_firmware_list_item(selector, i);
    }

    selector->selected_count = 0;
    selector->total_selected_size = 0;

    update_buttons_state(selector);

    ESP_LOGI(TAG, "Cleared all firmware selections");
    return ESP_OK;
}

esp_err_t firmware_selector_check_space(firmware_selector_t* selector, bool* fits)
{
    if (!selector || !fits) {
        return ESP_ERR_INVALID_ARG;
    }

    *fits = (selector->total_selected_size <= AVAILABLE_FLASH_SPACE);

    ESP_LOGD(TAG, "Space check: %d bytes selected, %d bytes available, %s",
             selector->total_selected_size, AVAILABLE_FLASH_SPACE,
             *fits ? "FITS" : "DOES NOT FIT");

    return ESP_OK;
}

esp_err_t firmware_selector_get_selected(firmware_selector_t* selector,
                                       firmware_info_t** selected_list,
                                       uint32_t max_count,
                                       uint32_t* count)
{
    if (!selector || !selected_list || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t selected_idx = 0;
    for (uint32_t i = 0; i < selector->firmware_count && selected_idx < max_count; i++) {
        if (selector->firmware_list[i].is_selected) {
            selected_list[selected_idx] = &selector->firmware_list[i];
            selected_idx++;
        }
    }

    *count = selected_idx;

    ESP_LOGI(TAG, "Retrieved %lu selected firmwares", (unsigned long)*count);
    return ESP_OK;
}

firmware_info_t* firmware_selector_get_firmware(firmware_selector_t* selector, uint32_t index)
{
    if (!selector || index >= selector->firmware_count) {
        return NULL;
    }

    return &selector->firmware_list[index];
}

esp_err_t firmware_selector_update_size_display(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    update_buttons_state(selector);
    return ESP_OK;
}

esp_err_t firmware_selector_cleanup(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Cleaning up firmware selector");

    // Note: LVGL objects will be cleaned up by LVGL when screen is destroyed
    // Just reset our state
    memset(selector, 0, sizeof(firmware_selector_t));

    return ESP_OK;
}

esp_err_t firmware_selector_store_firmware_config(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Storing firmware configuration in firmware_storage partition for boot menu");

    // Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    esp_err_t err = firmware_selector_get_selected(selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get selected firmware: %s", esp_err_to_name(err));
        return err;
    }

    // Clear existing firmware storage by erasing the header
    // This will re-initialize the storage on first add
    firmware_storage_header_t empty_header;
    memset(&empty_header, 0, sizeof(empty_header));
    // Write invalid magic to clear storage
    memcpy(empty_header.magic, "CLR_", 4);
    esp_flash_write(NULL, &empty_header, FIRMWARE_STORAGE_OFFSET, sizeof(empty_header));

    // Store each selected firmware
    for (uint32_t i = 0; i < selected_count; i++) {
        firmware_info_t* firmware = selected_firmware[i];

        if (!firmware->assigned_partition) {
            ESP_LOGW(TAG, "Skipping firmware %s - no assigned partition", firmware->display_name);
            continue;
        }

        partition_info_t* partition = (partition_info_t*)firmware->assigned_partition;

        // Calculate offset from firmware_storage base to OTA partition
        uint32_t partition_offset = partition->offset - FIRMWARE_STORAGE_OFFSET;

        // Add entry to firmware_storage
        err = firmware_storage_add_entry(firmware->display_name, partition_offset, firmware->size, firmware->crc32);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store firmware %d in storage: %s", (int)i, esp_err_to_name(err));
            continue;
        }

        ESP_LOGI(TAG, "Stored firmware %d: %s -> %s (offset: 0x%x, %d bytes, CRC32: 0x%08X)",
                 (int)i, firmware->display_name, partition->name, partition_offset, firmware->size, firmware->crc32);
    }

    ESP_LOGI(TAG, "✓ Stored %u firmware configuration(s) in firmware_storage", selected_count);
    return ESP_OK;
}

esp_err_t firmware_selector_scan_storage(firmware_selector_t* selector)
{
    if (!selector || !selector->is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Scanning firmware storage area...");

    // Check if firmware storage exists
    bool storage_valid = false;
    esp_err_t ret = firmware_storage_check_valid(&storage_valid);
    if (ret != ESP_OK || !storage_valid) {
        ESP_LOGI(TAG, "No valid firmware storage found");
        return ESP_ERR_NOT_FOUND;
    }

    // Get firmware count
    uint32_t firmware_count = 0;
    ret = firmware_storage_get_count(&firmware_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get firmware count from storage");
        return ret;
    }

    ESP_LOGI(TAG, "Found %u firmwares in storage", firmware_count);

    // Add each firmware to the list
    for (uint32_t i = 0; i < firmware_count && selector->firmware_count < MAX_FIRMWARE_COUNT; i++) {
        firmware_storage_entry_t entry;
        ret = firmware_storage_get_entry(i, &entry);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get firmware entry %u", i);
            continue;
        }

        // Create firmware info entry
        firmware_info_t* firmware = &selector->firmware_list[selector->firmware_count];
        memset(firmware, 0, sizeof(firmware_info_t));

        // Copy display name
        strncpy(firmware->display_name, entry.name, sizeof(firmware->display_name) - 1);
        firmware->display_name[sizeof(firmware->display_name) - 1] = '\0';

        // Set filename (use display name as filename)
        strncpy(firmware->filename, entry.name, sizeof(firmware->filename) - 1);
        firmware->filename[sizeof(firmware->filename) - 1] = '\0';

        // Set file path to indicate firmware storage
        snprintf(firmware->file_path, sizeof(firmware->file_path),
                 "flash://0x%08X", FIRMWARE_STORAGE_OFFSET);

        // Copy metadata
        firmware->size = entry.size;
        firmware->crc32 = entry.crc32;
        firmware->is_valid = true;
        firmware->is_selected = false;
        firmware->assigned_partition = NULL;

        ESP_LOGI(TAG, "  [%u] %s (%u bytes, CRC32: 0x%08X)",
                 selector->firmware_count, firmware->display_name,
                 firmware->size, firmware->crc32);

        selector->firmware_count++;
    }

    ESP_LOGI(TAG, "Firmware storage scan complete: %u firmwares added",
             selector->firmware_count);

    return ESP_OK;
}
