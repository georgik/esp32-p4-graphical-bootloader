#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/reent.h>
#include "sdkconfig.h"
#include "bootloader_custom.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "esp_log.h"
#include "esp_image_format.h"
#include "esp_rom_sys.h"

static const char *TAG = "custom-bootloader";

// Custom bootloader logic - process boot requests and ensure factory-first behavior

// Main bootloader entry point
void __attribute__((noreturn)) call_start_cpu0(void)
{
    // Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    // Handle deep sleep wake-up if needed
    bootloader_utility_load_boot_image_from_deep_sleep();
#endif

    ESP_LOGI(TAG, "=== Custom Bootloader with Factory-First Boot ===");
    ESP_LOGI(TAG, "Features: Factory default + NVS boot requests");

    // Initialize bootloader state and check for boot requests
    bootloader_state_t bs = {0};
    boot_request_t request;

    // Load partition table
    if (!bootloader_utility_load_partition_table(&bs)) {
        ESP_LOGE(TAG, "Failed to load partition table");
        bootloader_reset();
    }

    // Try to read boot request
    bool has_request = (bootloader_read_boot_request(&request) == ESP_OK);
    int boot_index;

    if (has_request) {
        int partition_type = request.next_partition_type;
        ESP_LOGI(TAG, "Boot request found: type=%d", partition_type);

        // Handle the boot request by selecting the requested partition for this boot
        switch (partition_type) {
            case 0: // Factory
                ESP_LOGI(TAG, "Request to boot factory - proceeding normally");
                boot_index = FACTORY_INDEX;
                break;
            case 1: // OTA_0
                ESP_LOGI(TAG, "Request to boot OTA_0 - will switch back to factory after this boot");
                boot_index = 0; // OTA_0 index
                bootloader_clear_boot_request();
                ESP_LOGI(TAG, "Boot request cleared - will default to factory next time");
                break;
            case 2: // OTA_1
                ESP_LOGI(TAG, "Request to boot OTA_1 - will switch back to factory after this boot");
                boot_index = 1; // OTA_1 index
                bootloader_clear_boot_request();
                ESP_LOGI(TAG, "Boot request cleared - will default to factory next time");
                break;
            default:
                ESP_LOGW(TAG, "Unknown partition type %d, defaulting to factory", partition_type);
                boot_index = FACTORY_INDEX;
                break;
        }
    } else {
        ESP_LOGI(TAG, "No boot request found - using factory-first default behavior");
        // Factory-first: always default to factory
        boot_index = FACTORY_INDEX;
    }

    ESP_LOGI(TAG, "Loading boot image from partition index: %d", boot_index);

    // Load the selected boot image (this is the critical missing piece!)
    bootloader_utility_load_boot_image(&bs, boot_index);
}

// Required for newlib support
#if CONFIG_LIBC_NEWLIB
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif