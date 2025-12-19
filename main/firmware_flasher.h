/**
 * @file firmware_flasher.h
 * @brief Firmware flashing engine with progress tracking
 *
 * Provides high-performance firmware flashing with progress callbacks,
 * CRC verification, and error recovery for the multi-firmware bootloader.
 */

#ifndef FIRMWARE_FLASHER_H
#define FIRMWARE_FLASHER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "partition_manager.h"
#include "firmware_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

// Flashing operation states
typedef enum {
    FLASH_STATE_IDLE = 0,
    FLASH_STATE_INITIALIZING,
    FLASH_STATE_BACKING_UP,
    FLASH_STATE_WRITING_PARTITION_TABLE,
    FLASH_STATE_FLASHING_FIRMWARE,
    FLASH_STATE_VERIFYING,
    FLASH_STATE_CLEANING_UP,
    FLASH_STATE_COMPLETED,
    FLASH_STATE_ERROR
} flash_state_t;

// Flash operation result
typedef enum {
    FLASH_RESULT_SUCCESS = 0,
    FLASH_RESULT_ERROR_INVALID_FIRMWARE,
    FLASH_RESULT_ERROR_PARTITION_TABLE,
    FLASH_RESULT_ERROR_FLASH_WRITE,
    FLASH_RESULT_ERROR_CRC_MISMATCH,
    FLASH_RESULT_ERROR_SPACE_INSUFFICIENT,
    FLASH_RESULT_ERROR_READ_FAILED,
    FLASH_RESULT_ERROR_WRITE_FAILED,
    FLASH_RESULT_ERROR_ABORTED
} flash_result_t;

// Progress callback function type
typedef void (*flash_progress_callback_t)(uint32_t current_firmware,
                                           uint32_t total_firmwares,
                                           uint32_t current_progress,
                                           uint32_t total_progress,
                                           const char* status_message);

// Status callback function type
typedef void (*flash_status_callback_t)(flash_state_t state,
                                          flash_result_t result,
                                          const char* status_message);

/**
 * @brief Flash operation configuration
 */
typedef struct {
    firmware_selector_t* firmware_selector;
    partition_table_layout_t partition_layout;  // Store copy instead of pointer
    bool enable_backup;
    bool enable_verification;
    bool enable_optimized_chunking;
    uint32_t chunk_size;        // 0 = auto-detect
    flash_progress_callback_t progress_callback;
    flash_status_callback_t status_callback;
} flash_config_t;

/**
 * @brief Flash operation statistics
 */
typedef struct {
    uint32_t total_firmwares;
    uint32_t completed_firmwares;
    uint32_t current_firmware;
    uint32_t total_bytes;
    uint32_t written_bytes;
    uint32_t verification_errors;
    uint32_t write_errors;
    uint32_t crc_errors;
    uint32_t start_time_ms;
    uint32_t elapsed_time_ms;
    float bytes_per_second;
} flash_statistics_t;

/**
 * @brief Initialize firmware flasher
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_init(void);

/**
 * @brief Read current partition table and only update OTA entries
 *
 * This function reads the existing partition table and only modifies
 * OTA partition entries, preserving all system partitions.
 *
 * @param layout Generated layout with new OTA partitions
 * @param buffer Output buffer for modified partition table
 * @param buffer_size Size of output buffer
 * @param actual_size Actual size of written data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_create_ota_table(const firmware_selector_t* selector,
                                              uint8_t* buffer,
                                              size_t buffer_size,
                                              size_t* actual_size);

/**
 * @brief Start firmware flashing operation
 *
 * This function performs the complete flashing workflow:
 * 1. Validates selected firmwares
 * 2. Creates optimal partition layout
 * 3. Backs up current partition table (if enabled)
 * 4. Writes new partition table
 * 5. Flashes all firmware files
 * 6. Verifies all written data
 *
 * @param config Flash configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_start(const flash_config_t* config);

/**
 * @brief Abort current flashing operation
 *
 * Safely aborts the ongoing flash operation and cleans up resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_abort(void);

/**
 * @brief Get current flash state
 *
 * @return Current flash operation state
 */
flash_state_t firmware_flasher_get_state(void);

/**
 * @brief Get flash operation result
 *
 * @return Flash operation result
 */
flash_result_t firmware_flasher_get_result(void);

/**
 * @brief Get flash operation statistics
 *
 * @param stats Pointer to store statistics
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_get_statistics(flash_statistics_t* stats);

/**
 * @brief Check if flashing operation is in progress
 *
 * @return true if flashing is in progress, false otherwise
 */
bool firmware_flasher_is_busy(void);

/**
 * @brief Check if flashing operation can be started
 *
 * @param can_start Pointer to store result
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_can_start(bool* can_start);

/**
 * @brief Flash single firmware to partition
 *
 * @param firmware Firmware information
 * @param partition Target partition information
 * @param progress_callback Progress callback (optional)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_flash_single(const firmware_info_t* firmware,
                                           const partition_info_t* partition,
                                           flash_progress_callback_t progress_callback);

/**
 * @brief Verify firmware in partition
 *
 * @param firmware Expected firmware information
 * @param partition Partition to verify
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_verify_single(const firmware_info_t* firmware,
                                            const partition_info_t* partition);

/**
 * @brief Calculate optimal chunk size for flashing
 *
 * @param file_size File size to optimize for
 * @param is_ota_partition Whether target is OTA partition
 * @return Optimal chunk size in bytes
 */
uint32_t firmware_flasher_calculate_chunk_size(uint32_t file_size, bool is_ota_partition);

/**
 * @brief Get human readable flash result message
 *
 * @param result Flash result code
 * @param message Buffer to store message
 * @param buffer_size Size of message buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_get_result_message(flash_result_t result,
                                                char* message,
                                                size_t buffer_size);

/**
 * @brief Cleanup firmware flasher resources
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_cleanup(void);

/**
 * @brief Advanced: Flash firmware with custom parameters
 *
 * @param firmware Firmware to flash
 * @param offset Flash offset
 * @param size Size to flash
 * @param buffer Source buffer
 * @param progress_callback Progress callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_flash_raw(const uint8_t* buffer,
                                       uint32_t size,
                                       uint32_t offset,
                                       flash_progress_callback_t progress_callback);

/**
 * @brief Create OTA partition table from generated layout
 *
 * @param selector Firmware selector with selected firmwares
 * @param buffer Buffer to store partition table data
 * @param buffer_size Size of buffer
 * @param actual_size Pointer to store actual size of partition table
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t firmware_flasher_create_ota_table_from_layout(const firmware_selector_t* selector,
                                                          uint8_t* buffer,
                                                          size_t buffer_size,
                                                          size_t* actual_size);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_FLASHER_H