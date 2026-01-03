/**
 * @file cli_inspector.c
 * @brief Flash image inspection utilities implementation
 */

#ifdef __SIMULATOR_BUILD__

#include "cli_inspector.h"
#include "esp_log_mock.h"
#include "flash_emulator.h"
#include "partition_table.h"
#include "firmware_storage_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char* TAG = "cli_inspector";

// ESP32 partition table definitions
#define PARTITION_TABLE_OFFSET  0x10000
#define BOOTLOADER_OFFSET       0x2000
#define FACTORY_APP_OFFSET      0x20000
// FIRMWARE_STORAGE_OFFSET is now from firmware_storage_config.h

typedef struct __attribute__((packed)) {
    char     magic[4];          // 'FWST'
    uint32_t version;
    uint32_t count;
    uint32_t header_size;
    uint8_t  reserved[16];
} firmware_storage_header_t;

typedef struct __attribute__((packed)) {
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint32_t flags;
    char     name[64];
    uint8_t  reserved[12];
    uint32_t next_offset;
} firmware_storage_entry_t;

static const char* part_type_to_string(uint8_t type) {
    switch (type) {
        case PART_TYPE_APP:  return "APP";
        case PART_TYPE_DATA: return "DATA";
        default:             return "UNKNOWN";
    }
}

static const char* part_subtype_to_string(uint8_t type, uint8_t subtype) {
    if (type == PART_TYPE_APP) {
        switch (subtype) {
            case PART_SUBTYPE_FACTORY:  return "Factory";
            case PART_SUBTYPE_OTA_0:    return "OTA_0";
            case PART_SUBTYPE_OTA_1:    return "OTA_1";
            case PART_SUBTYPE_OTA_2:    return "OTA_2";
            case PART_SUBTYPE_OTA_3:    return "OTA_3";
            default:                    return "OTA Unknown";
        }
    } else if (type == PART_TYPE_DATA) {
        switch (subtype) {
            case PART_SUBTYPE_NVS:      return "NVS";
            case PART_SUBTYPE_PHY:      return "PHY";
            case PART_SUBTYPE_CUSTOM:   return "Custom";
            default:                    return "Data Unknown";
        }
    }
    return "Unknown";
}

static int print_partition_table(FILE* fp) {
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Partition Table (offset 0x%x)\n", PARTITION_TABLE_OFFSET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // ESP-IDF uses simple partition table without separate header
    // Each entry starts with magic 0xAA50, entries are 32 bytes each
    // Maximum 95 entries (MD5 format is 96 entries but last is for MD5)

    printf("%-12s %-20s %-10s %-10s %-12s %-12s\n",
           "Type", "Subtype", "Offset", "Size", "End", "Name");
    printf("────────────────────────────────────────────────────────────────\n");

    int entry_count = 0;
    for (int i = 0; i < 95; i++) {
        partition_entry_t entry;
        fseek(fp, PARTITION_TABLE_OFFSET + (i * sizeof(entry)), SEEK_SET);
        size_t read = fread(&entry, sizeof(entry), 1, fp);
        if (read != 1) {
            ESP_LOGE(TAG, "Failed to read partition entry %d", i);
            continue;
        }

        // Check magic (little-endian 0xAA50)
        if (entry.magic != 0x50AA) {
            break;  // End of partition table
        }

        const char* type_str = part_type_to_string(entry.type);
        const char* subtype_str = part_subtype_to_string(entry.type, entry.subtype);

        printf("%-12s %-20s 0x%08x %10lu 0x%08x %-12s\n",
               type_str, subtype_str, entry.offset, entry.size,
               entry.offset + entry.size, entry.name);

        entry_count++;
    }

    printf("\nTotal entries: %d\n", entry_count);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return 0;
}

