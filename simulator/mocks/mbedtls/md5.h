/**
 * @file md5.h
 * @brief mbedTLS MD5 wrapper for simulator
 */

#ifndef MBEDTLS_MD5_H_MOCK
#define MBEDTLS_MD5_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include <stdint.h>
    #include <stddef.h>
    #include <CommonCrypto/CommonDigest.h>

    // Type definitions for mbedTLS MD5 compatibility
    typedef CC_MD5_CTX mbedtls_md5_context;

    // Function declarations
    void mbedtls_md5_init(mbedtls_md5_context* ctx);
    void mbedtls_md5_free(mbedtls_md5_context* ctx);
    void mbedtls_md5_starts(mbedtls_md5_context* ctx);
    void mbedtls_md5_update(mbedtls_md5_context* ctx, const unsigned char* input, size_t ilen);
    void mbedtls_md5_finish(mbedtls_md5_context* ctx, unsigned char output[16]);
    void mbedtls_md5(const uint8_t* input, size_t ilen, uint8_t output[16]);

#else
    #include_next "mbedtls/md5.h"
#endif

#endif // MBEDTLS_MD5_H_MOCK
