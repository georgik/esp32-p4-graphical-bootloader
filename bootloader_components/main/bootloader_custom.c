#include "bootloader_custom.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "bootloader_common.h"
#include "esp_image_format.h"
#include "bootloader_init.h"

#define TAG "bootloader_custom"
#define BOOT_REQUEST_NAMESPACE "boot_req"
#define BOOT_REQUEST_KEY "next_boot"

esp_err_t bootloader_custom_init(void)
{
    esp_err_t ret = nvs_flash_init_partition("nvs");
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase_partition("nvs"));
        ret = nvs_flash_init_partition("nvs");
    }

    return ret;
}

esp_err_t bootloader_read_boot_request(boot_request_t *request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open_from_partition("nvs", BOOT_REQUEST_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No boot request found in NVS (0x%x)", ret);
        return ret;
    }

    size_t required_size = sizeof(boot_request_t);
    ret = nvs_get_blob(nvs_handle, BOOT_REQUEST_KEY, request, &required_size);
    nvs_close(nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to read boot request (0x%x)", ret);
        return ret;
    }

    if (required_size != sizeof(boot_request_t)) {
        ESP_LOGW(TAG, "Boot request size mismatch, expected %d, got %d",
                 sizeof(boot_request_t), required_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (request->magic != BOOT_REQUEST_MAGIC) {
        ESP_LOGW(TAG, "Invalid boot request magic: 0x%08x", request->magic);
        return ESP_ERR_INVALID_CRC;
    }

    if (request->version != BOOT_REQUEST_VERSION) {
        ESP_LOGW(TAG, "Unsupported boot request version: %d", request->version);
        return ESP_ERR_INVALID_VERSION;
    }

    ESP_LOGI(TAG, "Boot request found: next_partition_type=%d, boot_count=%d",
             request->next_partition_type, request->boot_count);

    return ESP_OK;
}

esp_err_t bootloader_clear_boot_request(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open_from_partition("nvs", BOOT_REQUEST_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_erase_key(nvs_handle, BOOT_REQUEST_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // Key doesn't exist, that's OK
        ret = ESP_OK;
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Boot request cleared from NVS");
    } else {
        ESP_LOGE(TAG, "Failed to clear boot request (0x%x)", ret);
    }

    return ret;
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

// bootloader_get_state is not needed since we use ESP-IDF's bootloader_state_t