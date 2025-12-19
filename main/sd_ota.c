#include "sd_ota.h"
#include "vdma_protection.h"
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

// Callbacks for UI updates
static void (*g_progress_callback)(uint8_t progress) = NULL;
static void (*g_status_callback)(const char* status) = NULL;

// Pre-allocated IRAM buffer to prevent fragmentation
static uint8_t* g_preallocated_buffer = NULL;
static size_t g_preallocated_size = 0;

esp_err_t sd_ota_init(void) {
    ESP_LOGI(TAG, "Initializing SD card for OTA operations using improved BSP method...");

    if (g_sd_card_mounted) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }

    // Pre-allocate IRAM buffer early to prevent fragmentation
    if (!g_preallocated_buffer) {
        // Try to allocate a small buffer during initialization
        g_preallocated_buffer = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_IRAM_8BIT);
        if (g_preallocated_buffer) {
            g_preallocated_size = 512;
            ESP_LOGI(TAG, "Pre-allocated 512-byte IRAM buffer for OTA operations");
        } else {
            ESP_LOGW(TAG, "Could not pre-allocate IRAM buffer - will try during OTA");
        }
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

    // Skip directory listing to reduce PSRAM contention and prevent flickering
    ESP_LOGI(TAG, "Checking for OTA file: %s", filename);

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_OTA_MOUNT_POINT, filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "OTA file not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    if (st.st_size > SD_OTA_MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "OTA file too large: %zu bytes (max: %d)", st.st_size, SD_OTA_MAX_FILE_SIZE);
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

    // OPTIMIZED: Try to use pre-allocated IRAM buffer first, then fall back to other strategies
    uint8_t* buffer = NULL;
    size_t chunk_size = 0;
    bool is_stack_buffer = false;

    // Strategy 1: Use pre-allocated IRAM buffer (if available)
    if (g_preallocated_buffer && g_preallocated_size > 0) {
        buffer = g_preallocated_buffer;
        chunk_size = g_preallocated_size;
        ESP_LOGI(TAG, "Using pre-allocated IRAM buffer (%zu bytes) - display will remain stable");
    } else {
        // Strategy 2: Try very small 256-byte IRAM chunks (most likely to succeed)
        chunk_size = 256;
        buffer = heap_caps_malloc(chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_IRAM_8BIT);
        if (buffer) {
            ESP_LOGI(TAG, "Allocated 256-byte IRAM DMA buffer - display will remain stable");
        } else {
            // Strategy 3: Try 512-byte IRAM chunks
            chunk_size = 512;
            buffer = heap_caps_malloc(chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_IRAM_8BIT);
            if (buffer) {
                ESP_LOGI(TAG, "Allocated 512-byte IRAM DMA buffer - display will remain stable");
            } else {
                // Strategy 4: Try 1KB IRAM chunks
                chunk_size = 1024;
                buffer = heap_caps_malloc(chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_IRAM_8BIT);
                if (buffer) {
                    ESP_LOGI(TAG, "Allocated 1KB IRAM DMA buffer - display will remain stable");
                } else {
                    // Strategy 5: Use stack-allocated buffer (no heap fragmentation)
                    static uint8_t stack_buffer[128];  // 128 bytes on stack
                    chunk_size = 128;
                    buffer = stack_buffer;
                    is_stack_buffer = true;
                    ESP_LOGW(TAG, "Using 128-byte stack buffer - minimal memory usage");
                }
            }
        }
    }

    ESP_LOGI(TAG, "Starting optimized OTA: 32-byte SD reads, %zu-byte flash writes", chunk_size);

    // IRAM buffer doesn't need cache synchronization for DMA operations

    uint32_t chunk_count = 0;

    // MEMORY-ISOLATED: Use PSRAM for OTA operations while display uses IRAM
    ESP_LOGI(TAG, "Starting PSRAM-isolated OTA: Moving OTA to PSRAM, display to IRAM");

    // EXPLICITLY use PSRAM for OTA operations to avoid display contention
    size_t psram_buffer_size = 4096;  // 4KB PSRAM buffer for efficient transfers

    // Force allocation from EXTERNAL memory (PSRAM) - NOT internal IRAM
    uint8_t* psram_buffer = heap_caps_malloc(psram_buffer_size,
                                           MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);

    if (!psram_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer, trying fallback");
        // Use smaller chunk size for fallback
        psram_buffer_size = 512;
        psram_buffer = malloc(psram_buffer_size);  // Use standard malloc
        if (!psram_buffer) {
            ESP_LOGE(TAG, "Failed to allocate any OTA buffer");
            esp_ota_abort(ota_handle);
            fclose(file);
            g_sd_ota_state.in_progress = false;
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Using small fallback buffer - display may still flicker");
    } else {
        ESP_LOGI(TAG, "Allocated %zu-byte PSRAM buffer for OTA operations", psram_buffer_size);
        ESP_LOGI(TAG, "Display runs from IRAM, OTA runs from PSRAM - complete memory isolation");
    }

    // MAXIMUM THROTTLING: Use extremely small chunks for zero display interference
    size_t psram_chunk_size;
    if (psram_buffer_size >= 4096) {
        psram_chunk_size = 128;   // MAXIMUM: Extremely small chunks for complete display stability
    } else {
        psram_chunk_size = 16;    // MAXIMUM: Tiny fallback chunks for absolute minimum interference
    }
    size_t psram_buffer_pos = 0;

    ESP_LOGI(TAG, "MAXIMUM THROTTLING: Using %zu-byte chunks with %zu-byte buffer", psram_chunk_size, psram_buffer_size);

    // MAXIMUM VDMA PROTECTION: Extreme display protection for SD card operations
    uint32_t display_yield_counter = 0;
    const uint32_t display_yield_interval = 2;  // MAXIMUM: Yield every 2 chunks (was 8) for absolute safety

    while (g_sd_ota_state.bytes_written < file_size) {
        // ENHANCED VDMA: Check if display is currently protected
        if (vdma_is_display_protected()) {
            // Display is currently rendering, wait for protection to be disabled
            vTaskDelay(pdMS_TO_TICKS(5));  // Short wait for display to complete
        }

        // AGGRESSIVE: VDMA coordination before every major PSRAM operation
        if (display_yield_counter % display_yield_interval == 0) {
            // Ensure display refresh completes before intensive SD card operations
            vdma_ensure_display_refresh(25);  // AGGRESSIVE: Ensure 25ms (was 16ms) for display refresh
            vTaskDelay(pdMS_TO_TICKS(5));     // AGGRESSIVE: Additional 5ms pause for display stability
            ESP_LOGD(TAG, "AGGRESSIVE VDMA: Extended display refresh before PSRAM operations");
        }

        // Fill PSRAM buffer with data from SD card - VDMA-coordinated operations
        while (psram_buffer_pos < psram_buffer_size && g_sd_ota_state.bytes_written < file_size) {
            size_t bytes_to_read = psram_chunk_size;
            size_t remaining = file_size - g_sd_ota_state.bytes_written;
            if (bytes_to_read > remaining) {
                bytes_to_read = remaining;
            }

            // ENHANCED VDMA: Coordinate SD card read with display protection
            // Brief micro-yield before SD card operation to allow display DMA access
            taskYIELD();

            size_t bytes_read = fread(psram_buffer + psram_buffer_pos, 1, bytes_to_read, file);
            if (bytes_read != bytes_to_read) {
                ESP_LOGE(TAG, "File read error: expected %zu, got %zu", bytes_to_read, bytes_read);
                heap_caps_free(psram_buffer);
                if (buffer && !is_stack_buffer && buffer != g_preallocated_buffer) {
                    heap_caps_free(buffer);
                }
                esp_ota_abort(ota_handle);
                fclose(file);
                g_sd_ota_state.in_progress = false;
                return ESP_ERR_INVALID_RESPONSE;
            }

            psram_buffer_pos += bytes_read;
            g_sd_ota_state.bytes_written += bytes_read;
            chunk_count++;
            display_yield_counter++;

            // CRITICAL: Micro-yield between small operations to prevent display blocking
            if (display_yield_counter % 2 == 0) {
                taskYIELD();  // Give display controller micro-time slices
            }
        }

        // Write accumulated PSRAM buffer to flash in one operation
        ret = esp_ota_write(ota_handle, psram_buffer, psram_buffer_pos);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write error at offset %zu: %s",
                     g_sd_ota_state.bytes_written - psram_buffer_pos, esp_err_to_name(ret));
            heap_caps_free(psram_buffer);
            if (buffer && !is_stack_buffer && buffer != g_preallocated_buffer) {
                heap_caps_free(buffer);
            }
            esp_ota_abort(ota_handle);
            fclose(file);
            g_sd_ota_state.in_progress = false;
            return ret;
        }

        // Reset buffer position for next chunk
        psram_buffer_pos = 0;

        // Update progress every ~64KB processed (less frequent UI updates)
        if (chunk_count % 64 == 0) {
            float progress_percent = (float)g_sd_ota_state.bytes_written * 100.0f / file_size;
            ESP_LOGI(TAG, "Progress: %zu/%zu bytes (%.1f%%)", g_sd_ota_state.bytes_written, file_size, progress_percent);

            // Call progress callback if set
            if (g_progress_callback) {
                g_progress_callback((uint8_t)progress_percent);
            }
        }

        // Optional: Small delay every few chunks to ensure LVGL gets CPU time
        if (chunk_count % 16 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Clean up buffer (use appropriate free function based on allocation)
    if (psram_buffer_size >= 4096) {
        heap_caps_free(psram_buffer);  // Was allocated with heap_caps_malloc (PSRAM)
    } else {
        free(psram_buffer);  // Was allocated with malloc (fallback)
    }

    // Clean up buffer (only free if heap-allocated)
    if (buffer && !is_stack_buffer && buffer != g_preallocated_buffer) {
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

    // Clean up pre-allocated buffer
    if (g_preallocated_buffer) {
        heap_caps_free(g_preallocated_buffer);
        g_preallocated_buffer = NULL;
        g_preallocated_size = 0;
        ESP_LOGI(TAG, "Cleaned up pre-allocated IRAM buffer");
    }

    // Reset OTA state
    memset(&g_sd_ota_state, 0, sizeof(g_sd_ota_state));
}

// Callback setters
void sd_ota_set_progress_callback(void (*callback)(uint8_t progress)) {
    g_progress_callback = callback;
}

void sd_ota_set_status_callback(void (*callback)(const char* status)) {
    g_status_callback = callback;
}

// Start SD Card OTA process
esp_err_t sd_ota_start(void) {
    ESP_LOGI(TAG, "Starting SD Card OTA process for ota1.bin...");

    if (!g_sd_card_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        if (g_status_callback) {
            g_status_callback("Error: SD card not available");
        }
        return ESP_ERR_INVALID_STATE;
    }

    // Notify UI
    if (g_status_callback) {
        g_status_callback("Checking for ota1.bin...");
    }

    // Check if ota1.bin exists
    esp_err_t ret = sd_ota_check_file(SD_OTA_FILENAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota1.bin not found or not readable");
        if (g_status_callback) {
            g_status_callback("Error: ota1.bin not found");
        }
        return ret;
    }

    // Get file size
    size_t file_size;
    ret = sd_ota_get_file_size(SD_OTA_FILENAME, &file_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get ota1.bin size");
        if (g_status_callback) {
            g_status_callback("Error: Failed to read file size");
        }
        return ret;
    }

    ESP_LOGI(TAG, "Found ota1.bin: %zu bytes", file_size);

    if (file_size > SD_OTA_MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large: %zu bytes (max: %d)", file_size, SD_OTA_MAX_FILE_SIZE);
        if (g_status_callback) {
            g_status_callback("Error: File too large");
        }
        return ESP_ERR_INVALID_SIZE;
    }

    // Initialize OTA state
    memset(&g_sd_ota_state, 0, sizeof(g_sd_ota_state));
    g_sd_ota_state.filename = SD_OTA_FILENAME;
    g_sd_ota_state.file_size = file_size;
    g_sd_ota_state.in_progress = true;

    // Notify UI
    if (g_status_callback) {
        g_status_callback("Flashing firmware...");
    }

    // Start flashing
    ret = sd_ota_flash_file(SD_OTA_FILENAME, ESP_PARTITION_SUBTYPE_APP_OTA_1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to flash ota1.bin: %s", esp_err_to_name(ret));
        g_sd_ota_state.in_progress = false;
        if (g_status_callback) {
            g_status_callback("Error: Flashing failed");
        }
        return ret;
    }

    ESP_LOGI(TAG, "OTA completed successfully");
    g_sd_ota_state.in_progress = false;
    if (g_status_callback) {
        g_status_callback("OTA completed successfully! Restarting...");
    }

    // Automatic restart after 2 seconds
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Restarting system to boot from OTA_1...");
    esp_restart();

    return ESP_OK;
}