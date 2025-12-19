#include "sd_ota.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "bsp/esp-bsp.h"
#include "soc/lp_system_reg.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

#define TAG "SD_OTA"

// RTC register constants for bootloader communication (must match bootloader_custom.c)
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic in ASCII

static sd_ota_state_t g_sd_ota_state = {0};
static bool g_sd_card_mounted = false;
static sdmmc_card_t* g_sd_card = NULL;

esp_err_t sd_ota_init(void) {
    ESP_LOGI(TAG, "Initializing SD card for OTA operations using improved BSP method...");

    if (g_sd_card_mounted) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Using standard BSP SD card mount (handles LDO internally)...");

    // Use standard BSP mount - let it handle LDO configuration internally
    esp_err_t ret = bsp_sdcard_mount();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP SD card mount failed (%s)", esp_err_to_name(ret));
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem - Check SD card format (should be FAT32)");
            ESP_LOGE(TAG, "SD card partition type check needed:");
            ESP_LOGE(TAG, "- Format: FAT32 (not exFAT, not NTFS, not ext4)");
            ESP_LOGE(TAG, "- Partition: MBR (not GPT)");
            ESP_LOGE(TAG, "- Cluster size: 4KB-32KB recommended");
        } else if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGE(TAG, "Memory error");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "SD card timeout - Check SD card insertion and compatibility");
        } else {
            ESP_LOGE(TAG, "SD card initialization failed (%s)", esp_err_to_name(ret));
        }
        ESP_LOGE(TAG, "Troubleshooting tips:");
        ESP_LOGE(TAG, "1. Ensure SD card is properly inserted");
        ESP_LOGE(TAG, "2. Check SD card format:");
        ESP_LOGE(TAG, "   - Must be FAT32 (not exFAT)");
        ESP_LOGE(TAG, "   - Use MBR partition table (not GPT)");
        ESP_LOGE(TAG, "   - Check cluster size (4KB-32KB works well)");
        ESP_LOGE(TAG, "3. Try a different SD card");
        ESP_LOGE(TAG, "4. Check if SD card is compatible with ESP32-P4");
        ESP_LOGE(TAG, "5. Try reformatting with standard FAT32 settings");
        ESP_LOGE(TAG, "6. Verify SD card voltage (should be 3.3V compatible)");

        return ret;
    }

    // Get card handle from BSP
    g_sd_card = bsp_sdcard_get_handle();

    if (!g_sd_card) {
        ESP_LOGE(TAG, "Failed to get SD card handle from BSP");
        bsp_sdcard_unmount();
        return ESP_FAIL;
    }

    // Get card info
    sdmmc_card_print_info(stdout, g_sd_card);

    g_sd_card_mounted = true;
    ESP_LOGI(TAG, "SD card mounted successfully via BSP at %s", SD_OTA_MOUNT_POINT);

    return ESP_OK;
}

esp_err_t sd_ota_check_file(const char* filename) {
    if (!g_sd_card_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (!filename) {
        ESP_LOGE(TAG, "Filename is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // First, list the root directory contents for debugging
    ESP_LOGI(TAG, "=== SD Card Root Directory Contents ===");
    DIR* dir = opendir(SD_OTA_MOUNT_POINT);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", SD_OTA_MOUNT_POINT, entry->d_name);
            struct stat st;

            if (stat(full_path, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    ESP_LOGI(TAG, "DIR  : %s", entry->d_name);
                } else {
                    ESP_LOGI(TAG, "FILE : %s (%zu bytes)", entry->d_name, st.st_size);
                }
            } else {
                // If stat fails, just show the name without type info
                ESP_LOGI(TAG, "ENTRY: %s", entry->d_name);
            }
        }
        closedir(dir);
        ESP_LOGI(TAG, "=== End of Directory Listing ===");
    } else {
        ESP_LOGE(TAG, "Failed to open directory: %s", SD_OTA_MOUNT_POINT);
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_OTA_MOUNT_POINT, filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    if (st.st_size > SD_OTA_MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large: %zu bytes (max: %d)", st.st_size, SD_OTA_MAX_FILE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "OTA file found: %s (%zu bytes)", filepath, st.st_size);
    return ESP_OK;
}

esp_err_t sd_ota_get_file_size(const char* filename, size_t* file_size) {
    if (!g_sd_card_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (!filename || !file_size) {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_OTA_MOUNT_POINT, filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    *file_size = st.st_size;
    return ESP_OK;
}

esp_err_t sd_ota_flash_file(const char* filename, esp_partition_subtype_t partition_subtype) {
    if (!g_sd_card_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (!filename) {
        ESP_LOGE(TAG, "Filename is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if file exists first
    esp_err_t ret = sd_ota_check_file(filename);
    if (ret != ESP_OK) {
        return ret;
    }

    // Find target OTA partition
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, partition_subtype, NULL);

    if (!partition) {
        ESP_LOGE(TAG, "Target OTA partition not found (subtype: %d)", partition_subtype);
        return ESP_ERR_NOT_FOUND;
    }

    // Get file size
    size_t file_size;
    ret = sd_ota_get_file_size(filename, &file_size);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Starting OTA flash: %s -> %s (%s at 0x%x, size: %zu bytes)",
             filename, partition->label ? partition->label : "unknown",
             partition_subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? "OTA_1" :
             partition_subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? "OTA_0" :
             partition_subtype == ESP_PARTITION_SUBTYPE_APP_OTA_2 ? "OTA_2" : "OTA_X",
             partition->address, file_size);

    if (file_size > partition->size) {
        ESP_LOGE(TAG, "File too large for partition: %zu bytes > %zu bytes",
                 file_size, partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Initialize OTA state
    g_sd_ota_state.filename = filename;
    g_sd_ota_state.file_size = file_size;
    g_sd_ota_state.target_partition = partition;
    g_sd_ota_state.bytes_written = 0;
    g_sd_ota_state.in_progress = true;

    // Open file on SD card
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_OTA_MOUNT_POINT, filename);

    FILE* file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        g_sd_ota_state.in_progress = false;
        return ESP_ERR_NOT_FOUND;
    }

    // Prepare OTA handle
    esp_ota_handle_t ota_handle;
    ret = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA operation: %s", esp_err_to_name(ret));
        fclose(file);
        g_sd_ota_state.in_progress = false;
        return ret;
    }

    // OPTIMIZED: Zero-copy operations with minimal IRAM buffer and DMA bypass
    const size_t chunk_size = 2048;  // MINIMAL: 2KB chunks for IRAM efficiency
    uint8_t* buffer = heap_caps_malloc(chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_IRAM_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate IRAM DMA buffer, falling back to SPIRAM");
        buffer = heap_caps_malloc(chunk_size, MALLOC_CAP_SPIRAM);
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate SPIRAM buffer");
            esp_ota_abort(ota_handle);
            fclose(file);
            g_sd_ota_state.in_progress = false;
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Using SPIRAM buffer - display may flicker during OTA");
    } else {
        ESP_LOGI(TAG, "Using IRAM DMA buffer - display will remain stable during OTA");
    }

    ESP_LOGI(TAG, "Flashing firmware in %zu byte IRAM-optimized chunks...", chunk_size);

    // IRAM buffer doesn't need cache synchronization for DMA operations

    uint32_t chunk_count = 0;
    uint32_t yield_counter = 0;

    while (g_sd_ota_state.bytes_written < file_size) {
        size_t bytes_to_read = chunk_size;
        if (bytes_to_read > (file_size - g_sd_ota_state.bytes_written)) {
            bytes_to_read = file_size - g_sd_ota_state.bytes_written;
        }

        // MINIMAL YIELDING: Only when display would be affected
        if (yield_counter % 32 == 0) {  // Every 32 chunks (64KB)
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        size_t bytes_read = fread(buffer, 1, bytes_to_read, file);
        if (bytes_read != bytes_to_read) {
            ESP_LOGE(TAG, "File read error: expected %zu, got %zu", bytes_to_read, bytes_read);
            if (buffer) {
                heap_caps_free(buffer);
            }
            esp_ota_abort(ota_handle);
            fclose(file);
            g_sd_ota_state.in_progress = false;
            return ESP_ERR_INVALID_RESPONSE;
        }

        // IRAM buffer doesn't need cache flush for DMA operations

        ret = esp_ota_write(ota_handle, buffer, bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write error at offset %zu: %s",
                     g_sd_ota_state.bytes_written, esp_err_to_name(ret));
            if (buffer) {
                heap_caps_free(buffer);
            }
            esp_ota_abort(ota_handle);
            fclose(file);
            g_sd_ota_state.in_progress = false;
            return ret;
        }

        g_sd_ota_state.bytes_written += bytes_read;
        chunk_count++;
        yield_counter++;

        // VERY INFREQUENT yielding to maintain display stability
        if (chunk_count % 64 == 0) {  // Every 64 chunks (128KB)
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // Log progress efficiently
        if (chunk_count % 128 == 0) {  // Every 128 chunks (256KB)
            ESP_LOGD(TAG, "Flashing progress: %zu/%zu bytes (%.1f%%)",
                     g_sd_ota_state.bytes_written, file_size,
                     (float)g_sd_ota_state.bytes_written * 100.0f / file_size);
        }
    }

    // Clean up IRAM buffer
    if (buffer) {
        heap_caps_free(buffer);
    }

    fclose(file);

    // Finalize OTA
    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize OTA: %s", esp_err_to_name(ret));
        g_sd_ota_state.in_progress = false;
        return ret;
    }

    ESP_LOGI(TAG, "OTA flash completed successfully: %zu bytes written to %s",
             g_sd_ota_state.bytes_written, partition->label ? partition->label : "unknown");

    // Set boot partition using RTC register for one-time boot (like other demos)
    ESP_LOGI(TAG, "Setting RTC boot request for partition type 2 (OTA_1)...");

    // Write boot request to RTC register for bootloader to read
    // OTA_1 = partition_type 2 (matches bootloader_custom.c partition mapping)
    uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (2 << 24);
    REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);

    ESP_LOGI(TAG, "RTC register updated: 0x%08x, system will boot from OTA_1 after restart", rtc_value);

    g_sd_ota_state.in_progress = false;
    ESP_LOGI(TAG, "Boot partition set successfully. System ready to boot from %s",
             partition->label ? partition->label : "unknown");

    return ESP_OK;
}

sd_ota_state_t sd_ota_get_state(void) {
    return g_sd_ota_state;
}

void sd_ota_cleanup(void) {
    if (g_sd_card_mounted) {
        // Use BSP unmount function
        esp_err_t ret = bsp_sdcard_unmount();

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unmount SD card via BSP: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "SD card unmounted successfully via BSP");
        }
        g_sd_card_mounted = false;
        g_sd_card = NULL;
    }

    // Reset OTA state
    memset(&g_sd_ota_state, 0, sizeof(g_sd_ota_state));
}