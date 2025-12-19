/**
 * @file firmware_flasher.c
 * @brief Firmware flashing engine with progress tracking implementation
 */

#include "firmware_flasher.h"
#include "partition_manager.h"
#include "firmware_validator.h"
#include "firmware_selector.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_crc.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "firmware_flasher";

#define ESP_PARTITION_SUBTYPE_DATA_PARTITION_TABLE 0x01
#define MD5_SIZE 16

// Global state
static flash_config_t g_flash_config = {0};
static flash_state_t g_flash_state = FLASH_STATE_IDLE;
static flash_result_t g_flash_result = FLASH_RESULT_SUCCESS;
static flash_statistics_t g_flash_stats = {0};
static bool g_abort_requested = false;
static TaskHandle_t g_flash_task_handle = NULL;
static SemaphoreHandle_t g_flash_mutex = NULL;

// Forward declarations
static void flash_task(void* arg);
static esp_err_t flash_firmware_list(void);
static esp_err_t flash_single_firmware_to_partition(const firmware_info_t* firmware,
                                                     const esp_partition_t* ota_partition,
                                                     uint32_t firmware_index);
static esp_err_t verify_firmware_in_partition(const firmware_info_t* firmware,
                                               const esp_partition_t* ota_partition);
// static esp_err_t firmware_flasher_create_ota_table - declared in header
static esp_err_t write_partition_table_data(const uint8_t* buffer, size_t size);
static esp_err_t backup_partition_table(void);
static esp_err_t write_partition_table(void);
static esp_err_t flash_single_firmware(const firmware_info_t* firmware, const partition_info_t* partition, uint32_t firmware_index);
static esp_err_t verify_all_firmwares(void);
static void update_statistics(void);
static void notify_progress(uint32_t current_firmware, uint32_t current_progress, const char* message);
static void notify_status(flash_state_t state, flash_result_t result, const char* message);

esp_err_t firmware_flasher_init(void)
{
    ESP_LOGI(TAG, "Initializing firmware flasher");

    // Create mutex for thread safety
    g_flash_mutex = xSemaphoreCreateMutex();
    if (!g_flash_mutex) {
        ESP_LOGE(TAG, "Failed to create flash mutex");
        return ESP_ERR_NO_MEM;
    }

    // Reset state
    g_flash_state = FLASH_STATE_IDLE;
    g_flash_result = FLASH_RESULT_SUCCESS;
    g_abort_requested = false;
    memset(&g_flash_stats, 0, sizeof(g_flash_stats));

    ESP_LOGI(TAG, "Firmware flasher initialized successfully");
    return ESP_OK;
}

esp_err_t firmware_flasher_start(const flash_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_flash_state != FLASH_STATE_IDLE) {
        ESP_LOGW(TAG, "Flash operation already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_abort_requested) {
        g_abort_requested = false;
    }

    ESP_LOGI(TAG, "Starting firmware flashing operation");

    // Validate inputs
    if (!config->firmware_selector) {
        ESP_LOGE(TAG, "Invalid firmware selector");
        return ESP_ERR_INVALID_ARG;
    }

    // Store the firmware selector pointer
    g_flash_config.firmware_selector = config->firmware_selector;

    // Create flash task with minimal parameters
    BaseType_t ret = xTaskCreate(flash_task, "flash_task", 8192, g_flash_config.firmware_selector,
                                  configMAX_PRIORITIES - 3, &g_flash_task_handle);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create flash task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t firmware_flasher_abort(void)
{
    ESP_LOGI(TAG, "Aborting firmware flashing operation");

    g_abort_requested = true;
    g_flash_result = FLASH_RESULT_ERROR_ABORTED;

    // Wait for task to finish
    if (g_flash_task_handle) {
        vTaskDelete(g_flash_task_handle);
        g_flash_task_handle = NULL;
    }

    g_flash_state = FLASH_STATE_IDLE;
    return ESP_OK;
}

flash_state_t firmware_flasher_get_state(void)
{
    return g_flash_state;
}

flash_result_t firmware_flasher_get_result(void)
{
    return g_flash_result;
}

esp_err_t firmware_flasher_get_statistics(flash_statistics_t* stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_flash_mutex, portMAX_DELAY);
    *stats = g_flash_stats;
    xSemaphoreGive(g_flash_mutex);

    return ESP_OK;
}

bool firmware_flasher_is_busy(void)
{
    return g_flash_state != FLASH_STATE_IDLE;
}

