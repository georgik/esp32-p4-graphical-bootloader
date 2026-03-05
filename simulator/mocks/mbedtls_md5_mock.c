/**
 * @file mbedtls_md5_mock.c
 * @brief Mock implementation of MD5 using macOS CommonCrypto
 *        Provides both mbedTLS MD5 and ESP-ROM MD5 APIs
 */

#include <CommonCrypto/CommonDigest.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// mbedTLS MD5 API (Legacy - ESP-IDF < 6.1)
// ============================================================================

// Type definitions for mbedTLS MD5 compatibility
typedef CC_MD5_CTX mbedtls_md5_context;

void mbedtls_md5_init(mbedtls_md5_context* ctx) {
    (void)ctx;
    // No initialization needed for CommonCrypto
}

void mbedtls_md5_free(mbedtls_md5_context* ctx) {
    (void)ctx;
    // No cleanup needed for CommonCrypto
}

void mbedtls_md5_starts(mbedtls_md5_context* ctx) {
    // Start a new MD5 context
    CC_MD5_Init(ctx);
}

void mbedtls_md5_update(mbedtls_md5_context* ctx, const unsigned char* input, size_t ilen) {
    CC_MD5_Update(ctx, input, ilen);
}

void mbedtls_md5_finish(mbedtls_md5_context* ctx, unsigned char output[16]) {
    CC_MD5_Final(output, ctx);
}

void mbedtls_md5(const uint8_t* input, size_t ilen, uint8_t output[16]) {
    CC_MD5(input, ilen, output);
}

// ============================================================================
// ESP-ROM MD5 API (ESP-IDF 6.1+)
// ============================================================================

// Type definition for ESP-ROM MD5 compatibility
typedef CC_MD5_CTX md5_context_t;

void esp_rom_md5_init(md5_context_t* ctx) {
    // Initialize MD5 context
    CC_MD5_Init(ctx);
}

void esp_rom_md5_update(md5_context_t* ctx, const void* data, size_t len) {
    // Update MD5 hash with new data
    CC_MD5_Update(ctx, data, (CC_LONG)len);
}

void esp_rom_md5_final(uint8_t* output, md5_context_t* ctx) {
    // Finalize MD5 hash and output result
    CC_MD5_Final(output, ctx);
}
