#include "bootloader_custom.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "bootloader_utility.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"  // For REG_READ and REG_WRITE macros

#define TAG "bootloader_custom"

// RTC store register for boot requests (STORE0 is reserved in ESP32-P4)
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic in ASCII

typedef struct {
    uint32_t magic;
    uint32_t partition_type;  // 0=Factory, 1=OTA_0, 2=OTA_1
} rtc_boot_request_t;

esp_err_t bootloader_read_boot_request(boot_request_t *request)
{
    ESP_LOGI(TAG, "=== Custom Bootloader Active (RTC-based) ===");

    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read RTC store register for boot request
    uint32_t rtc_value = REG_READ(BOOT_REQUEST_RTC_REG);
    ESP_LOGI(TAG, "RTC store register value: 0x%08x", rtc_value);

    // Check if we have a valid boot request in RTC register
    // Extract magic from lower 24 bits and partition type from upper 8 bits
    uint32_t magic = rtc_value & 0x00FFFFFF;
    uint32_t partition_type = (rtc_value >> 24) & 0xFF;

    if (magic != BOOT_REQUEST_MAGIC_RTC) {
        ESP_LOGI(TAG, "No valid boot request found in RTC register. Magic: 0x%06x (expected 0x%06x)", magic, BOOT_REQUEST_MAGIC_RTC);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "RTC boot request found: magic=0x%06x, partition_type=%d", magic, partition_type);

    // Validate partition type
    if (partition_type > 2) {
        ESP_LOGW(TAG, "Invalid partition type %d in RTC request, defaulting to factory", partition_type);
        partition_type = 0;
    }

    // Fill our request structure
    request->magic = BOOT_REQUEST_MAGIC;
    request->version = BOOT_REQUEST_VERSION;
    request->reserved = 0;
    request->boot_count = 1;
    request->timestamp = 0;
    request->next_partition_type = partition_type;

    ESP_LOGI(TAG, "Boot request loaded from RTC: type=%d, will reset to factory after this boot", request->next_partition_type);
    return ESP_OK;
}

esp_err_t bootloader_clear_boot_request(void)
{
    ESP_LOGI(TAG, "Boot request clear called - clearing RTC register");

    // Clear the RTC store register to remove the boot request
    REG_WRITE(BOOT_REQUEST_RTC_REG, 0);

    ESP_LOGI(TAG, "RTC boot request cleared - will default to factory next time");
    return ESP_OK;
}

const esp_partition_t* bootloader_get_boot_partition(const boot_request_t *request,
                                                      const bootloader_state_t *state)
{
    ESP_LOGI(TAG, "=== Custom Bootloader Partition Selection ===");

    // Default to factory partition if no request
    if (!request) {
        ESP_LOGI(TAG, "No boot request, defaulting to factory application");
        return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                        ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                        NULL);
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
            ESP_LOGI(TAG, "Selected OTA_0 partition (one-time boot - will revert to factory next time)");
            selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                                        NULL);
            break;

        case 2: // OTA_1
            ESP_LOGI(TAG, "Selected OTA_1 partition (one-time boot - will revert to factory next time)");
            selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                                        NULL);
            break;

        default:
            ESP_LOGW(TAG, "Unknown partition type %d, defaulting to factory",
                     request->next_partition_type);
            selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                        ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                        NULL);
            break;
    }

    // Fallback to factory if requested partition is not available
    if (!selected_partition) {
        ESP_LOGW(TAG, "Requested partition not available, falling back to factory");
        selected_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                    ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                    NULL);
    }

    if (selected_partition) {
        ESP_LOGI(TAG, "Selected partition: %s at offset 0x%x",
                 selected_partition->label ? selected_partition->label : "unknown",
                 selected_partition->address);
    }

    return selected_partition;
}