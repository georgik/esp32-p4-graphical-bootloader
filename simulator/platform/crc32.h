/**
 * @file crc32.h
 * @brief CRC32 calculation for firmware integrity verification
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate CRC32 checksum
 *
 * Uses IEEE 802.3 CRC32 polynomial (0xEDB88320).
 * This is the standard CRC32 used in ZIP, PNG, Ethernet, etc.
 *
 * @param data Data buffer to calculate checksum for
 * @param length Length of data in bytes
 * @return CRC32 checksum
 */
uint32_t crc32_calculate(const uint8_t* data, size_t length);

/**
 * @brief Initialize CRC32 calculation context
 *
 * @return Initial CRC value (0xFFFFFFFF)
 */
uint32_t crc32_init(void);

/**
 * @brief Update CRC32 with new data
 *
 * @param crc Current CRC value (from crc32_init or previous crc32_update)
 * @param data New data to incorporate
 * @param length Length of new data
 * @return Updated CRC value
 */
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length);

/**
 * @brief Finalize CRC32 calculation
 *
 * @param crc Current CRC value
 * @return Final CRC32 checksum
 */
uint32_t crc32_finalize(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif // CRC32_H
