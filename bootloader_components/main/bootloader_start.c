#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/reent.h>
#include "sdkconfig.h"
#include "bootloader_custom_simple.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "esp_log.h"
#include "esp_image_format.h"
#include "esp_rom_sys.h"

static const char *TAG = "custom-bootloader";

// Function to select boot partition based on our custom logic
static int select_boot_partition(bootloader_state_t *bs, const boot_request_t *request, bool has_request)
{
    // Load partition table first
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "Failed to load partition table");
        return INVALID_INDEX;
    }

    if (has_request) {
        ESP_LOGI(TAG, "Boot request found: type=%d", request->next_partition_type);

        // Map our custom partition types to bootloader indices
        switch (request->next_partition_type) {
            case 0: // Factory
                return bs->factory.offset ? 0 : INVALID_INDEX;
            case 1: // OTA_0
                return bs->ota[0].offset ? 1 : INVALID_INDEX;
            case 2: // OTA_1
                return bs->ota[1].offset ? 2 : INVALID_INDEX;
            default:
                ESP_LOGW(TAG, "Unknown partition type %d, using default", request->next_partition_type);
                break;
        }
    }

    // Default behavior: always boot factory
    ESP_LOGI(TAG, "No boot request, defaulting to factory partition");
    return bs->factory.offset ? 0 : INVALID_INDEX;
}

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

    // Initialize bootloader state
    bootloader_state_t bs = {0};

    // Check for boot request before partition selection
    boot_request_t request;
    bool has_request = (bootloader_read_boot_request_flash(&request) == ESP_OK);

    // Select boot partition using our custom logic
    int boot_index = select_boot_partition(&bs, &request, has_request);
    if (boot_index == INVALID_INDEX) {
        ESP_LOGE(TAG, "No valid boot partition found!");
        bootloader_reset();
    }

    // Clear boot request after processing (to ensure factory-first on next boot)
    if (has_request) {
        bootloader_clear_boot_request_flash();
        ESP_LOGI(TAG, "Boot request cleared - will default to factory next time");
    }

    // Log what we're booting
    const char *partition_names[] = {"Factory", "OTA_0", "OTA_1"};
    ESP_LOGI(TAG, "Booting from: %s (index %d)",
             boot_index < 3 ? partition_names[boot_index] : "Unknown", boot_index);

    // Load and boot the application
    bootloader_utility_load_boot_image(&bs, boot_index);

    // Should never reach here
    ESP_LOGE(TAG, "Bootloader should not reach this point!");
    while (1) {
        // Use ROM delay function - available in bootloader context
        esp_rom_delay_us(1000000);
    }
}

// Required for newlib support
#if CONFIG_LIBC_NEWLIB
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif