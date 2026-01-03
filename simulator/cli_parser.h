/**
 * @file cli_parser.h
 * @brief CLI parser for multi-firmware flash image generation
 */

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of firmwares we can store in one image
#define MAX_FIRMWARE_COUNT 32

/**
 * @brief CLI execution modes
 */
typedef enum {
    MODE_SIMULATE,          // Run simulator (default)
    MODE_CREATE_IMAGE,      // Create flash image and exit
    MODE_LIST_FIRMWARES,    // List available firmwares and exit
    MODE_INSPECT_IMAGE,     // Inspect flash image file (partition table, firmware storage, etc.)
    MODE_LOAD_AND_SIMULATE  // Load flash image from file and run simulator
} cli_mode_t;

/**
 * @brief CLI configuration
 */
typedef struct {
    // Execution mode
    cli_mode_t mode;

    // Input files
    char* bootloader_path;
    char* partition_table_path;
    char* factory_app_path;

    // Firmware storage
    char** firmware_paths;      // Array of firmware file paths
    char** firmware_names;       // Array of firmware display names
    int firmware_count;          // Number of firmwares

    // Output options
    char* output_path;
    bool trim_zeros;
    bool force_overwrite;
    int flash_size_mb;

    // Image loading/inspection
    char* load_image_path;       // Path to flash image file to load
    char* inspect_image_path;     // Path to flash image file to inspect

    // Logging
    bool verbose;
} cli_config_t;

/**
 * @brief Create CLI configuration structure
 *
 * @return Newly allocated config, or NULL on error
 */
cli_config_t* cli_config_create(void);

/**
 * @brief Free CLI configuration structure
 *
 * @param config Config to free (can be NULL)
 */
void cli_config_free(cli_config_t* config);

/**
 * @brief Parse command-line arguments
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @param config Output configuration (must be allocated)
 * @return CLI mode, or -1 on error
 */
int cli_parse_args(int argc, char** argv, cli_config_t* config);

/**
 * @brief Validate CLI configuration
 *
 * Checks that all specified files exist and sizes are reasonable.
 *
 * @param config Configuration to validate
 * @return 0 if valid, error code otherwise
 */
int cli_validate_config(cli_config_t* config);

/**
 * @brief Print CLI configuration
 *
 * @param config Configuration to print
 */
void cli_print_config(cli_config_t* config);

/**
 * @brief List available firmwares in sdcard/firmwares/
 *
 * @return 0 on success, error code otherwise
 */
int cli_list_firmwares(void);

/**
 * @brief Resolve firmware path
 *
 * If name is not absolute/relative path, looks in sdcard/firmwares/.
 *
 * @param name Firmware name or path
 * @return Allocated path string, or NULL if not found
 */
char* cli_resolve_firmware_path(const char* name);

/**
 * @brief Print usage/help message
 *
 * @param program_name Program name (argv[0])
 */
void cli_print_usage(const char* program_name);

/**
 * @brief Print create-image usage examples
 */
void cli_print_create_image_examples(void);

#ifdef __cplusplus
}
#endif

#endif // CLI_PARSER_H
