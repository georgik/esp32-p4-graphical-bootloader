/**
 * @file firmware_selector.c
 * @brief Firmware selection and management implementation
 */

#include "firmware_selector.h"
#include "firmware_validator.h"
#include "partition_manager.h"
#include "firmware_flasher.h"
#include "lvgl_bootloader.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
static void fw_selector_flash_cb(lv_event_t* e);
static void fw_selector_back_cb(lv_event_t* e);
static void fw_selector_modal_ok_cb(lv_event_t* e);
static void fw_flash_progress_callback(uint32_t current_firmware, uint32_t total_firmwares,
                                       uint32_t current_progress, uint32_t total_progress, const char* status_message);
static void fw_flash_status_callback(flash_state_t state, flash_result_t result, const char* status_message);

static void update_firmware_list_item(firmware_selector_t* selector, uint32_t index);
static void update_firmware_list_item_text_only(firmware_selector_t* selector, uint32_t index);
static void update_buttons_state(firmware_selector_t* selector);

esp_err_t firmware_selector_init(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing firmware selector");

    // Initialize structure
    memset(selector, 0, sizeof(firmware_selector_t));

    // Ensure firmware directory exists
    struct stat st;
    if (stat(FIRMWARE_DIRECTORY, &st) != 0) {
        ESP_LOGW(TAG, "Firmware directory not found: %s, creating it...", FIRMWARE_DIRECTORY);
        // Create directory with parents
        #ifdef __SIMULATOR_BUILD__
            // On simulator, use system command to create with parents
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", FIRMWARE_DIRECTORY);
            system(cmd);
        #else
            // On hardware, create with parents (may fail if parent doesn't exist)
            mkdir(FIRMWARE_DIRECTORY, 0755);
        #endif
        ESP_LOGI(TAG, "Firmware directory created: %s", FIRMWARE_DIRECTORY);
    } else {
        ESP_LOGI(TAG, "Firmware directory found: %s", FIRMWARE_DIRECTORY);
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
        return ESP_ERR_NOT_FOUND;
    }

    selector->firmware_count = 0;
    selector->selected_count = 0;
    selector->total_selected_size = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && selector->firmware_count < MAX_FIRMWARE_COUNT) {
        // Skip hidden files (starting with .) - macOS creates these metadata files
        if (entry->d_name[0] == '.') {
            ESP_LOGD(TAG, "Skipping hidden file: %s", entry->d_name);
            continue;
        }

        // Check for .bin extension
        if (!firmware_has_valid_extension(entry->d_name)) {
            continue;
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

    ESP_LOGI(TAG, "Firmware scan complete: %lu files found", (unsigned long)selector->firmware_count);
    return ESP_OK;
}

static void fw_selector_list_event_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target(e);
    firmware_selector_t* selector = (firmware_selector_t*)lv_event_get_user_data(e);

    if (!selector) {
        return;
    }

    ESP_LOGI(TAG, "List item clicked - obj: %p", (void*)obj);

    // Find firmware index
    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        if (selector->firmware_list[i].list_item == obj) {
            ESP_LOGI(TAG, "Clicked firmware %lu: %s", (unsigned long)i, selector->firmware_list[i].display_name);

            // Toggle selection state
            selector->firmware_list[i].is_selected = !selector->firmware_list[i].is_selected;

            // Update the icon prefix
            char item_text[256];
            snprintf(item_text, sizeof(item_text), "%s %s",
                     selector->firmware_list[i].is_selected ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                     selector->firmware_list[i].display_name);

            // Get the label and update it
            lv_obj_t* label = lv_obj_get_child(obj, 0);
            if (label && lv_obj_check_type(label, &lv_label_class)) {
                ESP_LOGD(TAG, "Updating label text to: %s", item_text);
                lv_label_set_text(label, item_text);

                // CRITICAL: Re-align the label after text update to prevent garbled display
                // The label position gets messed up when text changes, so we re-center it
                lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

                // Force a redraw of this specific button
                lv_obj_invalidate(obj);
            } else {
                ESP_LOGW(TAG, "Label not found or wrong type (label=%p)", (void*)label);
            }

            // Remove checked/pressed/focused states to prevent visual artifacts
            lv_obj_remove_state(obj, LV_STATE_PRESSED);
            lv_obj_remove_state(obj, LV_STATE_CHECKED);
            lv_obj_remove_state(obj, LV_STATE_FOCUSED);

            break;
        }
    }
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
    if (selector && selector->completion_modal) {
        // Hide modal
        lv_obj_add_flag(selector->completion_modal, LV_OBJ_FLAG_HIDDEN);

        // Switch back to main screen and refresh it
        switch_screen(SCREEN_MAIN);

        // Small delay to ensure NVS operations are complete
        vTaskDelay(pdMS_TO_TICKS(100));

        refresh_main_screen();

        ESP_LOGI(TAG, "Modal closed, main screen refreshed");
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

    // When flashing completes (successfully or not), clear the flashing in progress state
    if (state == FLASH_STATE_COMPLETED) {
        flashing_in_progress = false;
        ESP_LOGI(TAG, "Flashing completed, re-enabling UI controls");

        // Re-enable flash button by updating button states
        if (g_active_firmware_selector) {
            ESP_LOGI(TAG, "Updating button states to re-enable flash button");
            update_buttons_state(g_active_firmware_selector);

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
        }

        // When flashing completes successfully, show completion modal
        if (result == FLASH_RESULT_SUCCESS) {
            ESP_LOGI(TAG, "Firmware flashing completed successfully, showing completion modal");

            // Show completion modal with success message
            if (g_active_firmware_selector && g_active_firmware_selector->completion_modal &&
                g_active_firmware_selector->completion_label) {

                ESP_LOGI(TAG, "Creating success message for modal");
                char success_msg[256];
                snprintf(success_msg, sizeof(success_msg), "Flashing completed successfully!\n%d firmware(s) flashed",
                        (int)g_active_firmware_selector->selected_count);

                ESP_LOGI(TAG, "Setting modal text: %s", success_msg);
                lv_label_set_text(g_active_firmware_selector->completion_label, success_msg);

                ESP_LOGI(TAG, "Clearing hidden flag from modal");
                lv_obj_clear_flag(g_active_firmware_selector->completion_modal, LV_OBJ_FLAG_HIDDEN);

                // Bring modal to front
                lv_obj_move_foreground(g_active_firmware_selector->completion_modal);

                ESP_LOGI(TAG, "Completion modal shown successfully");

                // Force NVS reload to ensure main screen can read updated data
                ESP_LOGI(TAG, "Forcing NVS data reload...");
                esp_err_t nvs_err = nvs_flash_deinit();
                if (nvs_err == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(100)); // Slightly longer delay to ensure all writes complete
                    nvs_err = nvs_flash_init();
                    if (nvs_err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to reinitialize NVS: %s", esp_err_to_name(nvs_err));
                    } else {
                        ESP_LOGI(TAG, "NVS reinitialized successfully, data should be reloaded");
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to deinit NVS: %s", esp_err_to_name(nvs_err));
                }
            }
        } else {
            ESP_LOGW(TAG, "Firmware flashing completed with errors: result=%d", result);

            // Show error modal
            if (g_active_firmware_selector && g_active_firmware_selector->completion_modal &&
                g_active_firmware_selector->completion_label) {

                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "Flashing failed!\nError code: %d\nPlease check the logs", result);
                lv_label_set_text(g_active_firmware_selector->completion_label, error_msg);

                // Change modal color to red for error
                // REMOVED: Border style causes crash in LVGL border rendering
                lv_obj_set_style_bg_color(g_active_firmware_selector->completion_modal, lv_color_hex(0xaa0000), 0);

                lv_obj_clear_flag(g_active_firmware_selector->completion_modal, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// LVGL progress callback for firmware flashing
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
        // Unselected items get lighter styling for white background
        lv_obj_set_style_bg_color(fw->list_item, lv_color_hex(0xe0e0e0), 0); // Light gray
        lv_obj_set_style_border_color(fw->list_item, lv_color_hex(0xcccccc), 0);
        lv_obj_set_style_border_width(fw->list_item, 1, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x333333), 0); // Dark text
    }
}

// Safe version that only updates text, no styles (to avoid LVGL freeze)
static void update_firmware_list_item_text_only(firmware_selector_t* selector, uint32_t index)
{
    if (!selector || index >= selector->firmware_count) {
        return;
    }

    firmware_info_t* fw = &selector->firmware_list[index];
    if (!fw->list_item) {
        return;
    }

    // Create item text with just icon and name (no size to avoid truncation)
    char item_text[256];
    snprintf(item_text, sizeof(item_text), "%s %s",
             fw->is_selected ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
             fw->display_name);

    // Update the label text
    lv_obj_t* label = lv_obj_get_child(fw->list_item, 0);
    if (label && lv_obj_check_type(label, &lv_label_class)) {
        lv_label_set_text(label, item_text);

        // CRITICAL: Re-align the label after text update to prevent garbled display
        // The label position gets messed up when text changes, so we re-center it
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

        // Force a redraw of this specific button
        lv_obj_invalidate(fw->list_item);
    }
}

static void update_buttons_state(firmware_selector_t* selector)
{
    if (!selector) {
        return;
    }

    // Enable/disable flash button based on selection and flashing state
    bool has_selection = (selector->selected_count > 0);
    bool should_disable = !has_selection || flashing_in_progress;
    lv_obj_set_state(selector->flash_btn, LV_STATE_DISABLED, should_disable);

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
    // DON'T set size - let LVGL handle it automatically (like our working test screen)
    // lv_obj_set_size(selector->screen, FW_SELECTOR_SCREEN_WIDTH, FW_SELECTOR_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(selector->screen, lv_color_white(), 0);

    // Create title - larger and more prominent for 1024px screen
    lv_obj_t* title = lv_label_create(selector->screen);
    lv_label_set_text(title, "Select Firmware Files");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); // Larger font
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Create firmware list - use custom scrollable view instead of lv_list
    // lv_list_add_btn() creates buttons with default borders that crash on macOS
    // So we create a scrollable container and add buttons manually with full control
    ESP_LOGI(TAG, "Creating custom firmware list view");

    lv_obj_t* list_view = lv_obj_create(selector->screen);
    lv_obj_set_width(list_view, 650);
    lv_obj_set_height(list_view, 350);  // Fixed height for scrollable area
    lv_obj_align(list_view, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_scrollbar_mode(list_view, LV_SCROLLBAR_MODE_OFF);

    // Make it flexible column layout for buttons
    lv_obj_set_layout(list_view, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_view, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Disable borders on the container itself
    lv_obj_set_style_border_width(list_view, 0, 0);
    lv_obj_set_style_border_opa(list_view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(list_view, 5, 0);

    selector->list = list_view;  // Store for reference

    // Add firmware items as individual buttons
    ESP_LOGI(TAG, "Adding %lu firmware items to custom list view...", (unsigned long)selector->firmware_count);

    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        firmware_info_t* fw = &selector->firmware_list[i];

        ESP_LOGD(TAG, "Adding firmware %lu: %s", (unsigned long)i, fw->display_name);

        // Create button for each firmware
        lv_obj_t* btn = lv_btn_create(list_view);
        lv_obj_set_width(btn, 630);  // Slightly narrower than container
        lv_obj_set_height(btn, 50);  // Fixed height for each button

        // CRITICAL: Disable borders BEFORE adding to parent to avoid crash
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xf0f0f0), 0);

        // Create label with icon and text
        lv_obj_t* label = lv_label_create(btn);
        char item_text[256];
        snprintf(item_text, sizeof(item_text), "%s %s",
                 LV_SYMBOL_FILE, fw->display_name);
        lv_label_set_text(label, item_text);
        lv_obj_set_style_text_color(label, lv_color_black(), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

        fw->list_item = btn;

        // Set click callback
        lv_obj_add_event_cb(btn, fw_selector_list_event_cb, LV_EVENT_CLICKED, selector);

        ESP_LOGD(TAG, "Firmware %lu button created (custom style, no borders)", (unsigned long)i);

        // Small delay to let LVGL process each item
        if (i < selector->firmware_count - 1) {  // Don't delay after last item
            vTaskDelay(pdMS_TO_TICKS(50));  // 50ms between items
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
    // REMOVED: Border styles cause crash in LVGL's draw_border_complex()
    // The border rendering code crashes when drawing complex borders on macOS
    // lv_obj_set_style_border_color(selector->completion_modal, lv_color_hex(0x00aa00), 0);
    // lv_obj_set_style_border_width(selector->completion_modal, 3, 0);
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

// Create and load firmware selector screen in one step (to avoid freeze)
esp_err_t firmware_selector_create_and_load(firmware_selector_t* selector) {
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Creating and loading firmware selector in one step...");

    // Initialize the selector
    esp_err_t ret = firmware_selector_init(selector);
    if (ret != ESP_OK) {
        return ret;
    }

    // Scan directory
    ret = firmware_selector_scan_directory(selector);
    if (ret != ESP_OK) {
        return ret;
    }

    // Create UI
    ret = firmware_selector_create_ui(selector);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Loading firmware selector screen immediately...");
    ESP_LOGI(TAG, "→ Before lv_screen_load() - screen object: %p", (void*)selector->screen);
    lv_screen_load(selector->screen);
    ESP_LOGI(TAG, "← After lv_screen_load() - screen loaded, returning");
    ESP_LOGI(TAG, "Firmware selector screen loaded!");

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

    // Load the new screen - this should trigger automatic redraw
    lv_screen_load(selector->screen);

    ESP_LOGI(TAG, "Firmware selection screen shown successfully");

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

    // Update UI - use text-only version to avoid LVGL border drawing crash
    update_firmware_list_item_text_only(selector, index);
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
            update_firmware_list_item_text_only(selector, i);  // Safe text-only update
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
        update_firmware_list_item_text_only(selector, i);  // Safe text-only update
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

    // CRITICAL: Delete the screen object to prevent memory leaks
    // LVGL will automatically delete all child objects when the screen is deleted
    if (selector->screen) {
        ESP_LOGI(TAG, "Deleting firmware selector screen object: %p", (void*)selector->screen);

        lv_obj_delete(selector->screen);
        selector->screen = NULL;
        ESP_LOGI(TAG, "Screen object deleted");
    }

    // Note: firmware_list is a static array, not a pointer, so don't free it
    // Just reset the count
    selector->firmware_count = 0;

    // Reset our state
    memset(selector, 0, sizeof(firmware_selector_t));

    ESP_LOGI(TAG, "Firmware selector cleanup complete");

    return ESP_OK;
}

esp_err_t firmware_selector_store_firmware_config(firmware_selector_t* selector)
{
    if (!selector) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Storing firmware configuration in NVS for boot menu");

    // Initialize NVS system first (in case not already initialized)
    esp_err_t nvs_init_err = nvs_flash_init();
    if (nvs_init_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_init_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased, doing that...");
        nvs_flash_erase();
        nvs_init_err = nvs_flash_init();
    } else if (nvs_init_err == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGD(TAG, "NVS not initialized, trying to initialize...");
        nvs_init_err = nvs_flash_init();
    }

    if (nvs_init_err != ESP_OK && nvs_init_err != ESP_ERR_NVS_NO_FREE_PAGES && nvs_init_err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "Error initializing NVS flash: %s", esp_err_to_name(nvs_init_err));
        return nvs_init_err;
    }

    // Open NVS namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("firmware_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    err = firmware_selector_get_selected(selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get selected firmware: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Clear existing firmware entries
    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to erase NVS entries: %s", esp_err_to_name(err));
    }

    // Store each selected firmware
    char key[32];
    char value[256];
    for (uint32_t i = 0; i < selected_count; i++) {
        firmware_info_t* firmware = selected_firmware[i];

        if (!firmware->assigned_partition) {
            ESP_LOGW(TAG, "Skipping firmware %s - no assigned partition", firmware->display_name);
            continue;
        }

        partition_info_t* partition = (partition_info_t*)firmware->assigned_partition;

        // Store filename
        snprintf(key, sizeof(key), "fw_%d_filename", (int)i);
        err = nvs_set_str(nvs_handle, key, firmware->display_name);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store filename for firmware %d: %s", (int)i, esp_err_to_name(err));
            continue;
        }

        // Store OTA partition name
        snprintf(key, sizeof(key), "fw_%d_partition", (int)i);
        err = nvs_set_str(nvs_handle, key, partition->name);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store partition for firmware %d: %s", (int)i, esp_err_to_name(err));
            continue;
        }

        // Store OTA partition offset
        snprintf(key, sizeof(key), "fw_%d_offset", (int)i);
        err = nvs_set_u32(nvs_handle, key, partition->offset);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store offset for firmware %d: %s", (int)i, esp_err_to_name(err));
            continue;
        }

        // Store firmware size
        snprintf(key, sizeof(key), "fw_%d_size", (int)i);
        err = nvs_set_u32(nvs_handle, key, firmware->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store size for firmware %d: %s", (int)i, esp_err_to_name(err));
            continue;
        }

        // Store CRC32 for integrity checking
        snprintf(key, sizeof(key), "fw_%d_crc32", (int)i);
        err = nvs_set_u32(nvs_handle, key, firmware->crc32);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store CRC32 for firmware %d: %s", (int)i, esp_err_to_name(err));
            continue;
        }

        ESP_LOGI(TAG, "Stored firmware %d: %s -> %s (0x%08x, %d bytes, CRC32: 0x%08X)",
                 (int)i, firmware->display_name, partition->name, partition->offset, firmware->size, firmware->crc32);
    }

    // Store firmware count
    err = nvs_set_u32(nvs_handle, "firmware_count", selected_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store firmware count: %s", esp_err_to_name(err));
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Successfully stored %d firmware(s) in NVS", (int)selected_count);
    }

    nvs_close(nvs_handle);
    return err;
}
