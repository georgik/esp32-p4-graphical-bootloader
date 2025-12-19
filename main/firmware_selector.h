/**
 * @file firmware_selector.h
 * @brief Multi-firmware selection and management for ESP32-P4 graphical bootloader
 *
 * Provides LVGL-based interface for selecting, validating, and managing multiple
 * firmware files from SD card for dynamic partition flashing.
 */

#ifndef FIRMWARE_SELECTOR_H
#define FIRMWARE_SELECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration constants
#define MAX_FIRMWARE_COUNT          16
#define MAX_FILENAME_LENGTH         256
#define MAX_DISPLAY_NAME_LENGTH     128
#define FIRMWARE_DIRECTORY          "/sdcard/firmwares"
#define FIRMWARE_EXTENSION          ".bin"

// Firmware selection screen dimensions - optimized for 1024x600 display
#define FW_SELECTOR_SCREEN_WIDTH    1024
#define FW_SELECTOR_SCREEN_HEIGHT   600
#define FW_LIST_HEIGHT              450    // Leave space for title, buttons, and info
#define FW_BUTTON_HEIGHT            50     // Larger buttons for touch interface
#define FW_INFO_HEIGHT              60     // More space for file size info

/**
 * @brief Firmware information structure
 */
typedef struct {
    char filename[MAX_FILENAME_LENGTH];        // Full filename with extension
    char display_name[MAX_DISPLAY_NAME_LENGTH]; // Display name without extension
    char file_path[MAX_FILENAME_LENGTH];       // Full path to file
    uint32_t size;                              // File size in bytes
    uint32_t crc32;                             // CRC32 checksum
    bool is_valid;                              // Binary validation status
    bool is_selected;                           // User selection state
    void* assigned_partition;                 // Assigned partition info (NULL if not assigned)
    lv_obj_t* list_item;                        // LVGL list item reference
} firmware_info_t;

/**
 * @brief Firmware selection screen data
 */
typedef struct {
    lv_obj_t* screen;                           // Main screen object
    lv_obj_t* list;                             // Firmware list
    lv_obj_t* total_size_label;                 // Total size display
    lv_obj_t* status_label;                     // Status message label
    lv_obj_t* select_all_btn;                   // Select all button
    lv_obj_t* clear_btn;                        // Clear selection button
    lv_obj_t* flash_btn;                        // Start flashing button
    lv_obj_t* back_btn;                         // Back to main menu button

    firmware_info_t firmware_list[MAX_FIRMWARE_COUNT];  // Firmware information array
    uint32_t firmware_count;                   // Number of firmware files found
    uint32_t selected_count;                   // Number of selected firmwares
    uint32_t total_selected_size;              // Total size of selected firmwares

    bool is_initialized;                        // Initialization status
} firmware_selector_t;

/**
 * @brief Initialize firmware selector
 *
 * @param selector Pointer to firmware_selector_t structure to initialize
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_init(firmware_selector_t* selector);

/**
 * @brief Scan firmware directory and populate firmware list
 *
 * @param selector Initialized firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_scan_directory(firmware_selector_t* selector);

/**
 * @brief Create LVGL UI for firmware selection
 *
 * @param selector Initialized and scanned firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_create_ui(firmware_selector_t* selector);

/**
 * @brief Show firmware selection screen
 *
 * @param selector Fully initialized firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_show(firmware_selector_t* selector);

/**
 * @brief Hide firmware selection screen
 *
 * @param selector Firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_hide(firmware_selector_t* selector);

/**
 * @brief Toggle selection status of a firmware
 *
 * @param selector Firmware selector
 * @param index Index of firmware in the list
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_toggle_selection(firmware_selector_t* selector, uint32_t index);

/**
 * @brief Select all firmware files
 *
 * @param selector Firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_select_all(firmware_selector_t* selector);

/**
 * @brief Clear all firmware selections
 *
 * @param selector Firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_clear_selection(firmware_selector_t* selector);

/**
 * @brief Check if selected firmwares fit in available flash space
 *
 * @param selector Firmware selector
 * @param fits Pointer to bool to store result
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_check_space(firmware_selector_t* selector, bool* fits);

/**
 * @brief Get list of selected firmwares for flashing
 *
 * @param selector Firmware selector
 * @param selected_list Array to store selected firmware pointers
 * @param max_count Maximum number of entries to store
 * @param count Pointer to store actual count
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_get_selected(firmware_selector_t* selector,
                                       firmware_info_t** selected_list,
                                       uint32_t max_count,
                                       uint32_t* count);

/**
 * @brief Cleanup firmware selector resources
 *
 * @param selector Firmware selector to cleanup
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_cleanup(firmware_selector_t* selector);

/**
 * @brief Get firmware information by index
 *
 * @param selector Firmware selector
 * @param index Index of firmware
 * @return Pointer to firmware_info_t or NULL if not found
 */
firmware_info_t* firmware_selector_get_firmware(firmware_selector_t* selector, uint32_t index);

/**
 * @brief Update total size display
 *
 * @param selector Firmware selector
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_update_size_display(firmware_selector_t* selector);

/**
 * @brief Store firmware configuration in NVS for boot menu
 *
 * Stores information about flashed firmware (filename, OTA partition) in NVS
 * so that the boot menu can display available applications and allow booting them.
 *
 * @param selector Firmware selector with selected firmware
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t firmware_selector_store_firmware_config(firmware_selector_t* selector);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_SELECTOR_H