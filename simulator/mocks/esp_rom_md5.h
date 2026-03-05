/**
 * @file esp_rom_md5.h
 * @brief Mock implementation of ESP-ROM MD5 API for simulator
 *
 * This header provides the ESP-ROM MD5 API used in ESP-IDF 6.1+
 * The actual implementation is in mbedtls_md5_mock.c
 */

#ifndef ESP_ROM_MD5_H
#define ESP_ROM_MD5_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MD5 context type
 */
typedef struct {
    unsigned long state[4];
    unsigned long count[2];
    unsigned char buffer[64];
} md5_context_t;

/**
 * @brief Initialize MD5 context
 *
 * @param ctx MD5 context to initialize
 */
void esp_rom_md5_init(md5_context_t* ctx);

/**
 * @brief Update MD5 hash with new data
 *
 * @param ctx MD5 context
 * @param data Data to hash
 * @param len Length of data in bytes
 */
void esp_rom_md5_update(md5_context_t* ctx, const void* data, size_t len);

/**
 * @brief Finalize MD5 hash and output result
 *
 * @param output Output buffer (16 bytes for MD5 hash)
 * @param ctx MD5 context
 */
void esp_rom_md5_final(uint8_t* output, md5_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // ESP_ROM_MD5_H
