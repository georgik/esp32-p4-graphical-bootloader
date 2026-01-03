/**
 * @file cli_parser.c
 * @brief CLI parser implementation for multi-firmware flash image generation
 */

#ifdef __SIMULATOR_BUILD__

#include "cli_parser.h"
#include "esp_log_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char* TAG = "cli_parser";

// Default paths
#define DEFAULT_BOOTLOADER_PATH     "../build/bootloader/bootloader.bin"
#define DEFAULT_PARTITION_PATH      "../build/partition_table/partition-table.bin"
#define DEFAULT_FACTORY_PATH        "../build/esp32_p4_graphical_bootloader.bin"
#define DEFAULT_OUTPUT_PATH         "simulated-flash.bin"
#define DEFAULT_SDCARD_PATH         "sdcard/firmwares"
#define DEFAULT_FLASH_SIZE_MB       16

cli_config_t* cli_config_create(void) {
    cli_config_t* config = (cli_config_t*)calloc(1, sizeof(cli_config_t));
    if (!config) {
        ESP_LOGE(TAG, "Failed to allocate config");
        return NULL;
    }

    // Allocate firmware arrays
    config->firmware_paths = (char**)calloc(MAX_FIRMWARE_COUNT, sizeof(char*));
    config->firmware_names = (char**)calloc(MAX_FIRMWARE_COUNT, sizeof(char*));
    if (!config->firmware_paths || !config->firmware_names) {
        ESP_LOGE(TAG, "Failed to allocate firmware arrays");
        free(config->firmware_paths);
        free(config->firmware_names);
        free(config);
        return NULL;
    }

    // Set defaults
    config->mode = MODE_SIMULATE;
    config->flash_size_mb = DEFAULT_FLASH_SIZE_MB;
    config->trim_zeros = false;
    config->force_overwrite = false;
    config->verbose = false;

    return config;
}

void cli_config_free(cli_config_t* config) {
    if (!config) {
        return;
    }

    // Free input paths
    free(config->bootloader_path);
    free(config->partition_table_path);
    free(config->factory_app_path);
    free(config->output_path);

    // Free firmware arrays
    if (config->firmware_paths) {
        for (int i = 0; i < config->firmware_count; i++) {
            free(config->firmware_paths[i]);
            free(config->firmware_names[i]);
        }
        free(config->firmware_paths);
    }
    if (config->firmware_names) {
        free(config->firmware_names);
    }

    free(config);
}

char* cli_resolve_firmware_path(const char* name) {
    if (!name) {
        return NULL;
    }

    // Check if it's an absolute or relative path
    if (name[0] == '/' || (name[0] == '.' && name[1] == '/')) {
        // Use as-is
        return strdup(name);
    }

    // Try sdcard/firmwares/<name>
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", DEFAULT_SDCARD_PATH, name);

    // Check if file exists
    struct stat st;
    if (stat(path, &st) == 0) {
        return strdup(path);
    }

    // Try with .bin extension
    snprintf(path, sizeof(path), "%s/%s.bin", DEFAULT_SDCARD_PATH, name);
    if (stat(path, &st) == 0) {
        return strdup(path);
    }

    ESP_LOGE(TAG, "Firmware not found: %s", name);
    return NULL;
}

