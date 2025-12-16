#include "bootloader_custom_simple.h"
#include "esp_log.h"
#include "bootloader_common.h"
#include "esp_rom_crc.h"

#define TAG "bootloader_custom"

esp_err_t bootloader_read_boot_request_flash(boot_request_t *request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read boot request from fixed flash location
    esp_err_t ret = bootloader_flash_read(BOOT_REQUEST_OFFSET, request, sizeof(boot_request_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read boot request from flash (0x%x)", ret);
        return ret;
    }

    // Validate the request
    if (request->magic != BOOT_REQUEST_MAGIC) {
        ESP_LOGI(TAG, "No valid boot request found (magic: 0x%08x)", request->magic);
        return ESP_ERR_NOT_FOUND;
    }

    if (request->version != BOOT_REQUEST_VERSION) {
        ESP_LOGW(TAG, "Unsupported boot request version: %d", request->version);
        return ESP_ERR_INVALID_VERSION;
    }

    ESP_LOGI(TAG, "Boot request found: next_partition_type=%d, boot_count=%d",
             request->next_partition_type, request->boot_count);

    return ESP_OK;
}

esp_err_t bootloader_clear_boot_request_flash(void)
{
    // For simplicity, we just erase the boot request area
    // The next boot will treat it as no request since magic won't match
    esp_err_t ret = bootloader_flash_erase_range(BOOT_REQUEST_OFFSET, sizeof(boot_request_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase boot request area (0x%x)", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Boot request area cleared from flash");
    return ESP_OK;
}

const esp_partition_t* bootloader_get_boot_partition(const boot_request_t *request,
                                                      const bootloader_state_t *state)
{
    // Default to factory partition
    if (!request) {
        ESP_LOGI(TAG, "No boot request, defaulting to factory application");
        return NULL; // Will be handled by caller
    }

    ESP_LOGI(TAG, "Processing boot request: type=%d, boot_count=%d",
             request->next_partition_type, request->boot_count);

    const esp_partition_t *selected_partition = NULL;

    switch (request->next_partition_type) {
        case 0: // Factory
            ESP_LOGI(TAG, "Selected factory partition");
            selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                        NULL);
            break;

        case 1: // OTA_0
            ESP_LOGI(TAG, "Selected OTA_0 partition");
            selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                        NULL);
            break;

        case 2: // OTA_1
            ESP_LOGI(TAG, "Selected OTA_1 partition");
            selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                        NULL);
            break;

        default:
            ESP_LOGW(TAG, "Unknown partition type %d, defaulting to factory",
                     request->next_partition_type);
            break;
    }

    // Fallback to factory if requested partition is not available
    if (!selected_partition) {
        ESP_LOGW(TAG, "Requested partition not available, falling back to factory");
        selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                    ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                    NULL);
    }

    return selected_partition;
}