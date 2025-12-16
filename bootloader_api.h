#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Partition types for next boot request
 */
typedef enum {
    BOOT_PARTITION_FACTORY = 0,   ///< Boot from factory partition
    BOOT_PARTITION_OTA_0 = 1,     ///< Boot from OTA slot 0
    BOOT_PARTITION_OTA_1 = 2      ///< Boot from OTA slot 1
} boot_partition_type_t;

/**
 * @brief Request the next boot partition
 *
 * Applications can call this function to specify which partition should be booted
 * on the next restart. The bootloader will read this request and clear it.
 *
 * @param partition_type The partition type to boot from next
 * @return ESP_OK on success
 */
esp_err_t bootloader_request_next_boot(boot_partition_type_t partition_type);

/**
 * @brief Check if there's a pending boot request
 *
 * @param has_request Pointer to store result (true if request exists)
 * @return ESP_OK on success
 */
esp_err_t bootloader_has_pending_request(bool *has_request);

/**
 * @brief Clear any pending boot request
 *
 * @return ESP_OK on success
 */
esp_err_t bootloader_clear_pending_request(void);

/**
 * @brief Get the current boot partition type
 *
 * @param current_partition Pointer to store current partition type
 * @return ESP_OK on success
 */
esp_err_t bootloader_get_current_partition(boot_partition_type_t *current_partition);

#ifdef __cplusplus
}
#endif