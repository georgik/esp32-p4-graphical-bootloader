/**
 * @file partition_visualizer.c
 * @brief Partition table visualizer implementation
 */

#include "partition_visualizer.h"
#include "lvgl_bootloader.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>
#include "lvgl.h"

static const char* TAG = "partition_visualizer";

// Screen dimensions (matching firmware selector)
#define PV_SCREEN_WIDTH    1024
#define PV_SCREEN_HEIGHT   600

// UI Constants
#define PV_HEADER_HEIGHT   60
#define PV_LIST_HEIGHT     360
#define PV_BUTTON_HEIGHT   50
#define PV_MARGIN          10
#define PV_ROW_HEIGHT      80

// Color definitions
#define PV_COLOR_EMPTY     lv_color_hex(0xCCCCCC)  // Gray for empty
#define PV_COLOR_APP       lv_color_hex(0x00CC00)  // Green for app
#define PV_COLOR_DATA      lv_color_hex(0x0088FF)  // Blue for data
#define PV_COLOR_ERROR     lv_color_hex(0xFF0000)  // Red for error
#define PV_COLOR_TEXT      lv_color_hex(0x333333)  // Dark gray text

// State
static lv_obj_t* pv_screen = NULL;
static lv_obj_t* pv_list = NULL;

// ============================================================================
// Core Functions (work without LVGL)
// ============================================================================

int partition_visualizer_read_first_bytes(const esp_partition_t* partition,
                                          uint8_t* buffer,
                                          size_t buffer_size) {
    if (!partition || !buffer || buffer_size < 16) {
        ESP_LOGE(TAG, "Invalid arguments for read_first_bytes");
        return -1;
    }

    // Read first 16 bytes
    esp_err_t ret = esp_partition_read(partition, 0, buffer, 16);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read partition %s: %s", partition->label, esp_err_to_name(ret));
        return -1;
    }

    return 16;
}

esp_err_t partition_visualizer_format_hex(const uint8_t* data,
                                          size_t size,
                                          char* output,
                                          size_t output_size) {
    if (!data || !output || output_size < (size * 3 + 1)) {
        return ESP_ERR_INVALID_ARG;
    }

    char* ptr = output;
    for (size_t i = 0; i < size && (ptr - output) < (output_size - 3); i++) {
        snprintf(ptr, 4, "%02X ", data[i]);
        ptr += 3;
    }

    // Remove trailing space if present
    if (ptr > output) {
        *(ptr - 1) = '\0';
    }

    return ESP_OK;
}