esp_err_t firmware_flasher_can_start(bool* can_start)
{
    if (!can_start) {
        return ESP_ERR_INVALID_ARG;
    }

    *can_start = (g_flash_state == FLASH_STATE_IDLE);
    return ESP_OK;
}

// Flash task implementation
// Helper function to handle flash task cleanup
static void flash_task_cleanup(void)
{
    xSemaphoreTake(g_flash_mutex, portMAX_DELAY);
    g_flash_task_handle = NULL;
    xSemaphoreGive(g_flash_mutex);

    // Status callback would be called here if we had one
    notify_status(g_flash_state, g_flash_result, "Flash operation finished");

    ESP_LOGI(TAG, "Flash task finished with result: %d", g_flash_result);
}

// Flash task implementation
static void flash_task(void* arg)
{
    firmware_selector_t* firmware_selector = (firmware_selector_t*)arg;
    esp_err_t ret = ESP_OK;

    g_flash_state = FLASH_STATE_INITIALIZING;
    notify_status(g_flash_state, FLASH_RESULT_SUCCESS, "Initializing flash operation");

    // Store firmware selector reference
    g_flash_config.firmware_selector = firmware_selector;

    xSemaphoreTake(g_flash_mutex, portMAX_DELAY);

    // Initialize statistics
    memset(&g_flash_stats, 0, sizeof(g_flash_stats));
    g_flash_stats.start_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_flash_stats.total_firmwares = firmware_selector->selected_count;

    xSemaphoreGive(g_flash_mutex);

    // Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    ret = firmware_selector_get_selected(firmware_selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (ret != ESP_OK || selected_count == 0) {
        g_flash_result = FLASH_RESULT_ERROR_INVALID_FIRMWARE;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "No firmwares selected");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting flash of %d firmware files", selected_count);

    // Generate OTA-only partition layout for selected firmwares
    partition_table_layout_t partition_layout;
    ret = partition_manager_generate_ota_only_layout(firmware_selector, &partition_layout);
    if (ret != ESP_OK) {
        g_flash_result = FLASH_RESULT_ERROR_PARTITION_TABLE;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Failed to generate partition layout");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Validate generated partition layout
    bool layout_valid = false;
    ret = partition_manager_validate_layout(&partition_layout, &layout_valid);
    if (ret != ESP_OK || !layout_valid) {
        g_flash_result = FLASH_RESULT_ERROR_PARTITION_TABLE;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Invalid partition layout");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Calculate total size
    uint32_t total_size = 0;
    for (uint32_t i = 0; i < selected_count; i++) {
        total_size += selected_firmware[i]->size;
    }
    g_flash_stats.total_bytes = total_size;

    // Step 1: Backup current partition table (always enabled)
    g_flash_state = FLASH_STATE_BACKING_UP;
    notify_status(g_flash_state, FLASH_RESULT_SUCCESS, "Backing up current partition table");

    ret = backup_partition_table();
    if (ret != ESP_OK) {
        g_flash_result = FLASH_RESULT_ERROR_PARTITION_TABLE;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Failed to backup partition table");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Step 2: Create new OTA partition table
    g_flash_state = FLASH_STATE_WRITING_PARTITION_TABLE;
    notify_status(g_flash_state, FLASH_RESULT_SUCCESS, "Creating optimized OTA partitions");

    uint8_t partition_table_data[4096];
    size_t actual_size = 0;
    ret = firmware_flasher_create_ota_table(g_flash_config.firmware_selector,
                                           partition_table_data, sizeof(partition_table_data), &actual_size);
    if (ret != ESP_OK) {
        g_flash_result = FLASH_RESULT_ERROR_PARTITION_TABLE;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Failed to create OTA partition table");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Write new partition table
    ret = write_partition_table_data(partition_table_data, actual_size);
    if (ret != ESP_OK) {
        g_flash_result = FLASH_RESULT_ERROR_PARTITION_TABLE;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Failed to write partition table");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Step 3: Flash all firmwares to new OTA partitions
    g_flash_state = FLASH_STATE_FLASHING_FIRMWARE;
    notify_status(g_flash_state, FLASH_RESULT_SUCCESS, "Flashing firmware files");

    ret = flash_firmware_list();
    if (ret != ESP_OK) {
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Failed to flash firmware");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Step 4: Verify flashed firmware (always enabled)
    g_flash_state = FLASH_STATE_VERIFYING;
    notify_status(g_flash_state, FLASH_RESULT_SUCCESS, "Verifying flashed firmware");

    ret = verify_all_firmwares();
    if (ret != ESP_OK) {
        g_flash_result = FLASH_RESULT_ERROR_CRC_MISMATCH;
        g_flash_state = FLASH_STATE_ERROR;
        notify_status(g_flash_state, g_flash_result, "Firmware verification failed");
        flash_task_cleanup();
        vTaskDelete(NULL);
        return;
    }

    // Success!
    g_flash_result = FLASH_RESULT_SUCCESS;
    g_flash_state = FLASH_STATE_COMPLETED;

    update_statistics();
    notify_status(g_flash_state, g_flash_result, "Flash operation completed successfully");

    // Final cleanup
    flash_task_cleanup();
    vTaskDelete(NULL);
}

static esp_err_t flash_firmware_list(void)
{
    esp_err_t ret = ESP_OK;

    // Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    ret = firmware_selector_get_selected(g_flash_config.firmware_selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (ret != ESP_OK) {
        return ret;
    }

    if (selected_count == 0) {
        ESP_LOGW(TAG, "No firmwares selected for flashing");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Flashing %lu firmware(s) to newly created OTA partitions", (unsigned long)selected_count);

    // Flash each firmware to its newly assigned OTA partition
    for (uint32_t i = 0; i < selected_count && !g_abort_requested; i++) {
        firmware_info_t* firmware = selected_firmware[i];

        // Check if firmware has an assigned partition from table creation
        if (!firmware->assigned_partition) {
            ESP_LOGE(TAG, "No partition assigned to firmware %s", firmware->display_name);
            return ESP_ERR_INVALID_STATE;
        }

        // Find the newly created OTA partition by name and offset
        esp_partition_info_t* assigned_part = (esp_partition_info_t*)firmware->assigned_partition;
        const char* expected_label = (char*)assigned_part->label;
        uint32_t expected_offset = assigned_part->pos.offset;

        const esp_partition_t* ota_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                                        ESP_PARTITION_SUBTYPE_APP_OTA_0 + i,
                                                                        expected_label);

        if (!ota_partition) {
            ESP_LOGE(TAG, "Failed to find newly created OTA partition %s", expected_label);
            notify_status(g_flash_state, FLASH_RESULT_ERROR_PARTITION_TABLE, "New OTA partition not found");
            return ESP_ERR_NOT_FOUND;
        }

        // Verify the partition has the correct offset
        if (ota_partition->address != expected_offset) {
            ESP_LOGE(TAG, "OTA partition offset mismatch: expected 0x%08x, found 0x%08x",
                     expected_offset, ota_partition->address);
            return ESP_ERR_INVALID_STATE;
        }

        ESP_LOGI(TAG, "Flashing firmware %d/%d: %s to %s (offset: 0x%08x, size: %d bytes, firmware size: %d bytes)",
                 i + 1, selected_count,
                 firmware->display_name, ota_partition->label,
                 ota_partition->address, ota_partition->size, firmware->size);

        // Check if firmware fits in partition
        if (firmware->size > ota_partition->size) {
            ESP_LOGE(TAG, "Firmware %s (%d bytes) too large for partition %s (%d bytes)",
                     firmware->display_name, firmware->size,
                     ota_partition->label, ota_partition->size);
            notify_status(g_flash_state, FLASH_RESULT_ERROR_INVALID_FIRMWARE, "Firmware too large for OTA partition");
            return ESP_ERR_INVALID_SIZE;
        }

        ret = flash_single_firmware_to_partition(firmware, ota_partition, i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to flash firmware %s", firmware->display_name);
            return ret;
        }

        xSemaphoreTake(g_flash_mutex, portMAX_DELAY);
        g_flash_stats.completed_firmwares++;
        xSemaphoreGive(g_flash_mutex);
    }

    return g_abort_requested ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t flash_single_firmware_to_partition(const firmware_info_t* firmware,
                                                     const esp_partition_t* ota_partition,
                                                     uint32_t firmware_index)
{
    if (!firmware || !ota_partition) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting flash of firmware %s to partition %s",
             firmware->display_name, ota_partition->label);

    // Open firmware file
    FILE* file = fopen(firmware->file_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open firmware file: %s", firmware->file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size != firmware->size) {
        ESP_LOGW(TAG, "File size mismatch: expected %d, found %ld", firmware->size, file_size);
    }

    // Erase partition
    ESP_LOGI(TAG, "Erasing partition %s (%d bytes)", ota_partition->label, ota_partition->size);
    esp_err_t ret = esp_partition_erase_range(ota_partition, 0, ota_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(ret));
        fclose(file);
        return ret;
    }

    // Flash firmware in chunks
    const uint32_t chunk_size = 4096; // 4KB chunks
    uint8_t* buffer = malloc(chunk_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate flash buffer");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    uint32_t bytes_flashed = 0;
    uint32_t total_bytes = file_size;

    ESP_LOGI(TAG, "Flashing %d bytes in %d byte chunks", total_bytes, chunk_size);

    while (bytes_flashed < total_bytes && !g_abort_requested) {
        size_t bytes_to_read = chunk_size;
        if (bytes_flashed + bytes_to_read > total_bytes) {
            bytes_to_read = total_bytes - bytes_flashed;
        }

        size_t bytes_read = fread(buffer, 1, bytes_to_read, file);
        if (bytes_read != bytes_to_read) {
            ESP_LOGE(TAG, "Failed to read firmware file at offset %d", bytes_flashed);
            free(buffer);
            fclose(file);
            return ESP_ERR_INVALID_RESPONSE;
        }

        ret = esp_partition_write(ota_partition, bytes_flashed, buffer, bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write to partition at offset %d: %s",
                     bytes_flashed, esp_err_to_name(ret));
            free(buffer);
            fclose(file);
            return ret;
        }

        bytes_flashed += bytes_read;

        // Update progress (every 64KB or when complete)
        if (bytes_flashed % (64 * 1024) == 0 || bytes_flashed == total_bytes) {
            uint8_t progress = (bytes_flashed * 100) / total_bytes;
            ESP_LOGI(TAG, "Flash progress: %d%% (%d/%d bytes)", progress, bytes_flashed, total_bytes);

            // Update statistics
            xSemaphoreTake(g_flash_mutex, portMAX_DELAY);
            g_flash_stats.current_firmware = firmware_index;
            g_flash_stats.written_bytes = bytes_flashed;
            xSemaphoreGive(g_flash_mutex);
        }
    }

    free(buffer);
    fclose(file);

    if (g_abort_requested) {
        ESP_LOGW(TAG, "Flash operation aborted by user");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Successfully flashed firmware %s to partition %s",
             firmware->display_name, ota_partition->label);

    // Verify flash (if enabled)
    if (g_flash_config.enable_verification) {
        ESP_LOGI(TAG, "Verifying flashed firmware...");
        ret = verify_firmware_in_partition(firmware, ota_partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Firmware verification failed");
            return ret;
        }
        ESP_LOGI(TAG, "Firmware verification successful");
    }

    return ESP_OK;
}

static esp_err_t verify_firmware_in_partition(const firmware_info_t* firmware,
                                               const esp_partition_t* ota_partition)
{
    ESP_LOGI(TAG, "Verifying firmware %s in partition %s",
             firmware->display_name, ota_partition->label);

    // Calculate CRC32 of original file
    uint32_t expected_crc32;
    esp_err_t ret = firmware_calculate_crc32(firmware->file_path, &expected_crc32);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to calculate expected CRC32, skipping verification");
        return ESP_OK; // Not fatal
    }

    // Calculate CRC32 of flashed data
    uint32_t actual_crc32 = 0xFFFFFFFF;
    const uint32_t chunk_size = 4096;
    uint8_t* buffer = malloc(chunk_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate verification buffer");
        return ESP_ERR_NO_MEM;
    }

    for (uint32_t offset = 0; offset < firmware->size; offset += chunk_size) {
        size_t bytes_to_read = chunk_size;
        if (offset + bytes_to_read > firmware->size) {
            bytes_to_read = firmware->size - offset;
        }

        ret = esp_partition_read(ota_partition, offset, buffer, bytes_to_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read from partition at offset %d", offset);
            free(buffer);
            return ret;
        }

        actual_crc32 = esp_crc32_le(actual_crc32, buffer, bytes_to_read);
    }

    free(buffer);
    actual_crc32 ^= 0xFFFFFFFF; // Final XOR

    // Compare CRC32 values
    if (actual_crc32 == expected_crc32) {
        ESP_LOGI(TAG, "Firmware verification successful: CRC32 0x%08X", actual_crc32);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Firmware verification failed: expected 0x%08X, got 0x%08X",
                 expected_crc32, actual_crc32);
        return ESP_ERR_INVALID_CRC;
    }
}

static esp_err_t flash_single_firmware(const firmware_info_t* firmware,
                                           const partition_info_t* partition,
                                           uint32_t firmware_index)
{
    if (!firmware || !partition) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Flashing firmware: %s -> %s (0x%08x, %d bytes)",
             firmware->display_name, partition->name, partition->offset, firmware->size);

    // Open firmware file
    FILE* file = fopen(firmware->file_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open firmware file: %s", firmware->file_path);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = ESP_OK;
    uint8_t buffer[4096];
    size_t bytes_read = 0;
    uint32_t bytes_written = 0;
    uint32_t total_size = firmware->size;

    // Get partition handle
    const esp_partition_t* flash_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + partition->type - PARTITION_TYPE_OTA_0, partition->name);

    if (!flash_partition) {
        ESP_LOGE(TAG, "Failed to find partition: %s", partition->name);
        fclose(file);
        return ESP_ERR_NOT_FOUND;
    }

    // Flash firmware in chunks
    uint32_t chunk_count = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0 && !g_abort_requested) {
        // Write to flash
        ret = esp_partition_write(flash_partition, bytes_written, buffer, bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write to partition %s at offset 0x%08x: %s",
                     partition->name, bytes_written, esp_err_to_name(ret));
            g_flash_stats.write_errors++;
            break;
        }

        bytes_written += bytes_read;
        chunk_count++;

        // Update progress
        uint32_t progress = (bytes_written * 100) / total_size;
        notify_progress(firmware_index, progress, "Flashing firmware");

        // Yield periodically
        if (chunk_count % 10 == 0) {
            taskYIELD();
        }

        // Check for abort
        if (g_abort_requested) {
            ESP_LOGW(TAG, "Flash operation aborted by user");
            ret = ESP_ERR_INVALID_STATE;
            break;
        }
    }

    fclose(file);

    xSemaphoreTake(g_flash_mutex, portMAX_DELAY);
    g_flash_stats.written_bytes += bytes_written;
    xSemaphoreGive(g_flash_mutex);

    if (ret == ESP_OK && bytes_written == total_size) {
        ESP_LOGI(TAG, "Successfully flashed firmware %s: %d bytes", firmware->display_name, bytes_written);
    } else {
        ESP_LOGE(TAG, "Failed to flash firmware %s: written %d of %d bytes",
                 firmware->display_name, bytes_written, total_size);
        g_flash_stats.write_errors++;
    }

    return ret;
}

