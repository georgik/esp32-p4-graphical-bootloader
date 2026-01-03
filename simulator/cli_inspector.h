/**
 * @file cli_inspector.h
 * @brief Flash image inspection utilities
 */

#ifndef CLI_INSPECTOR_H
#define CLI_INSPECTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __SIMULATOR_BUILD__

/**
 * @brief Inspect a flash image file
 *
 * Reads and displays:
 * - Partition table
 * - Firmware storage (if present)
 * - Bootloader info
 * - Factory app info
 *
 * @param image_path Path to flash image file
 * @return 0 on success, -1 on error
 */
int cli_inspect_image(const char* image_path);

/**
 * @brief Load flash image into flash emulator
 *
 * Loads a flash image file into the flash emulator's memory.
 *
 * @param image_path Path to flash image file
 * @return 0 on success, -1 on error
 */
int cli_load_image(const char* image_path);

#endif // __SIMULATOR_BUILD__

#ifdef __cplusplus
}
#endif

#endif // CLI_INSPECTOR_H
