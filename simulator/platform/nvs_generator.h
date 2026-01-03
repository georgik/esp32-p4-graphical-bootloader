/**
 * @file nvs_generator.h
 * @brief NVS partition generator for firmware metadata
 */

#ifndef NVS_GENERATOR_H
#define NVS_GENERATOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __SIMULATOR_BUILD__

/**
 * @brief Generate NVS partition with firmware metadata
 *
 * This function initializes NVS and stores firmware metadata for all
 * firmwares included in the flash image.
 *
 * @param flash_image Buffer containing the flash image
 * @param flash_size Size of flash image
 * @param firmware_names Array of firmware names
 * @param firmware_sizes Array of firmware sizes
 * @param firmware_crcs Array of firmware CRC32 values
 * @param firmware_count Number of firmwares
 * @param nvs_offset Offset of NVS partition in flash (typically 0x120000)
 * @param nvs_size Size of NVS partition (typically 0x8000 = 32KB)
 * @return 0 on success, -1 on error
 */
int nvs_generate_firmware_metadata(
    uint8_t* flash_image,
    size_t flash_size,
    char** firmware_names,
    uint32_t* firmware_sizes,
    uint32_t* firmware_crcs,
    int firmware_count,
    uint32_t nvs_offset,
    size_t nvs_size
);

#endif // __SIMULATOR_BUILD__

#ifdef __cplusplus
}
#endif

#endif // NVS_GENERATOR_H