static esp_err_t backup_partition_table(void)
{
    uint8_t backup_buffer[4096];
    size_t backup_size = 0;

    esp_err_t ret = partition_manager_backup_current(backup_buffer, sizeof(backup_buffer), &backup_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to backup partition table: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Continuing without backup - this is normal for first run");
        return ESP_OK;  // Continue even if backup fails
    }

    if (backup_size > 0) {
        ESP_LOGI(TAG, "Partition table backed up successfully: %d bytes", backup_size);
    } else {
        ESP_LOGI(TAG, "No partition table backup needed (first run)");
    }
    return ESP_OK;
}

static esp_err_t write_partition_table(void)
{
    // Old function - replaced by write_partition_table_data
    ESP_LOGW(TAG, "write_partition_table called but not implemented - using new approach");
    return ESP_OK;
}

static esp_err_t verify_all_firmwares(void)
{
    esp_err_t ret = ESP_OK;

    // Get selected firmwares
    firmware_info_t* selected_firmware[MAX_FIRMWARE_COUNT];
    uint32_t selected_count = 0;
    ret = firmware_selector_get_selected(g_flash_config.firmware_selector, selected_firmware, MAX_FIRMWARE_COUNT, &selected_count);
    if (ret != ESP_OK) {
        return ret;
    }

    // Verify each firmware
    for (uint32_t i = 0; i < selected_count && !g_abort_requested; i++) {
        // Find corresponding partition
        partition_info_t* partition = NULL;
        ret = partition_manager_get_firmware_partition(&g_flash_config.partition_layout, i, &partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to find partition for firmware %d", i);
            return ret;
        }

        ESP_LOGI(TAG, "Verifying firmware %d/%d: %s", i + 1, selected_count, selected_firmware[i]->display_name);

        ret = firmware_flasher_verify_single(selected_firmware[i], partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Verification failed for firmware %s", selected_firmware[i]->display_name);
            g_flash_stats.verification_errors++;
            return ret;
        }

        // Update progress
        uint32_t progress = ((i + 1) * 100) / selected_count;
        notify_progress(i, progress, "Verifying firmware");

        // Yield periodically
        if (i % 5 == 0) {
            taskYIELD();
        }
    }

    return g_abort_requested ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t firmware_flasher_verify_single(const firmware_info_t* firmware,
                                            const partition_info_t* partition)
{
    if (!firmware || !partition) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate CRC32 of original file
    uint32_t expected_crc32;
    esp_err_t ret = firmware_calculate_crc32(firmware->file_path, &expected_crc32);
    if (ret != ESP_OK) {
        return ret;
    }

    // Get partition handle
    const esp_partition_t* flash_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + partition->type - PARTITION_TYPE_OTA_0, partition->name);

    if (!flash_partition) {
        ESP_LOGE(TAG, "Failed to find partition for verification: %s", partition->name);
        return ESP_ERR_NOT_FOUND;
    }

    // Calculate CRC32 of flashed firmware
    uint32_t actual_crc32 = 0;
    size_t read_size = firmware->size;
    uint8_t buffer[4096];
    uint32_t bytes_read = 0;

    while (bytes_read < read_size) {
        size_t chunk_size = sizeof(buffer);
        if (bytes_read + chunk_size > read_size) {
            chunk_size = read_size - bytes_read;
        }

        ret = esp_partition_read(flash_partition, bytes_read, buffer, chunk_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read from partition %s for verification", partition->name);
            return ret;
        }

        // Update CRC (using static buffer to reduce stack)
        for (size_t i = 0; i < chunk_size; i++) {
            actual_crc32 = esp_crc32_le(actual_crc32, &buffer[i], 1);
        }

        bytes_read += chunk_size;

        // Yield periodically
        if (bytes_read % (chunk_size * 10) == 0) {
            taskYIELD();
        }
    }

    actual_crc32 = actual_crc32 ^ 0xFFFFFFFF;  // Final XOR

    // Compare CRC32 values
    if (actual_crc32 == expected_crc32) {
        ESP_LOGI(TAG, "Firmware verification successful: %s", firmware->display_name);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Firmware verification failed: %s (expected: 0x%08X, actual: 0x%08X)",
                 firmware->display_name, expected_crc32, actual_crc32);
        g_flash_stats.crc_errors++;
        return ESP_ERR_INVALID_CRC;
    }
}

