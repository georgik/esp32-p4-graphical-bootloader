#include "bootloader_api.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

#define TAG "bootloader_api"
#define BOOT_REQUEST_NAMESPACE "boot_req"
#define BOOT_REQUEST_KEY "next_boot"
#define BOOT_REQUEST_OFFSET  0x300000    // Fixed offset in flash for boot request

// Boot request structure matching bootloader
typedef struct {
    uint32_t magic;              // 0x50415445 ("PETE")
    uint8_t version;             // Structure version
    uint8_t next_partition_type; // Next boot partition type
    uint8_t reserved;            // Reserved
    uint8_t boot_count;          // Boot count
    uint32_t timestamp;          // Timestamp
} boot_request_t;

#define BOOT_REQUEST_MAGIC  0x50415445
#define BOOT_REQUEST_VERSION 1

esp_err_t bootloader_request_next_boot(boot_partition_type_t partition_type)
{
    ESP_LOGI(TAG, "Requesting next boot partition: %d", partition_type);

    if (partition_type > BOOT_PARTITION_OTA_1) {
        ESP_LOGE(TAG, "Invalid partition type: %d", partition_type);
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS (0x%x)", ret);
        return ret;
    }

    // Create boot request
    boot_request_t request = {
        .magic = BOOT_REQUEST_MAGIC,
        .version = BOOT_REQUEST_VERSION,
        .next_partition_type = (uint8_t)partition_type,
        .reserved = 0,
        .boot_count = 0,
        .timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL)
    };

    // Store in NVS for applications to use
    nvs_handle_t nvs_handle;
    ret = nvs_open(BOOT_REQUEST_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace (0x%x)", ret);
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, BOOT_REQUEST_KEY, &request, sizeof(request));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write boot request (0x%x)", ret);
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Boot request stored successfully");
        ESP_LOGI(TAG, "Next boot will be from partition type: %d", partition_type);
    } else {
        ESP_LOGE(TAG, "Failed to commit boot request (0x%x)", ret);
    }

    return ret;
}

esp_err_t bootloader_has_pending_request(bool *has_request)
{
    if (!has_request) {
        return ESP_ERR_INVALID_ARG;
    }

    *has_request = false;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    // Check if request exists in NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open(BOOT_REQUEST_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ESP_OK; // No namespace = no request
    }

    boot_request_t request;
    size_t required_size = sizeof(request);
    ret = nvs_get_blob(nvs_handle, BOOT_REQUEST_KEY, &request, &required_size);
    nvs_close(nvs_handle);

    if (ret == ESP_OK && required_size == sizeof(request) &&
        request.magic == BOOT_REQUEST_MAGIC && request.version == BOOT_REQUEST_VERSION) {
        *has_request = true;
        ESP_LOGI(TAG, "Pending boot request found for partition type %d", request.next_partition_type);
    }

    return ESP_OK;
}

esp_err_t bootloader_clear_pending_request(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear request from NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open(BOOT_REQUEST_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ESP_OK; // No namespace = nothing to clear
    }

    ret = nvs_erase_key(nvs_handle, BOOT_REQUEST_KEY);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK; // Key doesn't exist = already cleared
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Boot request cleared");
    }

    return ret;
}

esp_err_t bootloader_get_current_partition(boot_partition_type_t *current_partition)
{
    if (!current_partition) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get the current running partition
    const esp_partition_t *running_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);

    if (!running_partition) {
        ESP_LOGE(TAG, "Failed to find app partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Current running partition: %s (subtype: %d)",
             running_partition->label, running_partition->subtype);

    switch (running_partition->subtype) {
        case ESP_PARTITION_SUBTYPE_APP_FACTORY:
            *current_partition = BOOT_PARTITION_FACTORY;
            break;
        case ESP_PARTITION_SUBTYPE_APP_OTA_0:
            *current_partition = BOOT_PARTITION_OTA_0;
            break;
        case ESP_PARTITION_SUBTYPE_APP_OTA_1:
            *current_partition = BOOT_PARTITION_OTA_1;
            break;
        default:
            ESP_LOGW(TAG, "Unknown partition subtype: %d, defaulting to factory", running_partition->subtype);
            *current_partition = BOOT_PARTITION_FACTORY;
            break;
    }

    ESP_LOGI(TAG, "Current partition type: %d", *current_partition);
    return ESP_OK;
}
