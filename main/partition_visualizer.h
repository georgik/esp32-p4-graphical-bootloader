/**
 * @file partition_visualizer.h
 * @brief Partition table visualization screen for ESP32-P4 bootloader
 *
 * This screen displays:
 * - Visual flash map showing all partitions
 * - Partition details panel
 * - Flash operation progress visualization
 * - Real-time flash write/erase monitoring
 *
 * Works on both hardware and simulator!
 */

#ifndef PARTITION_VISUALIZER_H
#define PARTITION_VISUALIZER_H

#include "lvgl.h"
#include "partition_manager.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Visualizer modes
typedef enum {
    VIS_MODE_OVERVIEW,      // Show all partitions
    VIS_MODE_DETAILED,      // Show selected partition details
    VIS_MODE_FLASH_OP,      // Show flash operation progress
} partition_vis_mode_t;

// Flash operation visualization
typedef enum {
    FLASH_OP_IDLE,
    FLASH_OP_WRITING,
    FLASH_OP_ERASING,
    FLASH_OP_VALIDATING,
    FLASH_OP_COMPLETE,
    FLASH_OP_ERROR
} flash_op_state_t;

/**
 * @brief Initialize partition visualizer screen
 *
 * @return ESP_OK on success
 */
esp_err_t partition_visualizer_init(void);

/**
 * @brief Show partition visualizer screen with current layout
 *
 * @param layout Partition table layout to visualize
 * @return ESP_OK on success
 */
esp_err_t partition_visualizer_show(const partition_table_layout_t* layout);

/**
 * @brief Update visualizer with new partition layout
 *
 * @param layout New partition layout
 * @return ESP_OK on success
 */
esp_err_t partition_visualizer_update_layout(const partition_table_layout_t* layout);

/**
 * @brief Start flash operation visualization
 *
 * @param partition_name Name of partition being operated on
 * @param op_type Operation type (write/erase)
 * @param total_size Total size of operation
 */
void partition_visualizer_flash_op_start(
    const char* partition_name,
    flash_op_state_t op_type,
    uint32_t total_size
);

/**
 * @brief Update flash operation progress
 *
 * @param offset Current offset in partition
 * @param chunk_size Size of current chunk
 */
void partition_visualizer_flash_op_progress(uint32_t offset, uint32_t chunk_size);

/**
 * @brief Complete flash operation visualization
 *
 * @param success Whether operation succeeded
 */
void partition_visualizer_flash_op_complete(bool success);

/**
 * @brief Get current visualizer screen object
 *
 * @return LVGL screen object
 */
lv_obj_t* partition_visualizer_get_screen(void);

#ifdef __cplusplus
}
#endif

#endif // PARTITION_VISUALIZER_H
