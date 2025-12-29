/**
 * @file mbedtls_md5_mock.c
 * @brief Mock implementation of mbedTLS MD5 using macOS CommonCrypto
 */

#include "main/mbedtls/md5.h"
#include <CommonCrypto/CommonDigest.h>

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