static int print_firmware_storage(FILE* fp) {
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Firmware Storage\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // Scan for firmware storage header by checking common locations
    // Include the fixed offset from firmware_storage_config.h
    uint32_t search_offsets[] = {FIRMWARE_STORAGE_OFFSET, 0xBF0000, 0xEF0000, 0x130000, 0x110000};
    int firmware_storage_found = 0;
    uint32_t firmware_storage_offset = 0;

    for (int i = 0; i < 5; i++) {
        firmware_storage_header_t header;
        fseek(fp, search_offsets[i], SEEK_SET);
        size_t read = fread(&header, sizeof(header), 1, fp);
        if (read != 1) {
            continue;
        }

        // Check magic
        if (memcmp(header.magic, "FWST", 4) == 0) {
            firmware_storage_found = 1;
            firmware_storage_offset = search_offsets[i];
            break;
        }
    }

    if (!firmware_storage_found) {
        ESP_LOGW(TAG, "No firmware storage found");
        printf("Status:      NOT FOUND\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        return 0;
    }

    printf("Offset:      0x%08x\n", firmware_storage_offset);

    // Read firmware storage header
    firmware_storage_header_t header;
    fseek(fp, firmware_storage_offset, SEEK_SET);
    size_t read = fread(&header, sizeof(header), 1, fp);
    if (read != 1) {
        ESP_LOGE(TAG, "Failed to read firmware storage header");
        return -1;
    }

    printf("Magic:       %.4s\n", header.magic);
    printf("Version:     %d\n", header.version);
    printf("Count:       %d firmwares\n", header.count);
    printf("Header Size: %d bytes\n", header.header_size);
    printf("\n");

    if (header.count == 0) {
        printf("No firmware entries.\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        return 0;
    }

    // Read firmware entries
    printf("%-35s %-12s %-12s %-10s %-10s\n",
           "Name", "Offset", "Next Offset", "Size", "CRC32");
    printf("────────────────────────────────────────────────────────────────────\n");

    for (uint32_t i = 0; i < header.count; i++) {
        firmware_storage_entry_t entry;
        uint32_t entry_offset = firmware_storage_offset + header.header_size + (i * sizeof(entry));

        fseek(fp, entry_offset, SEEK_SET);
        read = fread(&entry, sizeof(entry), 1, fp);
        if (read != 1) {
            ESP_LOGE(TAG, "Failed to read firmware entry %d", i);
            continue;
        }

        double size_mb = entry.size / (1024.0 * 1024.0);

        printf("%-35s 0x%08x  0x%08x  %8.2f MB 0x%08x\n",
               entry.name, entry.offset, entry.next_offset, size_mb, entry.crc32);
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return 0;
}

static int print_bootloader_info(FILE* fp) {
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Bootloader (offset 0x%x)\n", BOOTLOADER_OFFSET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // Read first 8 bytes to check for valid bootloader image
    uint8_t header[8];
    fseek(fp, BOOTLOADER_OFFSET, SEEK_SET);
    size_t read = fread(header, sizeof(header), 1, fp);
    if (read != 1) {
        ESP_LOGE(TAG, "Failed to read bootloader header");
        return -1;
    }

    // ESP image magic is 0xE9
    if (header[0] == 0xE9) {
        printf("Status:      VALID\n");
        printf("Magic:       0x%02x (ESP image)\n", header[0]);
        printf("Segment Count: %d\n", header[1]);
    } else {
        printf("Status:      INVALID\n");
        printf("Magic:       0x%02x (expected 0xE9)\n", header[0]);
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return 0;
}

static int print_factory_app_info(FILE* fp) {
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Factory App (offset 0x%x)\n", FACTORY_APP_OFFSET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // Read first 8 bytes to check for valid app image
    uint8_t header[8];
    fseek(fp, FACTORY_APP_OFFSET, SEEK_SET);
    size_t read = fread(header, sizeof(header), 1, fp);
    if (read != 1) {
        ESP_LOGE(TAG, "Failed to read factory app header");
        return -1;
    }

    // ESP image magic is 0xE9
    if (header[0] == 0xE9) {
        printf("Status:      VALID\n");
        printf("Magic:       0x%02x (ESP image)\n", header[0]);
        printf("Segment Count: %d\n", header[1]);
    } else {
        printf("Status:      INVALID\n");
        printf("Magic:       0x%02x (expected 0xE9)\n", header[0]);
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return 0;
}

int cli_inspect_image(const char* image_path) {
    if (!image_path) {
        ESP_LOGE(TAG, "NULL image path");
        return -1;
    }

    ESP_LOGI(TAG, "Inspecting flash image: %s", image_path);

    // Open file
    FILE* fp = fopen(image_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open image file: %s", image_path);
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         ESP32-P4 Flash Image Inspection                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("File:        %s\n", image_path);
    printf("Size:        %ld bytes (%.2f MB)\n", file_size, file_size / (1024.0 * 1024.0));

    // Print sections
    int ret = 0;
    ret |= print_bootloader_info(fp);
    ret |= print_partition_table(fp);
    ret |= print_factory_app_info(fp);
    ret |= print_firmware_storage(fp);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                   Inspection Complete                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    fclose(fp);
    return ret;
}

int cli_load_image(const char* image_path) {
    if (!image_path) {
        ESP_LOGE(TAG, "NULL image path");
        return -1;
    }

    ESP_LOGI(TAG, "Loading flash image: %s", image_path);

    // Open file
    FILE* fp = fopen(image_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open image file: %s", image_path);
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    printf("Loading image: %s (%.2f MB)\n", image_path, file_size / (1024.0 * 1024.0));

    // Read entire file into buffer
    uint8_t* buffer = (uint8_t*)malloc(file_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for image");
        fclose(fp);
        return -1;
    }

    size_t read = fread(buffer, 1, file_size, fp);
    fclose(fp);

    if (read != file_size) {
        ESP_LOGE(TAG, "Failed to read complete image (read %zu of %ld bytes)", read, file_size);
        free(buffer);
        return -1;
    }

    // Load into flash emulator
    esp_err_t ret = flash_emulator_load_image(buffer, file_size);
    free(buffer);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load image into flash emulator");
        return -1;
    }

    printf("✓ Flash image loaded successfully\n");
    printf("  Image size: %.2f MB\n", file_size / (1024.0 * 1024.0));

    return 0;
}

#endif // __SIMULATOR_BUILD__