int cli_parse_args(int argc, char** argv, cli_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "NULL config");
        return -1;
    }

    // Default mode
    config->mode = MODE_SIMULATE;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            cli_print_usage(argv[0]);
            return -1;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config->verbose = true;
        }
        else if (strcmp(argv[i], "--create-image") == 0) {
            config->mode = MODE_CREATE_IMAGE;
        }
        else if (strcmp(argv[i], "--list-firmwares") == 0) {
            config->mode = MODE_LIST_FIRMWARES;
            return config->mode;
        }
        else if (strcmp(argv[i], "--inspect") == 0) {
            config->mode = MODE_INSPECT_IMAGE;
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--inspect requires argument");
                return -1;
            }
            free(config->inspect_image_path);
            config->inspect_image_path = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "--load-image") == 0) {
            config->mode = MODE_LOAD_AND_SIMULATE;
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--load-image requires argument");
                return -1;
            }
            free(config->load_image_path);
            config->load_image_path = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--output requires argument");
                return -1;
            }
            free(config->output_path);
            config->output_path = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "--bootloader") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--bootloader requires argument");
                return -1;
            }
            free(config->bootloader_path);
            config->bootloader_path = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "--partition") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--partition requires argument");
                return -1;
            }
            free(config->partition_table_path);
            config->partition_table_path = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "--factory") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--factory requires argument");
                return -1;
            }
            free(config->factory_app_path);
            config->factory_app_path = strdup(argv[++i]);
        }
        else if (strcmp(argv[i], "--firmware") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--firmware requires argument");
                return -1;
            }
            if (config->firmware_count >= MAX_FIRMWARE_COUNT) {
                ESP_LOGE(TAG, "Too many firmwares (max %d)", MAX_FIRMWARE_COUNT);
                return -1;
            }
            char* path = strdup(argv[++i]);
            char* name = strdup(path);  // Use full path as name for now
            config->firmware_paths[config->firmware_count] = path;
            config->firmware_names[config->firmware_count] = name;
            config->firmware_count++;
        }
        else if (strcmp(argv[i], "--from-sdcard") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--from-sdcard requires argument");
                return -1;
            }
            if (config->firmware_count >= MAX_FIRMWARE_COUNT) {
                ESP_LOGE(TAG, "Too many firmwares (max %d)", MAX_FIRMWARE_COUNT);
                return -1;
            }
            const char* name = argv[++i];
            char* path = cli_resolve_firmware_path(name);
            if (!path) {
                ESP_LOGE(TAG, "Failed to resolve firmware: %s", name);
                return -1;
            }
            config->firmware_paths[config->firmware_count] = path;
            config->firmware_names[config->firmware_count] = strdup(name);
            config->firmware_count++;
        }
        else if (strcmp(argv[i], "--trim") == 0) {
            config->trim_zeros = true;
        }
        else if (strcmp(argv[i], "--force") == 0) {
            config->force_overwrite = true;
        }
        else if (strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc) {
                ESP_LOGE(TAG, "--size requires argument");
                return -1;
            }
            config->flash_size_mb = atoi(argv[++i]);
            if (config->flash_size_mb < 1 || config->flash_size_mb > 128) {
                ESP_LOGE(TAG, "Invalid flash size: %d MB (must be 1-128)", config->flash_size_mb);
                return -1;
            }
        }
        else {
            ESP_LOGE(TAG, "Unknown argument: %s", argv[i]);
            ESP_LOGE(TAG, "Use --help for usage information");
            return -1;
        }
    }

    return config->mode;
}

int cli_validate_config(cli_config_t* config) {
    if (!config) {
        ESP_LOGE(TAG, "NULL config");
        return -1;
    }

    // Check input files exist
    struct stat st;

    // Bootloader
    const char* bl_path = config->bootloader_path ? config->bootloader_path : DEFAULT_BOOTLOADER_PATH;
    if (stat(bl_path, &st) != 0) {
        ESP_LOGE(TAG, "Bootloader not found: %s", bl_path);
        return -1;
    }

    // Partition table
    const char* pt_path = config->partition_table_path ? config->partition_table_path : DEFAULT_PARTITION_PATH;
    if (stat(pt_path, &st) != 0) {
        ESP_LOGE(TAG, "Partition table not found: %s", pt_path);
        return -1;
    }

    // Factory app
    const char* fa_path = config->factory_app_path ? config->factory_app_path : DEFAULT_FACTORY_PATH;
    if (stat(fa_path, &st) != 0) {
        ESP_LOGE(TAG, "Factory app not found: %s", fa_path);
        return -1;
    }

    // Check output file
    const char* out_path = config->output_path ? config->output_path : DEFAULT_OUTPUT_PATH;
    if (stat(out_path, &st) == 0 && !config->force_overwrite) {
        ESP_LOGE(TAG, "Output file exists: %s (use --force to overwrite)", out_path);
        return -1;
    }

    // Check firmwares
    if (config->firmware_count == 0) {
        ESP_LOGW(TAG, "No firmwares specified (use --from-sdcard or --firmware)");
    }

    for (int i = 0; i < config->firmware_count; i++) {
        if (stat(config->firmware_paths[i], &st) != 0) {
            ESP_LOGE(TAG, "Firmware %d not found: %s", i, config->firmware_paths[i]);
            return -1;
        }
        ESP_LOGI(TAG, "  Firmware %d: %s (%ld bytes)",
                 i, config->firmware_names[i], st.st_size);
    }

    return 0;
}

