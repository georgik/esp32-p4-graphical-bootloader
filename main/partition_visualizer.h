/**
 * @file partition_visualizer.h
 * @brief Partition table visualizer screen for inspecting flash partitions
 *
 * This module provides an interactive UI to display partition information
 * including hex dumps, working on both ESP-IDF hardware and simulator.
 */

#ifndef PARTITION_VISUALIZER_H
#define PARTITION_VISUALIZER_H

#include "esp_partition.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LVGL_H
#include "lvgl.h"

/**
 * @brief Create partition visualizer screen
 *
 * Creates a new LVGL screen displaying all partitions with their
 * information and hex dumps.
 *
 * @return LVGL screen object, or NULL on failure
 */
lv_obj_t* partition_visualizer_create_screen(void);

/**
 * @brief Show partition visualizer screen
 *
 * Loads and displays the partition visualizer screen.
 * Creates the screen if it doesn't exist.
 */
void partition_visualizer_show(void);

/**
 * @brief Refresh partition data
 *
 * Re-reads partition information from flash and updates the display.
 *
 * @param screen Visualizer screen to refresh (NULL to use active screen)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_visualizer_refresh(lv_obj_t* screen);

/**
 * @brief Cleanup partition visualizer
 *
 * Frees resources associated with the partition visualizer.
 */
void partition_visualizer_cleanup(void);

#endif // LVGL_H

// Core functions (work without LVGL for data extraction)

/**
 * @brief Read first bytes from a partition
 *
 * Reads the first 16 bytes from a partition for hex dump display.
 *
 * @param partition Partition to read from
 * @param buffer Output buffer (must be at least 16 bytes)
 * @param buffer_size Size of buffer
 * @return Number of bytes read, or -1 on error
 */
int partition_visualizer_read_first_bytes(const esp_partition_t* partition,
                                          uint8_t* buffer,
                                          size_t buffer_size);

/**
 * @brief Format bytes as hex string
 *
 * Converts binary data to hex string format (e.g., "AA BB CC DD")
 *
 * @param data Binary data
 * @param size Number of bytes
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t partition_visualizer_format_hex(const uint8_t* data,
                                          size_t size,
                                          char* output,
                                          size_t output_size);

/**
 * @brief Detect partition content type
 *
 * Analyzes the first bytes to determine if partition is empty,
 * contains application data, or has errors.
 *
 * @param data First bytes of partition
 * @param size Number of bytes
 * @return Content type:
 *         0 = Empty (all 0xFF)
 *         1 = Application data (has magic number)
 *         2 = Other data
 *         -1 = Error
 */
int partition_visualizer_detect_content_type(const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // PARTITION_VISUALIZER_H