int partition_visualizer_detect_content_type(const uint8_t* data, size_t size) {
    if (!data || size < 4) {
        return -1;
    }

    // Check if all 0xFF (empty/erased)
    bool all_ff = true;
    for (size_t i = 0; i < size; i++) {
        if (data[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }

    if (all_ff) {
        return 0;  // Empty
    }

    // Check for ESP32 application magic number (0xE9)
    if (data[0] == 0xE9) {
        return 1;  // Application
    }

    // Other data
    return 2;
}

// ============================================================================
// LVGL UI Functions
// ============================================================================

// Forward declarations
static lv_obj_t* create_partition_row(lv_obj_t* parent, const esp_partition_t* partition,
                                       uint32_t index);
static void refresh_button_cb(lv_event_t* e);
static void back_button_cb(lv_event_t* e);

// Async screen load functions (to avoid deadlock - see DEVELOPER_GUIDELINE.md Section 3)
static void async_show_visualizer(void* user_data);
static void async_refresh_visualizer(void* user_data);
static void async_back_to_selector(void* user_data);

// Forward declarations for functions defined later
lv_obj_t* partition_visualizer_create_screen(void);
esp_err_t partition_visualizer_refresh(lv_obj_t* screen);

static const char* get_partition_type_name(esp_partition_type_t type) {
    switch (type) {
        case ESP_PARTITION_TYPE_APP:
            return "APP";
        case ESP_PARTITION_TYPE_DATA:
            return "DATA";
        default:
            return "USER";
    }
}

static const char* get_partition_subtype_name(esp_partition_subtype_t subtype) {
    switch (subtype) {
        case ESP_PARTITION_SUBTYPE_APP_FACTORY:
            return "FACTORY";
        case ESP_PARTITION_SUBTYPE_APP_OTA_0:
            return "OTA_0";
        case ESP_PARTITION_SUBTYPE_APP_OTA_1:
            return "OTA_1";
        case ESP_PARTITION_SUBTYPE_APP_OTA_2:
            return "OTA_2";
        case ESP_PARTITION_SUBTYPE_DATA_NVS:
            return "NVS";
        case ESP_PARTITION_SUBTYPE_DATA_PHY:
            return "PHY";
        default:
            return "UNKNOWN";
    }
}

static const char* format_size(uint32_t size, char* buffer, size_t buffer_size) {
    if (size >= 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", size / (1024.0 * 1024.0));
    } else if (size >= 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", size / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%u B", (unsigned int)size);
    }
    return buffer;
}

static lv_obj_t* create_partition_row(lv_obj_t* parent, const esp_partition_t* partition,
                                       uint32_t index) {
    (void)index;  // Unused for now

    // Read first bytes and detect content BEFORE creating any UI objects
    uint8_t first_bytes[16];
    int bytes_read = partition_visualizer_read_first_bytes(partition, first_bytes, 16);

    int content_type = -1;
    lv_color_t indicator_color = PV_COLOR_ERROR;
    const char* content_label = "Error";

    if (bytes_read > 0) {
        content_type = partition_visualizer_detect_content_type(first_bytes, 16);
        switch (content_type) {
            case 0:  // Empty
                indicator_color = PV_COLOR_EMPTY;
                content_label = "Empty";
                break;
            case 1:  // App
                indicator_color = PV_COLOR_APP;
                content_label = "App";
                break;
            case 2:  // Data
                indicator_color = PV_COLOR_DATA;
                content_label = "Data";
                break;
        }
    }

    // Prepare all text strings before creating UI
    char name_text[64];
    snprintf(name_text, sizeof(name_text), "%s %s/%s",
             partition->label,
             get_partition_type_name(partition->type),
             get_partition_subtype_name(partition->subtype));

    char offset_str[32], size_str[32];
    snprintf(offset_str, sizeof(offset_str), "Offset: 0x%08X", (unsigned int)partition->address);
    format_size(partition->size, size_str, sizeof(size_str));

    char size_text[64];
    snprintf(size_text, sizeof(size_text), " | Size: %s", size_str);

    char hex_text[80];
    if (bytes_read > 0) {
        char hex_str[64];
        partition_visualizer_format_hex(first_bytes, 16, hex_str, sizeof(hex_str));
        snprintf(hex_text, sizeof(hex_text), "First: %s", hex_str);
    } else {
        snprintf(hex_text, sizeof(hex_text), "First: [Error]");
    }

    // Container for the row - simplified structure
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, PV_SCREEN_WIDTH - 2 * PV_MARGIN, PV_ROW_HEIGHT);
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_pad_row(row, 2, 0);  // Small gap between labels

    // Use flex layout for vertical stacking
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Row 1: Name label (full width)
    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, name_text);
    lv_obj_set_style_text_color(name_label, PV_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(name_label, PV_SCREEN_WIDTH - 4 * PV_MARGIN);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_LEFT, 0);

    // Row 2: Info labels in a horizontal container
    lv_obj_t* info_row = lv_obj_create(row);
    lv_obj_set_size(info_row, PV_SCREEN_WIDTH - 4 * PV_MARGIN, 20);
    lv_obj_set_style_pad_all(info_row, 0, 0);
    lv_obj_set_style_border_width(info_row, 0, 0);
    lv_obj_set_style_bg_opa(info_row, LV_OPA_TRANSP, 0);

    // Use flex layout for horizontal arrangement
    lv_obj_set_layout(info_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Offset label
    lv_obj_t* offset_label = lv_label_create(info_row);
    lv_label_set_text(offset_label, offset_str);
    lv_obj_set_style_text_color(offset_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(offset_label, &lv_font_montserrat_12, 0);

    // Size label
    lv_obj_t* size_label = lv_label_create(info_row);
    lv_label_set_text(size_label, size_text);
    lv_obj_set_style_text_color(size_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(size_label, &lv_font_montserrat_12, 0);

    // Status indicator (small colored box)
    lv_obj_t* indicator = lv_obj_create(info_row);
    lv_obj_set_size(indicator, 50, 18);
    lv_obj_set_style_bg_color(indicator, indicator_color, 0);
    lv_obj_set_style_radius(indicator, 3, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);

    lv_obj_t* status_label = lv_label_create(indicator);
    lv_label_set_text(status_label, content_label);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_center(status_label);

    // Row 3: Hex dump
    lv_obj_t* hex_label = lv_label_create(row);
    lv_label_set_text(hex_label, hex_text);
    lv_obj_set_style_text_color(hex_label, lv_color_hex(0x0088CC), 0);
    lv_obj_set_style_text_font(hex_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(hex_label, PV_SCREEN_WIDTH - 4 * PV_MARGIN);
    lv_obj_set_style_text_align(hex_label, LV_TEXT_ALIGN_LEFT, 0);

    return row;
}

static void refresh_button_cb(lv_event_t* e) {
    (void)e;
    ESP_LOGI(TAG, "Refresh button clicked");
    // Use async call to avoid deadlock (see DEVELOPER_GUIDELINE.md Section 3)
    lv_async_call(async_refresh_visualizer, NULL);
}

static void back_button_cb(lv_event_t* e) {
    (void)e;
    ESP_LOGI(TAG, "Back button clicked");
    // Use async call to avoid deadlock (see DEVELOPER_GUIDELINE.md Section 3)
    lv_async_call(async_back_to_selector, NULL);
}

// Async implementations
static void async_show_visualizer(void* user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Async: showing partition visualizer");

    if (!pv_screen) {
        pv_screen = partition_visualizer_create_screen();
    }

    if (pv_screen) {
        lv_scr_load(pv_screen);
    }
}

static void async_refresh_visualizer(void* user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Async: refreshing partition visualizer");
    partition_visualizer_refresh(NULL);
}

static void async_back_to_selector(void* user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "Async: returning to firmware selector");

    // Return to firmware selector screen
    show_firmware_selector_screen();
}

lv_obj_t* partition_visualizer_create_screen(void) {
    ESP_LOGI(TAG, "Creating partition visualizer screen");

    // Create screen
    lv_obj_t* screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, PV_SCREEN_WIDTH, PV_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF5F5F5), 0);

    // Header
    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_set_size(header, PV_SCREEN_WIDTH, PV_HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 15, 0);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "Partition Inspector");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    // Partition list (scrollable) - match firmware selector width style
    pv_list = lv_obj_create(screen);
    lv_obj_set_size(pv_list, PV_SCREEN_WIDTH - 40, PV_LIST_HEIGHT);
    lv_obj_align(pv_list, LV_ALIGN_TOP_MID, 0, PV_HEADER_HEIGHT + PV_MARGIN);
    lv_obj_set_layout(pv_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pv_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pv_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(pv_list, 5, 0);
    lv_obj_set_style_bg_color(pv_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(pv_list, 0, 0);
    lv_obj_set_style_radius(pv_list, 10, 0);

    // Enable scrolling
    lv_obj_set_scrollbar_mode(pv_list, LV_SCROLLBAR_MODE_AUTO);

    // Add all partitions
    const esp_partition_t* partition = NULL;
    uint32_t count = 0;

    // Iterate through all partitions
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                     ESP_PARTITION_SUBTYPE_ANY,
                                                     NULL);

    if (it) {
        while ((partition = esp_partition_get(it)) != NULL && count < 20) {
            lv_obj_t* row = create_partition_row(pv_list, partition, count);
            (void)row;  // Row is already added to parent in create_partition_row
            ESP_LOGI(TAG, "Added partition %u: %s @ 0x%08X",
                     count, partition->label, (unsigned int)partition->address);
            count++;
        }
        esp_partition_iterator_release(it);
    }

    ESP_LOGI(TAG, "Total partitions displayed: %u", count);

    // Button bar - match firmware selector width style
    lv_obj_t* button_bar = lv_obj_create(screen);
    lv_obj_set_size(button_bar, PV_SCREEN_WIDTH - 40, PV_BUTTON_HEIGHT);
    lv_obj_align(button_bar, LV_ALIGN_BOTTOM_MID, 0, -PV_MARGIN);
    lv_obj_set_style_bg_opa(button_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_bar, 0, 0);
    lv_obj_set_style_pad_all(button_bar, 0, 0);
    lv_obj_set_layout(button_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(button_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Refresh button
    lv_obj_t* refresh_btn = lv_btn_create(button_bar);
    lv_obj_set_size(refresh_btn, 140, 40);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_border_width(refresh_btn, 0, 0);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_set_style_text_color(refresh_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(refresh_label);

    // Back button
    lv_obj_t* back_btn = lv_btn_create(button_bar);
    lv_obj_set_size(back_btn, 140, 40);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x757575), 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, back_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_label);

    ESP_LOGI(TAG, "Partition visualizer screen created successfully");
    return screen;
}

void partition_visualizer_show(void) {
    ESP_LOGI(TAG, "Showing partition visualizer screen");
    // Use async call to avoid deadlock (see DEVELOPER_GUIDELINE.md Section 3)
    lv_async_call(async_show_visualizer, NULL);
}

esp_err_t partition_visualizer_refresh(lv_obj_t* screen) {
    ESP_LOGI(TAG, "Refreshing partition visualizer");

    if (!screen) {
        screen = pv_screen;
    }

    if (!screen) {
        ESP_LOGE(TAG, "No screen to refresh");
        return ESP_ERR_INVALID_STATE;
    }

    // Clean up old screen and create new one
    if (pv_screen == screen) {
        lv_obj_del(pv_screen);
        pv_screen = NULL;
        pv_list = NULL;
    }

    pv_screen = partition_visualizer_create_screen();
    if (!pv_screen) {
        ESP_LOGE(TAG, "Failed to recreate visualizer screen");
        return ESP_FAIL;
    }

    lv_scr_load(pv_screen);
    ESP_LOGI(TAG, "Partition visualizer refreshed");

    return ESP_OK;
}

void partition_visualizer_cleanup(void) {
    ESP_LOGI(TAG, "Cleaning up partition visualizer");

    if (pv_screen) {
        lv_obj_del(pv_screen);
        pv_screen = NULL;
        pv_list = NULL;
    }
}
