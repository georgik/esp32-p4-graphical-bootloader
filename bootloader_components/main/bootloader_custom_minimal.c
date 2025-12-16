#include "bootloader_custom_simple.h"

// Minimal implementation - just returns success for now
esp_err_t bootloader_read_boot_request_flash(boot_request_t *request)
{
    // For now, just return no request - we'll implement flash reading later
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bootloader_clear_boot_request_flash(void)
{
    // For now, just return success - we'll implement flash erase later
    return ESP_OK;
}

const esp_partition_t* bootloader_get_boot_partition(const boot_request_t *request,
                                                      const bootloader_state_t *state)
{
    // Always return factory partition for now
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                    NULL);
}