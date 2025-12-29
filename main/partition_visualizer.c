/**
 * @file partition_visualizer.c
 * @brief Partition table visualization screen implementation
 *
 * This file implements the partition visualizer screen that shows:
 * 1. Visual flash map with colored partition blocks
 * 2. Partition details panel
 * 3. Flash operation progress in real-time
 */

#include "partition_visualizer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "partition_visualizer";

// Screen objects
static lv_obj_t* visualizer_screen = NULL;
static lv_obj_t* flash_map_container = NULL;
static lv_obj_t* details_panel = NULL;
static lv_obj_t* flash_progress_bar = NULL;
static lv_obj_t* flash_status_label = NULL;
static lv_obj_t* partition_list = NULL;

// Color scheme for partition types
#define COLOR_BOOTLOADER  lv_color_hex(0x2196F3)  // Blue
#define COLOR_PARTITION_TBL lv_color_hex(0x9C27B0)  // Purple
#define COLOR_NVS          lv_color_hex(0x4CAF50)  // Green
#define COLOR_OTA_DATA     lv_color_hex(0xFF9800)  // Orange
#define COLOR_FACTORY      lv_color_hex(0xF44336)  // Red
#define COLOR_OTA          lv_color_hex(0x00BCD4)  // Cyan
#define COLOR_CUSTOM       lv_color_hex(0x607D8B)  // Gray
#define COLOR_FREE_SPACE   lv_color_hex(0xECEFF1)  // Light gray

// Get color for partition type
static lv_color_t get_partition_color(const partition_info_t* partition) {
    switch (partition->type) {
        case PARTITION_TYPE_BOOTLOADER:
            return COLOR_BOOTLOADER;
        case PARTITION_TYPE_PARTITION_TABLE:
            return COLOR_PARTITION_TBL;
        case PARTITION_TYPE_NVS:
        case PARTITION_TYPE_PHY_INIT:
            return COLOR_NVS;
        case PARTITION_TYPE_OTA_DATA:
            return COLOR_OTA_DATA;
        case PARTITION_TYPE_FACTORY_APP:
            return COLOR_FACTORY;
        case PARTITION_TYPE_OTA_0:
        case PARTITION_TYPE_OTA_1:
        case PARTITION_TYPE_OTA_2:
        case PARTITION_TYPE_OTA_3:
            return COLOR_OTA;
        default:
            return COLOR_CUSTOM;
    }
}

// Draw visual flash map
static void draw_flash_map(const partition_table_layout_t* layout) {
    if (!flash_map_container) return;

    // Clean existing content
    lv_obj_clean(flash_map_container);

    lv_obj_t* title = lv_label_create(flash_map_container);
    lv_label_set_text(title, "Flash Memory Map (16MB)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00BCD4), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Calculate dimensions
    uint32_t canvas_width = 900;
    uint32_t canvas_height = 200;
    uint32_t total_flash = 16 * 1024 * 1024;  // 16MB
    uint32_t row_height = 25;
    uint32_t y_offset = 50;

    // Draw partitions
    for (uint32_t i = 0; i < layout->partition_count && i < 10; i++) {
        const partition_info_t* part = &layout->partitions[i];

        // Calculate position and width
        uint32_t x_start = (part->offset * canvas_width) / total_flash;
        uint32_t width = (part->size * canvas_width) / total_flash;
        uint32_t y_pos = y_offset + (i % 7) * (row_height + 3);

        // Ensure minimum width for visibility
        if (width < 2) width = 2;

        // Create partition rectangle
        lv_obj_t* part_rect = lv_obj_create(flash_map_container);
        lv_obj_set_pos(part_rect, x_start + 10, y_pos);
        lv_obj_set_size(part_rect, width - 1, row_height);
        lv_obj_set_style_bg_color(part_rect, get_partition_color(part), 0);
        lv_obj_set_style_border_width(part_rect, 1, 0);
        lv_obj_set_style_border_color(part_rect, lv_color_white(), 0);
        lv_obj_set_style_radius(part_rect, 2, 0);

        // Add label if wide enough
        if (width > 60) {
            lv_obj_t* label = lv_label_create(part_rect);
            lv_label_set_text(label, part->name);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
            lv_obj_center(label);
        }

        // Click event to show details
        lv_obj_add_flag(part_rect, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(part_rect, NULL, LV_EVENT_CLICKED, (void*)part);
    }

    // Draw scale markers
    for (uint32_t addr = 0; addr <= total_flash; addr += 4 * 1024 * 1024) {
        uint32_t x_pos = 10 + (addr * canvas_width) / total_flash;

        lv_obj_t* marker = lv_obj_create(flash_map_container);
        lv_obj_set_pos(marker, x_pos, y_offset + 180);
        lv_obj_set_size(marker, 1, 15);
        lv_obj_set_style_bg_color(marker, lv_color_white(), 0);

        lv_obj_t* label = lv_label_create(flash_map_container);
        char addr_str[32];
        snprintf(addr_str, sizeof(addr_str), "%luMB", addr / (1024 * 1024));
        lv_label_set_text(label, addr_str);
        lv_obj_set_pos(label, x_pos - 15, y_offset + 195);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
    }
}

// Update partition details panel
static void update_details_panel(const partition_info_t* partition) {
    if (!details_panel || !partition) return;

    lv_obj_clean(details_panel);

    lv_obj_t* title = lv_label_create(details_panel);
    char title_str[64];
    snprintf(title_str, sizeof(title_str), "Partition: %s", partition->name);
    lv_label_set_text(title, title_str);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00BCD4), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

    char details[512];
    snprintf(details, sizeof(details),
        "Type:      %u\n"
        "Subtype:   %u\n"
        "Offset:    0x%08x (%lu bytes)\n"
        "Size:      0x%08x (%.2f MB)\n"
        "Aligned:   %s\n"
        "Read-only: %s\n"
        "Encrypted: %s",
        partition->type,
        partition->subtype,
        (unsigned int)partition->offset,
        (unsigned long)partition->offset,
        (unsigned int)partition->size,
        partition->size / (1024.0 * 1024.0),
        (partition->offset % 4096 == 0) ? "Yes ✓" : "No ✗",
        partition->is_readonly ? "Yes" : "No",
        partition->is_encrypted ? "Yes" : "No"
    );

    lv_obj_t* details_label = lv_label_create(details_panel);
    lv_label_set_text(details_label, details);
    lv_obj_set_style_text_color(details_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(details_label, &lv_font_montserrat_12, 0);
    lv_obj_align(details_label, LV_ALIGN_TOP_LEFT, 10, 50);

    // Firmware info if applicable
    if (partition->firmware) {
        lv_obj_t* fw_title = lv_label_create(details_panel);
        lv_label_set_text(fw_title, "\nFirmware:");
        lv_obj_set_style_text_color(fw_title, lv_color_hex(0x00BCD4), 0);
        lv_obj_set_style_text_font(fw_title, &lv_font_montserrat_12, 0);
        lv_obj_align(fw_title, LV_ALIGN_TOP_LEFT, 10, 180);

        char fw_info[256];
        snprintf(fw_info, sizeof(fw_info),
            "File: %s\nSize: %.2f MB\nCRC32: 0x%08lX",
            partition->firmware->filename,
            partition->firmware->size / (1024.0 * 1024.0),
            (unsigned long)partition->firmware->crc32
        );

        lv_obj_t* fw_label = lv_label_create(details_panel);
        lv_label_set_text(fw_label, fw_info);
        lv_obj_set_style_text_color(fw_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(fw_label, &lv_font_montserrat_10, 0);
        lv_obj_align(fw_label, LV_ALIGN_TOP_LEFT, 10, 210);
    }
}

// Initialize visualizer screen
esp_err_t partition_visualizer_init(void) {
    ESP_LOGI(TAG, "Initializing partition visualizer...");

    visualizer_screen = lv_obj_create(NULL);
    lv_obj_set_size(visualizer_screen, 1024, 600);
    lv_obj_set_style_bg_color(visualizer_screen, lv_color_hex(0x263238), 0);

    // Title
    lv_obj_t* title = lv_label_create(visualizer_screen);
    lv_label_set_text(title, "ESP32-P4 Partition Inspector");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00BCD4), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Flash map container
    flash_map_container = lv_obj_create(visualizer_screen);
    lv_obj_set_size(flash_map_container, 920, 250);
    lv_obj_set_style_bg_color(flash_map_container, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_pad_all(flash_map_container, 5, 0);
    lv_obj_align(flash_map_container, LV_ALIGN_TOP_MID, 0, 60);

    // Details panel
    details_panel = lv_obj_create(visualizer_screen);
    lv_obj_set_size(details_panel, 920, 180);
    lv_obj_set_style_bg_color(details_panel, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_pad_all(details_panel, 10, 0);
    lv_obj_align(details_panel, LV_ALIGN_TOP_MID, 0, 320);

    // Flash operation progress section
    lv_obj_t* progress_section = lv_obj_create(visualizer_screen);
    lv_obj_set_size(progress_section, 920, 80);
    lv_obj_set_style_bg_color(progress_section, lv_color_hex(0x37474F), 0);
    lv_obj_align(progress_section, LV_ALIGN_BOTTOM_MID, 0, -20);

    flash_progress_bar = lv_bar_create(progress_section);
    lv_obj_set_size(flash_progress_bar, 600, 20);
    lv_obj_align(flash_progress_bar, LV_ALIGN_TOP_MID, 0, 10);
    lv_bar_set_range(flash_progress_bar, 0, 100);
    lv_bar_set_value(flash_progress_bar, 0, LV_ANIM_OFF);

    flash_status_label = lv_label_create(progress_section);
    lv_label_set_text(flash_status_label, "No flash operation in progress");
    lv_obj_set_style_text_color(flash_status_label, lv_color_white(), 0);
    lv_obj_align(flash_status_label, LV_ALIGN_TOP_MID, 0, 40);

    // Back button
    lv_obj_t* back_btn = lv_btn_create(visualizer_screen);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_LEFT, 20, -20);

    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);

    ESP_LOGI(TAG, "✅ Partition visualizer initialized");
    return ESP_OK;
}

// Show visualizer with layout
esp_err_t partition_visualizer_show(const partition_table_layout_t* layout) {
    if (!visualizer_screen) {
        esp_err_t ret = partition_visualizer_init();
        if (ret != ESP_OK) return ret;
    }

    draw_flash_map(layout);

    // Calculate statistics
    uint32_t used_space = 0;
    uint32_t app_count = 0;
    for (uint32_t i = 0; i < layout->partition_count; i++) {
        used_space += layout->partitions[i].size;
        if (layout->partitions[i].type == PARTITION_TYPE_FACTORY_APP ||
            layout->partitions[i].type >= PARTITION_TYPE_OTA_0) {
            app_count++;
        }
    }

    char stats[256];
    snprintf(stats, sizeof(stats),
        "Partitions: %u | Apps: %u | Used: %.1f%%",
        layout->partition_count,
        app_count,
        (used_space / (16.0 * 1024 * 1024)) * 100.0
    );

    if (flash_status_label) {
        lv_label_set_text(flash_status_label, stats);
    }

    lv_screen_load(visualizer_screen);
    ESP_LOGI(TAG, "Partition visualizer shown");

    return ESP_OK;
}

// Flash operation callbacks
void partition_visualizer_flash_op_start(
    const char* partition_name,
    flash_op_state_t op_type,
    uint32_t total_size) {

    if (!flash_progress_bar || !flash_status_label) return;

    const char* op_str = (op_type == FLASH_OP_WRITING) ? "Writing" : "Erasing";
    char status[128];
    snprintf(status, sizeof(status), "%s %s (%.2f MB)...",
             op_str, partition_name, total_size / (1024.0 * 1024.0));
    lv_label_set_text(flash_status_label, status);
    lv_bar_set_value(flash_progress_bar, 0, LV_ANIM_OFF);
}

void partition_visualizer_flash_op_progress(uint32_t offset, uint32_t chunk_size) {
    if (!flash_progress_bar) return;

    // Just a placeholder - would need total size to calculate percentage
    // This will be called from flash_emulator callbacks
}

void partition_visualizer_flash_op_complete(bool success) {
    if (!flash_status_label || !flash_progress_bar) return;

    if (success) {
        lv_label_set_text(flash_status_label, "✅ Flash operation completed successfully");
        lv_bar_set_value(flash_progress_bar, 100, LV_ANIM_ON);
    } else {
        lv_label_set_text(flash_status_label, "❌ Flash operation failed");
    }
}

lv_obj_t* partition_visualizer_get_screen(void) {
    return visualizer_screen;
}

esp_err_t partition_visualizer_update_layout(const partition_table_layout_t* layout) {
    return partition_visualizer_show(layout);
}
