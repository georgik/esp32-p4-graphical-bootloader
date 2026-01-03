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

    // Map available partitions dynamically to populate g_ota_map
    if (bootloader_map_partitions(&bs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to map partitions");
        bootloader_reset();
    }

    // Try to read boot request
    bool has_request = (bootloader_read_boot_request(&request) == ESP_OK);
    int boot_index;

    if (has_request) {
        int partition_index = request.next_partition_type;
        ESP_LOGI(TAG, "Boot request found: partition_index=%d", partition_index);

        // Map partition index to bootloader index
        if (partition_index == 0) {
            // Factory partition
            boot_index = FACTORY_INDEX;
            ESP_LOGI(TAG, "Booting factory partition per request");
        } else if (partition_index > 0) {
            // OTA partition (1-based from RTC, but bootloader expects 0-based)
            boot_index = partition_index - 1;
            ESP_LOGI(TAG, "Booting OTA partition %d (bootloader index %d) per request", partition_index, boot_index);
        } else {
            ESP_LOGW(TAG, "Invalid partition index %d, defaulting to factory", partition_index);
            boot_index = FACTORY_INDEX;
        }

        // Clear boot request after processing (one-time boot)
        bootloader_clear_boot_request();
        ESP_LOGI(TAG, "Boot request cleared - will default to factory next time");
    } else {
        ESP_LOGI(TAG, "No boot request found - using factory-first default behavior");
        // Factory-first: always default to factory
        boot_index = FACTORY_INDEX;
    }

    ESP_LOGI(TAG, "Loading boot image from bootloader index: %d", boot_index);

    // Get the partition to boot from
    const esp_partition_t* boot_partition = NULL;
    if (boot_index == FACTORY_INDEX) {
        // Factory partition
        boot_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                     ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                     NULL);
        if (boot_partition) {
            ESP_LOGI(TAG, "Booting from factory partition: %s at 0x%x",
                     boot_partition->label, boot_partition->address);
        } else {
            ESP_LOGE(TAG, "Factory partition not found!");
            bootloader_reset();
        }
    } else if (boot_index >= 0) {
        // OTA partition
        boot_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                     ESP_PARTITION_SUBTYPE_APP_OTA_MIN + boot_index,
                                                     NULL);
        if (boot_partition) {
            ESP_LOGI(TAG, "Booting from OTA partition %d: %s at 0x%x",
                     boot_index, boot_partition->label, boot_partition->address);
        } else {
            ESP_LOGE(TAG, "OTA partition %d not found!", boot_index);
            bootloader_reset();
        }
    }

    if (boot_partition) {
        // Load the boot image from the selected partition
        bootloader_utility_load_boot_image(&bs, boot_index);
    }

    // Should never reach here
    bootloader_reset();
}

// Required for newlib support
#if CONFIG_LIBC_NEWLIB
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif