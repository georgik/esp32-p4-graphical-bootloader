#ifndef SD_OTA_H
#define SD_OTA_H

#include "esp_err.h"
#include "esp_partition.h"

#define SD_OTA_MOUNT_POINT "/sdcard"
#define SD_OTA_FILENAME "ota1.bin"
#define SD_OTA_MAX_FILE_SIZE (8 * 1024 * 1024)  // 8MB maximum

typedef struct {
    const char* filename;
    size_t file_size;
    const esp_partition_t* target_partition;
    size_t bytes_written;
    bool in_progress;
} sd_ota_state_t;

/**
 * @brief Initialize SD card and mount filesystem
 * @return ESP_OK on success
 */
esp_err_t sd_ota_init(void);

/**
 * @brief Check if OTA file exists on SD card
 * @param filename Name of the file to check (e.g., "ota1.bin")
 * @return ESP_OK if file exists and is readable
 */
esp_err_t sd_ota_check_file(const char* filename);

/**
 * @brief Get file size of OTA file on SD card
 * @param filename Name of the file
 * @param file_size Output parameter for file size
 * @return ESP_OK on success
 */
esp_err_t sd_ota_get_file_size(const char* filename, size_t* file_size);

/**
 * @brief Flash OTA file from SD card to target partition
 * @param filename Name of the file to flash (e.g., "ota1.bin")
 * @param partition_subtype Target OTA partition subtype (ESP_PARTITION_SUBTYPE_APP_OTA_1, etc.)
 * @return ESP_OK on success
 */
esp_err_t sd_ota_flash_file(const char* filename, esp_partition_subtype_t partition_subtype);

/**
 * @brief Get OTA state for progress tracking
 * @return Current OTA state
 */
sd_ota_state_t sd_ota_get_state(void);

/**
 * @brief Cleanup SD card resources
 */
void sd_ota_cleanup(void);

#endif // SD_OTA_H