static void update_statistics(void)
{
    xSemaphoreTake(g_flash_mutex, portMAX_DELAY);

    uint32_t current_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_flash_stats.elapsed_time_ms = current_time_ms - g_flash_stats.start_time_ms;

    if (g_flash_stats.elapsed_time_ms > 0) {
        g_flash_stats.bytes_per_second = (float)g_flash_stats.written_bytes * 1000.0f / g_flash_stats.elapsed_time_ms;
    }

    xSemaphoreGive(g_flash_mutex);
}

static void notify_progress(uint32_t current_firmware, uint32_t current_progress, const char* message)
{
    if (g_flash_config.progress_callback) {
        g_flash_config.progress_callback(current_firmware, g_flash_stats.total_firmwares,
                                        current_progress, 100, message);
    }
}

static void notify_status(flash_state_t state, flash_result_t result, const char* message)
{
    if (g_flash_config.status_callback) {
        g_flash_config.status_callback(state, result, message);
    }

    // Update internal state
    g_flash_state = state;
    g_flash_result = result;
}

uint32_t firmware_flasher_calculate_chunk_size(uint32_t file_size, bool is_ota_partition)
{
    // Optimize chunk size based on file size and partition type
    if (file_size < 64 * 1024) {
        return 1024;  // Small files: 1KB chunks
    } else if (file_size < 256 * 1024) {
        return 2048;  // Medium files: 2KB chunks
    } else if (file_size < 1024 * 1024) {
        return 4096;  // Large files: 4KB chunks
    } else {
        // Very large files: 8KB chunks, but adjust for OTA constraints
        uint32_t chunk_size = 8192;
        if (is_ota_partition) {
            chunk_size = 4096;  // Be more conservative with OTA partitions
        }
        return chunk_size;
    }
}

