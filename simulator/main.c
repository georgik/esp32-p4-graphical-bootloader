/**
 * @file main.c
 * @brief ESP32-P4 Bootloader Simulator - Main Entry Point
 *
 * Desktop simulator for ESP32-P4 graphical bootloader using LVGL + SDL2
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <execinfo.h>

// Mock headers - MUST be included before ESP-IDF headers
#include "esp_mock_header.h"
#include "platform/lvgl_sdl_init.h"
#include "platform/flash_builder.h"
#include "platform/flash_emulator.h"
#include "cli_parser.h"
#include "cli_inspector.h"

// Bootloader headers
#include "../main/lvgl_bootloader.h"
#include "../main/board_init.h"

static const char* TAG = "simulator";

// Flag to control main loop
static volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    if (running) {  // Only print once
        ESP_LOGI(TAG, "\nReceived signal %d, shutting down...", sig);
        running = 0;
    }
}

// Crash handler with backtrace
void crash_handler(int sig) {
    void *array[32];
    size_t size;

    // Get void*'s for all entries on the stack
    size = backtrace(array, 32);

    // Print out all the frames to stderr
    fprintf(stderr, "\n");
    fprintf(stderr, "╔════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║                   CRASH DETECTED!                           ║\n");
    fprintf(stderr, "║                                                            ║\n");
    fprintf(stderr, "║  Error: Signal %d received                                ║\n", sig);
    fprintf(stderr, "║                                                            ║\n");
    fprintf(stderr, "║  Backtrace:                                                ║\n");

    char** symbols = backtrace_symbols(array, size);
    for (size_t i = 0; i < size; i++) {
        fprintf(stderr, "║    [%2zu] %s                                            ║\n", i, symbols[i]);
    }
    free(symbols);

    fprintf(stderr, "║                                                            ║\n");
    fprintf(stderr, "║  To get file:line, run:                                  ║\n");
    fprintf(stderr, "║    atos -o build/simulator <address>                      ║\n");
    fprintf(stderr, "║                                                            ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "\n");

    exit(1);
}

// Print welcome banner
void print_banner(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                                                            ║\n");
    printf("║     ESP32-P4 Graphical Bootloader Simulator              ║\n");
    printf("║     Running on macOS via LVGL + SDL2                      ║\n");
    printf("║                                                            ║\n");
    printf("║     Features:                                              ║\n");
    printf("║     • Full LVGL UI (1024x600)                             ║\n");
    printf("║     • Partition visualization                            ║\n");
    printf("║     • Flash write simulation                             ║\n");
    printf("║     • NVS persistence                                    ║\n");
    printf("║                                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

// Print usage instructions
void print_usage(void) {
    printf("Controls:\n");
    printf("  • Use mouse to interact with touch interface\n");
    printf("  • Press Ctrl+C to exit\n");
    printf("\n");
    printf("Data Storage:\n");
    printf("  • Flash image:      simulated-flash.bin\n");
    printf("  • NVS storage:      .esp32-simulator/nvs.json\n");
    printf("\n");
}

// Initialize flash image
esp_err_t initialize_flash_image(void) {
    ESP_LOGI(TAG, "=== Initializing Flash Image ===");

    const char* flash_path = "simulated-flash.bin";
    const char* build_dir = "../build/";

    // Check if flash image exists
    if (flash_builder_exists(flash_path)) {
        ESP_LOGI(TAG, "Flash image already exists: %s", flash_path);

        // Validate existing flash image
        flash_builder_err_t ret = flash_builder_validate(flash_path);
        if (ret == FLASH_BUILDER_OK) {
            ESP_LOGI(TAG, "✅ Flash image validated");
        } else {
            ESP_LOGW(TAG, "Flash image validation failed, will recreate");
            // Recreate flash image
            flash_builder_err_t ret = flash_builder_create(flash_path, build_dir);
            if (ret != FLASH_BUILDER_OK) {
                ESP_LOGE(TAG, "Failed to create flash image: %d", ret);
                return ESP_FAIL;
            }
        }
    } else {
        // Create flash image from ESP-IDF build artifacts
        ESP_LOGI(TAG, "Creating flash image from ESP-IDF build directory...");
        ESP_LOGI(TAG, "  Build directory: %s", build_dir);

        flash_builder_err_t ret = flash_builder_create(flash_path, build_dir);
        if (ret != FLASH_BUILDER_OK) {
            ESP_LOGE(TAG, "Failed to create flash image: %d", ret);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "✅ Flash image created successfully");
    }

    // Initialize flash emulator with mmap
    ESP_LOGI(TAG, "Initializing flash emulator...");
    esp_err_t ret = flash_emulator_init(flash_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize flash emulator: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ Flash emulator ready");
    return ESP_OK;
}

// Initialize system
esp_err_t initialize_simulator(void) {
    ESP_LOGI(TAG, "=== Initializing ESP32-P4 Bootloader Simulator ===");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s, erasing...", esp_err_to_name(ret));
        nvs_flash_erase();
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS init failed after erase");
            return ret;
        }
    }
    ESP_LOGI(TAG, "✅ NVS initialized");

    // Initialize LVGL with SDL2
    ret = init_lvgl_sdl();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL/SDL2");
        return ret;
    }
    ESP_LOGI(TAG, "✅ LVGL/SDL2 initialized");

    // Initialize bootloader UI
    ret = lvgl_bootloader_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bootloader UI");
        return ret;
    }
    ESP_LOGI(TAG, "✅ Bootloader UI initialized");

    ESP_LOGI(TAG, "=== Simulator Initialization Complete ===\n");

    return ESP_OK;
}

// Main event loop
void event_loop(void) {
    ESP_LOGI(TAG, "Starting event loop...\n");

    uint64_t iteration = 0;
    while (running) {
        // Check running flag before potentially blocking operations
        if (!running) {
            ESP_LOGI(TAG, "Loop exit: running flag check 1");
            break;
        }

        // Process SDL events (returns false if quit requested)
        if (!lvgl_sdl_process_events()) {
            ESP_LOGI(TAG, "Loop exit: SDL quit event");
            running = false;
            break;
        }

        // Check running flag again after SDL events
        if (!running) {
            ESP_LOGI(TAG, "Loop exit: running flag check 2");
            break;
        }

        // Check running flag before LVGL handler
        if (!running) {
            ESP_LOGI(TAG, "Loop exit: running flag check 3");
            break;
        }

        // Handle LVGL events
        lvgl_tick_handler();

        // Small delay to prevent high CPU usage
        usleep(100);

        // Log status every 10000 iterations (every ~1 second)
        if (++iteration % 10000 == 0) {
            ESP_LOGD(TAG, "Simulator running... (iteration %llu)", iteration);
        }
    }

    ESP_LOGI(TAG, "Event loop exited");
}

// Cleanup
void cleanup(void) {
    ESP_LOGI(TAG, "Cleaning up...");

    // Cleanup LVGL
    lvgl_bootloader_deinit();

    // Cleanup SDL resources
    lvgl_sdl_cleanup();

    // Cleanup flash emulator
    flash_emulator_deinit();

    // NVS is automatically saved on modification

    ESP_LOGI(TAG, "Cleanup complete");
}

// Main entry point
int main(int argc, char** argv) {
    // Create CLI configuration
    cli_config_t* config = cli_config_create();
    if (!config) {
        ESP_LOGE(TAG, "Failed to create CLI config");
        return 1;
    }

    // Parse command line arguments
    int mode = cli_parse_args(argc, argv, config);
    if (mode < 0) {
        // Error or --help shown
        cli_config_free(config);
        return (mode == -1) ? 0 : 1;
    }

    // Set log level if verbose
    if (config->verbose) {
        esp_log_level_set("*", ESP_LOG_VERBOSE);
    }

    // Handle CLI modes
    if (mode == MODE_LIST_FIRMWARES) {
        int ret = cli_list_firmwares();
        cli_config_free(config);
        return (ret == 0) ? 0 : 1;
    }

    if (mode == MODE_INSPECT_IMAGE) {
        int ret = cli_inspect_image(config->inspect_image_path);
        cli_config_free(config);
        return (ret == 0) ? 0 : 1;
    }

    if (mode == MODE_CREATE_IMAGE) {
        // Validate configuration
        int ret = cli_validate_config(config);
        if (ret != 0) {
            ESP_LOGE(TAG, "Configuration validation failed");
            cli_config_free(config);
            return 1;
        }

        // Print configuration
        cli_print_config(config);

        // Create flash image with firmwares
        flash_builder_err_t fb_ret = flash_builder_create_with_firmwares(
            config->output_path ? config->output_path : "simulated-flash.bin",
            config->bootloader_path,
            config->partition_table_path,
            config->factory_app_path,
            config->firmware_paths,
            config->firmware_names,
            config->firmware_count,
            config->trim_zeros,
            config->flash_size_mb
        );

        cli_config_free(config);

        if (fb_ret == FLASH_BUILDER_OK) {
            ESP_LOGI(TAG, "\n✓ Done!");
            return 0;
        } else {
            ESP_LOGE(TAG, "Failed to create flash image: %d", fb_ret);
            return 1;
        }
    }

    // MODE_LOAD_AND_SIMULATE: Load image then run simulator
    if (mode == MODE_LOAD_AND_SIMULATE) {
        ESP_LOGI(TAG, "Loading flash image from file: %s", config->load_image_path);

        int ret = cli_load_image(config->load_image_path);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to load flash image");
            cli_config_free(config);
            return 1;
        }

        // Image loaded, continue to simulator GUI
        ESP_LOGI(TAG, "Image loaded, starting simulator GUI...\n");
    }

    // Default: MODE_SIMULATE or MODE_LOAD_AND_SIMULATE
    // Print banner
    print_banner();

    // Print usage
    print_usage();

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Setup crash handlers
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGABRT, crash_handler);

    // Initialize flash image (only if not loading from file)
    esp_err_t ret;
    if (mode != MODE_LOAD_AND_SIMULATE) {
        ret = initialize_flash_image();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Flash image initialization failed: %s", esp_err_to_name(ret));
            cli_config_free(config);
            return 1;
        }
    }

    // Initialize simulator
    ret = initialize_simulator();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Simulator initialization failed: %s", esp_err_to_name(ret));
        cli_config_free(config);
        return 1;
    }

    // Run event loop
    event_loop();

    // Cleanup
    cleanup();

    cli_config_free(config);

    printf("\n");
    printf("Simulator exited cleanly.\n");
    printf("\n");

    return 0;
}
