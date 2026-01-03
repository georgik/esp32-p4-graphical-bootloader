/**
 * @file firmware_storage_config.h
 * @brief Shared firmware storage configuration
 *
 * This header defines the firmware storage offset location that is used by:
 * - Simulator flash builder (simulator/platform/flash_builder.c)
 * - Simulator CLI inspector (simulator/cli_inspector.c)
 * - ESP-IDF application (main/firmware_storage.h)
 * - ESP-IDF bootloader (if needed)
 *
 * IMPORTANT: Any changes to this offset MUST be reflected in all components.
 * The firmware storage metadata contains pointers to OTA partitions, not
 * the actual firmware data, so it can be placed at any fixed location
 * that doesn't conflict with other partitions.
 *
 * Current placement: After bootloader_config (0x12B000 + 64KB = 0x13B000),
 * before OTA partitions start at 0x140000.
 */

#ifndef FIRMWARE_STORAGE_CONFIG_H
#define FIRMWARE_STORAGE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// Firmware storage location in flash
// Fixed location after bootloader_config, before OTA partitions
#define FIRMWARE_STORAGE_OFFSET  0x13C000

// Firmware storage format version
#define FIRMWARE_STORAGE_VERSION 1

// Firmware storage magic string
#define FIRMWARE_STORAGE_MAGIC   "FWST"

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_STORAGE_CONFIG_H
