#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_partition.h"
#include "esp_err.h"
#include "bootloader_config.h"

#define BOOT_REQUEST_MAGIC  0x50415445  // "PETE"
#define BOOT_REQUEST_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Boot request structure stored in NVS
 *
 * This structure is used to store the next boot partition information
 * that applications can set before rebooting.
 */
typedef struct {
    uint32_t magic;              ///< Magic number for validation (BOOT_REQUEST_MAGIC)
    uint8_t version;             ///< Structure version
    uint8_t next_partition_type; ///< Next boot partition type (0=factory, 1=ota_0, 2=ota_1)
    uint8_t reserved;            ///< Reserved for future use
    uint8_t boot_count;          ///< Number of times this request has been processed
    uint32_t timestamp;          ///< Timestamp when request was created
} boot_request_t;

// Use ESP-IDF's bootloader_state_t - no need for custom declaration

/**
 * @brief Initialize the bootloader custom functionality
 *
 * @return ESP_OK on success
 */
esp_err_t bootloader_custom_init(void);

/**
 * @brief Read boot request from NVS
 *
 * @param request Pointer to store the boot request
 * @return ESP_OK if request found and valid, ESP_ERR_NOT_FOUND if no request
 */
esp_err_t bootloader_read_boot_request(boot_request_t *request);

/**
 * @brief Clear boot request from NVS
 *
 * @return ESP_OK on success
 */
esp_err_t bootloader_clear_boot_request(void);

/**
 * @brief Map available partitions dynamically from partition table
 *
 * @param state Bootloader state information (ESP-IDF's bootloader_state_t)
 * @return ESP_OK on success
 */
esp_err_t bootloader_map_partitions(const bootloader_state_t *state);

/**
 * @brief Get the partition to boot based on request and default behavior
 *
 * @param request Pointer to boot request (can be NULL for no request)
 * @param state Bootloader state information (ESP-IDF's bootloader_state_t)
 * @return Pointer to partition to boot from
 */
const esp_partition_t* bootloader_get_boot_partition(const boot_request_t *request,
                                                      const bootloader_state_t *state);

#ifdef __cplusplus
}
#endif