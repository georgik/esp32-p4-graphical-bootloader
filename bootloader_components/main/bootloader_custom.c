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

// Dynamic OTA partition mapping structure
typedef struct {
    const esp_partition_t* factory;
    const esp_partition_t* ota_partitions[16];  // Support up to 16 OTA partitions
    int ota_count;
} ota_partition_map_t;

static ota_partition_map_t g_ota_map = {0};

// Function to dynamically map available partitions
esp_err_t bootloader_map_partitions(const bootloader_state_t* state)
{
    ESP_LOGI(TAG, "=== Mapping Available Partitions ===");

    // Initialize the mapping structure
    memset(&g_ota_map, 0, sizeof(g_ota_map));

    // Find factory partition
    g_ota_map.factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                   ESP_PARTITION_SUBTYPE_APP_FACTORY,
                                                   NULL);
    if (g_ota_map.factory) {
        ESP_LOGI(TAG, "Found factory partition: %s at 0x%x (size: 0x%x)",
                 g_ota_map.factory->label ? g_ota_map.factory->label : "unknown",
                 g_ota_map.factory->address,
                 g_ota_map.factory->size);
    }

    // Find all OTA partitions dynamically
    for (int i = 0; i < 16; i++) {
        esp_partition_subtype_t subtype = ESP_PARTITION_SUBTYPE_APP_OTA_MIN + i;
        const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);

        if (partition) {
            if (g_ota_map.ota_count < 16) {
                g_ota_map.ota_partitions[g_ota_map.ota_count] = partition;
                ESP_LOGI(TAG, "Found OTA partition %d: %s at 0x%x (size: 0x%x)",
                         g_ota_map.ota_count,
                         partition->label ? partition->label : "unknown",
                         partition->address,
                         partition->size);
                g_ota_map.ota_count++;
            }
        } else {
            break;  // No more OTA partitions found
        }
    }

    ESP_LOGI(TAG, "Partition mapping complete: %d OTA partitions available", g_ota_map.ota_count);
    return ESP_OK;
}

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

    ESP_LOGI(TAG, "RTC boot request found: magic=0x%06x, partition_index=%d", magic, partition_type);

    // Validate partition index (0=Factory, 1-N=OTA)
    if (partition_type > g_ota_map.ota_count) {
        ESP_LOGW(TAG, "Invalid partition index %d (max: %d), defaulting to factory", partition_type, g_ota_map.ota_count);
        partition_type = 0;
    }

    // Fill our request structure
    request->magic = BOOT_REQUEST_MAGIC;
    request->version = BOOT_REQUEST_VERSION;
    request->reserved = 0;
    request->boot_count = 1;
    request->timestamp = 0;
    request->next_partition_type = partition_type;

    ESP_LOGI(TAG, "Boot request loaded from RTC: index=%d, will reset to factory after this boot", request->next_partition_type);
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
    ESP_LOGI(TAG, "=== Custom Bootloader Partition Selection (Dynamic) ===");

    // Default to factory partition if no request
    if (!request) {
        ESP_LOGI(TAG, "No boot request, defaulting to factory application");
        return g_ota_map.factory;
    }

    ESP_LOGI(TAG, "Processing boot request: index=%d, boot_count=%d",
             request->next_partition_type, request->boot_count);

    const esp_partition_t *selected_partition = NULL;
    int partition_index = request->next_partition_type;

    // Dynamic partition mapping based on request index
    if (partition_index == 0) {
        // Factory partition
        ESP_LOGI(TAG, "Selected factory partition");
        selected_partition = g_ota_map.factory;
    } else if (partition_index > 0 && partition_index <= g_ota_map.ota_count) {
        // OTA partition (1-based indexing from RTC)
        selected_partition = g_ota_map.ota_partitions[partition_index - 1];
        ESP_LOGI(TAG, "Selected OTA partition %d: %s at 0x%x (size: 0x%x) - one-time boot",
                 partition_index,
                 selected_partition->label ? selected_partition->label : "unknown",
                 selected_partition->address,
                 selected_partition->size);
    } else {
        // Invalid index, fallback to factory
        ESP_LOGW(TAG, "Invalid partition index %d, defaulting to factory", partition_index);
        selected_partition = g_ota_map.factory;
    }

    // Fallback to factory if requested partition is not available
    if (!selected_partition) {
        ESP_LOGW(TAG, "Requested partition not available, falling back to factory");
        selected_partition = g_ota_map.factory;
    }

    if (selected_partition) {
        ESP_LOGI(TAG, "Selected partition: %s at offset 0x%x (size: 0x%x)",
                 selected_partition->label ? selected_partition->label : "unknown",
                 selected_partition->address,
                 selected_partition->size);
    }

    return selected_partition;
}