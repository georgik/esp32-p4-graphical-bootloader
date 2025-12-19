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
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "firmware_selector";

// Available flash space for firmwares (16MB total - bootloader - partitions)
#define AVAILABLE_FLASH_SPACE (16 * 1024 * 1024 - 0x100000)

// LVGL event callbacks
static void fw_selector_list_event_cb(lv_event_t* e);
static void fw_selector_select_all_cb(lv_event_t* e);
static void fw_selector_clear_cb(lv_event_t* e);
static void fw_selector_flash_cb(lv_event_t* e);
static void fw_selector_back_cb(lv_event_t* e);
static void fw_flash_progress_callback(uint32_t current_firmware, uint32_t total_firmwares,
                                       uint32_t current_progress, uint32_t total_progress, const char* status_message);

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

    // Ensure firmware directory exists
    struct stat st;
    if (stat(FIRMWARE_DIRECTORY, &st) != 0) {
        ESP_LOGW(TAG, "Firmware directory not found: %s", FIRMWARE_DIRECTORY);
        // Note: Directory creation could be added here if needed
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

    // Find firmware index
    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        if (selector->firmware_list[i].list_item == obj) {
            firmware_selector_toggle_selection(selector, i);
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

        // Check if total size fits in available space
        bool fits_in_flash = false;
        esp_err_t ret = firmware_selector_check_space(selector, &fits_in_flash);
        if (ret != ESP_OK || !fits_in_flash) {
            ESP_LOGE(TAG, "Selected firmwares don't fit in available flash space");
            return;
        }

        ESP_LOGI(TAG, "Starting partition generation and flashing for %d firmwares (%lu total bytes)",
                 selector->selected_count, (unsigned long)selector->total_selected_size);

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
        flash_config.status_callback = NULL;   // Use default status handling

        // Start flashing
        ret = firmware_flasher_start(&flash_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start firmware flashing: %s", esp_err_to_name(ret));
            return;
        }

        ESP_LOGI(TAG, "Firmware flashing operation started");
    }
}

// LVGL progress callback for firmware flashing
static void fw_flash_progress_callback(uint32_t current_firmware, uint32_t total_firmwares,
                                   uint32_t current_progress, uint32_t total_progress, const char* status_message)
{
    ESP_LOGD(TAG, "Flash Progress: %lu/%lu, %lu%% - %s",
             (unsigned long)current_firmware, (unsigned long)total_firmwares,
             (unsigned long)current_progress, status_message);

    // Update LVGL progress bar on screen
    if (total_progress > 0) {
        uint8_t percentage = (uint8_t)((current_progress * 100) / total_progress);
        update_progress_bar(percentage);
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
}

static void update_buttons_state(firmware_selector_t* selector)
{
    if (!selector) {
        return;
    }

    // Enable/disable flash button based on selection
    bool has_selection = (selector->selected_count > 0);
    lv_obj_set_state(selector->flash_btn, LV_STATE_DISABLED, !has_selection);

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
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); // Larger font
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Create firmware list - optimized for 1024px width
    selector->list = lv_list_create(selector->screen);
    lv_obj_set_size(selector->list, FW_SELECTOR_SCREEN_WIDTH - 40, FW_LIST_HEIGHT); // More padding
    lv_obj_align(selector->list, LV_ALIGN_TOP_MID, 0, 60); // More space for title
    lv_obj_set_style_bg_color(selector->list, lv_color_hex(0x303030), 0);
    lv_obj_set_style_border_width(selector->list, 2, 0);
    lv_obj_set_style_border_color(selector->list, lv_color_hex(0x606060), 0);
    lv_obj_set_style_radius(selector->list, 10, 0); // Rounded corners

    // Add firmware items to list - with small delays to prevent LVGL overload
    ESP_LOGI(TAG, "Adding %lu firmware items to list...", (unsigned long)selector->firmware_count);

    for (uint32_t i = 0; i < selector->firmware_count; i++) {
        firmware_info_t* fw = &selector->firmware_list[i];

        ESP_LOGD(TAG, "Adding firmware %lu: %s", (unsigned long)i, fw->display_name);

        // Create list item with simple name first
        lv_obj_t* btn = lv_list_add_btn(selector->list, LV_SYMBOL_FILE, fw->display_name);
        if (!btn) {
            ESP_LOGE(TAG, "Failed to create list button for firmware %lu", (unsigned long)i);
            continue;
        }

        fw->list_item = btn;

        // Set click callback
        lv_obj_add_event_cb(btn, fw_selector_list_event_cb, LV_EVENT_CLICKED, selector);

        // Update item display after creation
        update_firmware_list_item(selector, i);

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
    lv_obj_set_style_text_color(selector->status_label, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_text_font(selector->status_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(selector->status_label, "Ready");
    lv_obj_align(selector->status_label, LV_ALIGN_BOTTOM_LEFT, 20, -70);

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
    lv_scr_load(selector->screen);
    return ESP_OK;
}

esp_err_t firmware_selector_hide(firmware_selector_t* selector)
{
    if (!selector || !selector->screen) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Hiding firmware selection screen");
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

    ESP_LOGI(TAG, "Storing firmware configuration in NVS for boot menu");

    // Initialize NVS system first (in case not already initialized)
    esp_err_t nvs_init_err = nvs_flash_init();
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