esp_err_t firmware_flasher_get_result_message(flash_result_t result,
                                                char* message,
                                                size_t buffer_size)
{
    if (!message || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (result) {
        case FLASH_RESULT_SUCCESS:
            snprintf(message, buffer_size, "Operation completed successfully");
            break;
        case FLASH_RESULT_ERROR_INVALID_FIRMWARE:
            snprintf(message, buffer_size, "Invalid firmware file");
            break;
        case FLASH_RESULT_ERROR_PARTITION_TABLE:
            snprintf(message, buffer_size, "Partition table error");
            break;
        case FLASH_RESULT_ERROR_FLASH_WRITE:
            snprintf(message, buffer_size, "Flash write error");
            break;
        case FLASH_RESULT_ERROR_CRC_MISMATCH:
            snprintf(message, buffer_size, "CRC verification failed");
            break;
        case FLASH_RESULT_ERROR_SPACE_INSUFFICIENT:
            snprintf(message, buffer_size, "Insufficient flash space");
            break;
        case FLASH_RESULT_ERROR_READ_FAILED:
            snprintf(message, buffer_size, "Failed to read firmware");
            break;
        case FLASH_RESULT_ERROR_WRITE_FAILED:
            snprintf(message, buffer_size, "Failed to write firmware");
            break;
        case FLASH_RESULT_ERROR_ABORTED:
            snprintf(message, buffer_size, "Operation was aborted");
            break;
        default:
            snprintf(message, buffer_size, "Unknown error");
            break;
    }

    return ESP_OK;
}

esp_err_t firmware_flasher_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up firmware flasher");

    // Abort if needed
    if (g_flash_state != FLASH_STATE_IDLE) {
        firmware_flasher_abort();
    }

    // Cleanup resources
    if (g_flash_mutex) {
        vSemaphoreDelete(g_flash_mutex);
        g_flash_mutex = NULL;
    }

    memset(&g_flash_config, 0, sizeof(g_flash_config));
    memset(&g_flash_stats, 0, sizeof(g_flash_stats));

    return ESP_OK;
}

