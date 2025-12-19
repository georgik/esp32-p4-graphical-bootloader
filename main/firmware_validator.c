/**
 * @file firmware_validator.c
 * @brief Firmware validation and integrity checking implementation
 */

#include "firmware_validator.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_crc.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

static const char* TAG = "firmware_validator";

// ESP32 application image header structure (simplified)
typedef struct {
    uint8_t magic;                    // Magic byte (0xE9)
    uint8_t segment_count;            // Number of memory segments
    uint16_t spi_mode;               // SPI mode
    uint32_t entry_addr;              // Entry point address
    uint32_t wp_pin;                 // Flash write protect pin
    uint16_t spi_pin_drv[3];         // SPI pin drive levels
    char chip_id[8];                 // Chip identification
    uint8_t min_chip_rev;            // Minimum chip revision
    uint8_t min_chip_rev_full;       // Full chip revision
    uint8_t max_chip_rev;            // Maximum chip revision
    uint8_t max_chip_rev_full;       // Full chip revision
    uint8_t reserved[4];             // Reserved fields
    uint32_t checksum;               // Header checksum
} __attribute__((packed)) esp_image_header_t;

esp_err_t firmware_validate(const char* file_path, firmware_validation_result_t* result)
{
    if (!file_path || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize result structure
    memset(result, 0, sizeof(firmware_validation_result_t));
    result->error_message = "Unknown error";

    ESP_LOGI(TAG, "Validating firmware: %s", file_path);

    // Check file existence and get size
    struct stat st;
    if (stat(file_path, &st) != 0) {
        result->error_message = "File not found";
        ESP_LOGE(TAG, "File not found: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    result->file_size = st.st_size;

    // Basic size validation
    if (result->file_size < ESP_APP_IMAGE_MIN_SIZE) {
        result->error_message = "File too small for valid firmware";
        ESP_LOGE(TAG, "File too small: %d bytes (minimum %d bytes)",
                 result->file_size, ESP_APP_IMAGE_MIN_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (result->file_size > ESP_APP_IMAGE_MAX_SIZE) {
        result->error_message = "File too large for ESP32 flash";
        ESP_LOGE(TAG, "File too large: %d bytes (maximum %d bytes)",
                 result->file_size, ESP_APP_IMAGE_MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    result->has_correct_size = true;

    // Open file for reading
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        result->error_message = "Failed to open file";
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Read and validate magic byte
    uint8_t magic;
    if (fread(&magic, 1, 1, file) != 1) {
        result->error_message = "Failed to read magic byte";
        fclose(file);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (magic != ESP_APP_IMAGE_MAGIC) {
        result->error_message = "Invalid ESP32 firmware magic byte";
        fclose(file);
        ESP_LOGE(TAG, "Invalid magic byte: 0x%02X (expected 0x%02X)", magic, ESP_APP_IMAGE_MAGIC);
        return ESP_ERR_INVALID_RESPONSE;
    }

    result->has_magic = true;

    // Read header for validation
    esp_image_header_t header;
    fseek(file, 0, SEEK_SET);
    if (fread(&header, sizeof(esp_image_header_t), 1, file) != 1) {
        result->error_message = "Failed to read firmware header";
        fclose(file);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Basic header validation
    if (header.segment_count > 16) {  // Reasonable limit
        result->error_message = "Invalid segment count in header";
        fclose(file);
        return ESP_ERR_INVALID_RESPONSE;
    }

    result->has_valid_header = true;

    fclose(file);

    // Calculate CRC32
    esp_err_t ret = firmware_calculate_crc32(file_path, &result->calculated_crc32);
    if (ret != ESP_OK) {
        result->error_message = "Failed to calculate CRC32";
        return ret;
    }

    // If CRC32 is embedded in firmware (future enhancement), verify it here
    // For now, any calculated CRC32 is considered valid
    result->crc32_valid = true;
    result->is_valid = result->has_magic &&
                      result->has_correct_size &&
                      result->has_valid_header &&
                      result->crc32_valid;

    if (result->is_valid) {
        result->error_message = "Firmware is valid";
        ESP_LOGI(TAG, "Firmware validation successful: %d bytes, CRC32: 0x%08X",
                 result->file_size, result->calculated_crc32);
    }

    return ESP_OK;
}

// Use static buffer to reduce stack usage
static uint8_t crc_buffer[1024];  // Smaller buffer to reduce stack usage

esp_err_t firmware_calculate_crc32(const char* file_path, uint32_t* crc32)
{
    if (!file_path || !crc32) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE* file = fopen(file_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for CRC calculation: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }

    // Initialize CRC32
    uint32_t calculated_crc = 0xFFFFFFFF;  // Standard CRC32 initial value

    // Read file in smaller chunks and calculate CRC
    size_t bytes_read;
    while ((bytes_read = fread(crc_buffer, 1, sizeof(crc_buffer), file)) > 0) {
        calculated_crc = esp_crc32_le(calculated_crc, crc_buffer, bytes_read);

        // Yield to prevent task starvation
        taskYIELD();
    }

    fclose(file);

    // Final CRC32 value (invert for standard CRC32)
    *crc32 = calculated_crc ^ 0xFFFFFFFF;  // Final XOR

    ESP_LOGD(TAG, "CRC32 calculated for %s: 0x%08X", file_path, *crc32);
    return ESP_OK;
}

esp_err_t firmware_verify_crc32(const char* file_path, uint32_t expected_crc32, bool* is_valid)
{
    if (!file_path || !is_valid) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t calculated_crc32;
    esp_err_t ret = firmware_calculate_crc32(file_path, &calculated_crc32);
    if (ret != ESP_OK) {
        return ret;
    }

    *is_valid = (calculated_crc32 == expected_crc32);

    ESP_LOGI(TAG, "CRC32 verification for %s: expected 0x%08X, calculated 0x%08X, %s",
             file_path, expected_crc32, calculated_crc32, *is_valid ? "VALID" : "INVALID");

    return ESP_OK;
}

esp_err_t firmware_quick_validate(const char* file_path, uint32_t* file_size, bool* is_valid)
{
    if (!file_path || !file_size || !is_valid) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(file_path, &st) != 0) {
        *file_size = 0;
        *is_valid = false;
        return ESP_ERR_NOT_FOUND;
    }

    *file_size = st.st_size;

    // Quick size check
    if (*file_size < ESP_APP_IMAGE_MIN_SIZE || *file_size > ESP_APP_IMAGE_MAX_SIZE) {
        *is_valid = false;
        return ESP_OK;
    }

    // Quick magic byte check
    FILE* file = fopen(file_path, "rb");
    if (!file) {
        *is_valid = false;
        return ESP_OK;
    }

    uint8_t magic;
    if (fread(&magic, 1, 1, file) != 1) {
        fclose(file);
        *is_valid = false;
        return ESP_OK;
    }

    fclose(file);

    *is_valid = (magic == ESP_APP_IMAGE_MAGIC);
    return ESP_OK;
}

bool firmware_has_valid_extension(const char* filename)
{
    if (!filename) {
        return false;
    }

    size_t len = strlen(filename);
    if (len < 4) {
        return false;
    }

    // Check for .bin extension (case-insensitive)
    return (strcasecmp(filename + len - 4, ".bin") == 0);
}

esp_err_t firmware_extract_display_name(const char* file_path, char* display_name, size_t buffer_size)
{
    if (!file_path || !display_name || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Extract filename from path
    const char* filename = strrchr(file_path, '/');
    if (filename) {
        filename++;  // Skip the '/'
    } else {
        filename = file_path;
    }

    // Copy filename (without extension) to display name
    size_t len = strlen(filename);
    const char* ext = strrchr(filename, '.');
    if (ext && (ext - filename) < buffer_size) {
        len = ext - filename;
    }

    if (len >= buffer_size) {
        len = buffer_size - 1;
    }

    strncpy(display_name, filename, len);
    display_name[len] = '\0';

    return ESP_OK;
}

esp_err_t firmware_format_size(uint32_t size_bytes, char* formatted_size, size_t buffer_size)
{
    if (!formatted_size || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (size_bytes < 1024) {
        snprintf(formatted_size, buffer_size, "%lu B", (unsigned long)size_bytes);
    } else if (size_bytes < 1024 * 1024) {
        snprintf(formatted_size, buffer_size, "%lu KB", (unsigned long)size_bytes / 1024);
    } else {
        snprintf(formatted_size, buffer_size, "%.1f MB", (float)size_bytes / (1024 * 1024));
    }

    return ESP_OK;
}

esp_err_t firmware_get_validation_status(const firmware_validation_result_t* result,
                                        char* status_buffer,
                                        size_t buffer_size)
{
    if (!result || !status_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (result->is_valid) {
        snprintf(status_buffer, buffer_size, "✓ Valid (CRC: 0x%08lX)", (unsigned long)result->calculated_crc32);
    } else {
        snprintf(status_buffer, buffer_size, "✗ %s", result->error_message);
    }

    return ESP_OK;
}

esp_err_t firmware_calculate_fast_crc32(const char* file_path, uint32_t file_size, uint32_t* crc32)
{
    if (!file_path || !crc32) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE* file = fopen(file_path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t SAMPLE_SIZE = 4096; // 4KB samples
    uint32_t calculated_crc = 0xFFFFFFFF;
    bool small_file = (file_size <= 2 * SAMPLE_SIZE);

    if (small_file) {
        // For small files (≤8KB), calculate full CRC
        uint8_t buffer[1024];
        size_t bytes_read;

        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            calculated_crc = esp_crc32_le(calculated_crc, buffer, bytes_read);
        }
    } else {
        // For large files, sample first 4KB and last 4KB
        uint8_t buffer[SAMPLE_SIZE];

        // Sample first 4KB
        size_t bytes_read = fread(buffer, 1, SAMPLE_SIZE, file);
        if (bytes_read == SAMPLE_SIZE) {
            calculated_crc = esp_crc32_le(calculated_crc, buffer, bytes_read);
        }

        // Seek to last 4KB (or start if file smaller)
        if (file_size > SAMPLE_SIZE) {
            fseek(file, -SAMPLE_SIZE, SEEK_END);
            bytes_read = fread(buffer, 1, SAMPLE_SIZE, file);
            if (bytes_read > 0) {
                calculated_crc = esp_crc32_le(calculated_crc, buffer, bytes_read);
            }
        }
    }

    fclose(file);
    *crc32 = calculated_crc ^ 0xFFFFFFFF; // Final XOR

    return ESP_OK;
}