void cli_print_config(cli_config_t* config) {
    if (!config) {
        return;
    }

    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Configuration\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    const char* bl_path = config->bootloader_path ? config->bootloader_path : DEFAULT_BOOTLOADER_PATH;
    const char* pt_path = config->partition_table_path ? config->partition_table_path : DEFAULT_PARTITION_PATH;
    const char* fa_path = config->factory_app_path ? config->factory_app_path : DEFAULT_FACTORY_PATH;
    const char* out_path = config->output_path ? config->output_path : DEFAULT_OUTPUT_PATH;

    printf("Bootloader:       %s\n", bl_path);
    printf("Partition Table:  %s\n", pt_path);
    printf("Factory App:      %s\n", fa_path);

    if (config->firmware_count > 0) {
        printf("\nFirmwares to include (%d):\n", config->firmware_count);
        for (int i = 0; i < config->firmware_count; i++) {
            struct stat st;
            if (stat(config->firmware_paths[i], &st) == 0) {
                double size_mb = st.st_size / (1024.0 * 1024.0);
                printf("  %d. %s (%.2f MB)\n", i + 1, config->firmware_names[i], size_mb);
            }
        }
    }

    printf("\nOutput:           %s\n", out_path);
    printf("Flash Size:       %d MB\n", config->flash_size_mb);
    printf("Trim Zeros:       %s\n", config->trim_zeros ? "Yes" : "No");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\n");
}

int cli_list_firmwares(void) {
    const char* dir_path = DEFAULT_SDCARD_PATH;
    DIR* dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return -1;
    }

    printf("\n");
    printf("Available firmware binaries in %s:\n", dir_path);
    printf("  (You can use these names with --from-sdcard)\n");
    printf("\n");

    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".bin") != NULL) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

            struct stat st;
            if (stat(path, &st) == 0) {
                double size_mb = st.st_size / (1024.0 * 1024.0);
                printf("  %2d. %-35s (%6.1f MB)\n", ++count, entry->d_name, size_mb);
            }
        }
    }

    closedir(dir);

    printf("\n");
    printf("Total: %d firmware binaries\n", count);
    printf("\n");

    return 0;
}

void cli_print_usage(const char* program_name) {
    printf("\n");
    printf("ESP32-P4 Bootloader Simulator\n");
    printf("\n");
    printf("Usage: %s [MODE] [OPTIONS]\n", program_name);
    printf("\n");
    printf("Modes:\n");
    printf("  --simulate            Run simulator (default)\n");
    printf("  --create-image        Create flash image and exit\n");
    printf("  --list-firmwares      List available firmware binaries\n");
    printf("  --inspect <file>      Inspect flash image file (partition table, firmware storage)\n");
    printf("  --load-image <file>   Load flash image and run simulator\n");
    printf("\n");
    printf("Create-Image Options:\n");
    printf("  --output <file>       Output filename (default: %s)\n", DEFAULT_OUTPUT_PATH);
    printf("  --bootloader <bin>    Bootloader binary path\n");
    printf("  --partition <bin>     Partition table path\n");
    printf("  --factory <bin>       Factory app binary path\n");
    printf("  --firmware <bin>      Add firmware binary (can be used multiple times)\n");
    printf("  --from-sdcard <name>  Add firmware from sdcard/firmwares/ (multiple times)\n");
    printf("  --trim                Trim trailing zeros after creation\n");
    printf("  --force               Overwrite existing output file\n");
    printf("  --size <MB>           Flash size in MB (default: %d)\n", DEFAULT_FLASH_SIZE_MB);
    printf("\n");
    printf("General Options:\n");
    printf("  -v, --verbose         Enable verbose logging\n");
    printf("  -h, --help            Show this help message\n");
    printf("\n");

    cli_print_create_image_examples();
}

void cli_print_create_image_examples(void) {
    printf("Examples:\n");
    printf("\n");
    printf("  # Create image with multiple GUI framework applications\n");
    printf("  %s --create-image \\\n", "simulator");
    printf("    --from-sdcard \"Application 1\" \\\n");
    printf("    --from-sdcard \"Application 2\" \\\n");
    printf("    --output flash-combined.bin \\\n");
    printf("    --trim\n");
    printf("\n");
    printf("  # List available firmwares\n");
    printf("  %s --list-firmwares\n", "simulator");
    printf("\n");
    printf("  # Inspect flash image\n");
    printf("  %s --inspect flash-image.bin\n", "simulator");
    printf("\n");
    printf("  # Load flash image and run simulator\n");
    printf("  %s --load-image flash-image.bin\n", "simulator");
    printf("\n");
    printf("  # Create image with 4 GUI applications\n");
    printf("  %s --create-image \\\n", "simulator");
    printf("    --from-sdcard \"App 1\" \\\n");
    printf("    --from-sdcard \"App 2\" \\\n");
    printf("    --from-sdcard \"App 3\" \\\n");
    printf("    --from-sdcard \"App 4\" \\\n");
    printf("    --output flash-multi.bin --trim\n");
    printf("\n");
}

#endif // __SIMULATOR_BUILD__