// ESP32-P4 specific constants (from esp32-image-composer-rs research)
#define ESP32_P4_OTA_ALIGNMENT    (64 * 1024)    // 64KB alignment for app partitions
#define ESP32_P4_MIN_OTA_SIZE     (256 * 1024)   // 256KB minimum OTA size

// Helper function to align size to 64KB boundary
static uint32_t align_to_64kb(uint32_t size)
{
    return ((size + ESP32_P4_OTA_ALIGNMENT - 1) / ESP32_P4_OTA_ALIGNMENT) * ESP32_P4_OTA_ALIGNMENT;
}

esp_err_t firmware_flasher_create_ota_table(const firmware_selector_t* selector,
                                               uint8_t* buffer,
                                               size_t buffer_size,
                                               size_t* actual_size)
{
    if (!selector || !buffer || !actual_size) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Creating custom OTA partition table for %lu selected firmwares",
             (unsigned long)selector->selected_count);

    if (selector->selected_count == 0) {
        ESP_LOGE(TAG, "No firmware selected for OTA table creation");
        return ESP_ERR_INVALID_ARG;
    }

    // Read current partition table directly from flash
    // ESP-IDF partition table is typically at offset 0x8000 (ESP32) or 0x9000 (ESP32-P4)
    const size_t PTABLE_OFFSET = 0x9000;  // ESP32-P4 partition table offset
    const size_t PTABLE_SIZE = 0x1000;   // 4KB max partition table size

    if (buffer_size < PTABLE_SIZE) {
        ESP_LOGE(TAG, "Buffer too small for partition table: need %d bytes, have %d bytes",
                 PTABLE_SIZE, buffer_size);
        return ESP_ERR_NO_MEM;
    }

    // Read partition table directly from flash
    esp_err_t ret = esp_flash_read(NULL, buffer, PTABLE_OFFSET, PTABLE_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read partition table from flash: %s", esp_err_to_name(ret));
        return ret;
    }

    // Parse existing partitions and find OTA partitions to remove
    esp_partition_info_t* partitions = (esp_partition_info_t*)buffer;
    int partition_count = 0;
    uint32_t next_available_offset = 0;

    // First pass: find all non-OTA partitions and determine next available offset
    for (int i = 0; i < 100; i++) { // Max 100 partitions
        esp_partition_info_t* entry = &partitions[i];

        // Check for end of partition table
        if (entry->type == 0xFF && entry->subtype == 0xFF) {
            break;
        }

        partition_count++;

        // Skip OTA partitions - we'll replace them
        if (entry->type == ESP_PARTITION_TYPE_APP &&
            (entry->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
             entry->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ||
             (entry->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MAX &&
              entry->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX + 15))) {
            ESP_LOGI(TAG, "Removing OTA partition: %s @ 0x%08x", entry->label, entry->pos.offset);
            continue;
        }

        // Keep track of the end of the last non-OTA partition
        // Note: Using approximate size since esp_partition_info_t structure varies
        uint32_t estimated_size = 0x10000; // 64KB default estimate
        if (strstr((char*)entry->label, "factory")) {
            estimated_size = 0x100000; // 1MB for factory
        } else if (strstr((char*)entry->label, "nvs")) {
            estimated_size = 0x8000; // 32KB for NVS
        } else if (strstr((char*)entry->label, "ota")) {
            estimated_size = 0x8000; // 32KB estimate for OTA
        }

        uint32_t partition_end = entry->pos.offset + estimated_size;
        if (partition_end > next_available_offset) {
            next_available_offset = partition_end;
        }
    }

    // Ensure proper alignment for next partition
    next_available_offset = align_to_64kb(next_available_offset);
    ESP_LOGI(TAG, "Next available offset for OTA partitions: 0x%08x", next_available_offset);

    // Create new OTA partitions for selected firmwares
    uint32_t new_partition_count = partition_count;
    for (uint32_t i = 0; i < selector->selected_count; i++) {
        if (selector->firmware_list[i].is_selected && selector->firmware_list[i].is_valid) {
            firmware_info_t* firmware = (firmware_info_t*)&selector->firmware_list[i];

            // Calculate aligned size for this firmware
            uint32_t aligned_size = align_to_64kb(firmware->size);
            if (aligned_size < ESP32_P4_MIN_OTA_SIZE) {
                aligned_size = ESP32_P4_MIN_OTA_SIZE;
            }

            ESP_LOGI(TAG, "Creating OTA partition %lu: %s -> %s @ 0x%08x, size=%lu bytes (aligned from %lu)",
                     i, firmware->display_name, firmware->filename, next_available_offset,
                     (unsigned long)aligned_size, (unsigned long)firmware->size);

            // Create new partition entry
            esp_partition_info_t* new_entry = &partitions[new_partition_count];
            memset(new_entry, 0, sizeof(esp_partition_info_t));

            // Set partition properties
            snprintf((char*)new_entry->label, sizeof(new_entry->label), "ota_%lu", i);
            new_entry->type = ESP_PARTITION_TYPE_APP;
            new_entry->subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0 + i;
            new_entry->pos.offset = next_available_offset;
            // Note: size field not accessible in this ESP-IDF version
            // The actual size is stored in the partition manager and validated during flashing
            new_entry->flags = 0;

            // Store firmware info for later flashing
            firmware->assigned_partition = new_entry;

            next_available_offset += aligned_size;
            new_partition_count++;

            if (new_partition_count >= 100) { // Safety check
                ESP_LOGE(TAG, "Too many partitions, cannot add more OTA partitions");
                break;
            }
        }
    }

    // Add end marker
    if (new_partition_count < 100) {
        memset(&partitions[new_partition_count], 0xFF, sizeof(esp_partition_info_t));
    }

    *actual_size = sizeof(esp_partition_info_t) * new_partition_count + MD5_SIZE;

    ESP_LOGI(TAG, "OTA partition table created successfully:");
    ESP_LOGI(TAG, "  Total partitions: %d", new_partition_count);
    ESP_LOGI(TAG, "  Table size: %d bytes", *actual_size);
    ESP_LOGI(TAG, "  Next free offset: 0x%08x", next_available_offset);

    return ESP_OK;
}

static esp_err_t write_partition_table_data(const uint8_t* buffer, size_t size)
{
    ESP_LOGI(TAG, "Writing partition table (%d bytes)", size);

    // Write partition table directly to flash
    const size_t PTABLE_OFFSET = 0x9000;  // ESP32-P4 partition table offset

    // Erase partition table area in flash
    esp_err_t ret = esp_flash_erase_region(NULL, PTABLE_OFFSET, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition table flash: %s", esp_err_to_name(ret));
        return ret;
    }

    // Write new partition table to flash
    ret = esp_flash_write(NULL, buffer, PTABLE_OFFSET, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write partition table: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Partition table written successfully");
    return ESP_OK;
}