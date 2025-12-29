#ifndef MBEDTLS_MD5_H_SHIM
#define MBEDTLS_MD5_H_SHIM

#ifdef __SIMULATOR_BUILD__
    #include <stdint.h>
    #include <stddef.h>
    #include <string.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    // MD5 context structure
    typedef struct {
        uint32_t state[4];
        uint64_t total[2];
        unsigned char buffer[64];
    } mbedtls_md5_context;

    // Simple MD5 functions (using OpenSSL's MD5 on macOS)
    void mbedtls_md5_init(mbedtls_md5_context* ctx);
    void mbedtls_md5_free(mbedtls_md5_context* ctx);
    void mbedtls_md5_starts(mbedtls_md5_context* ctx);
    void mbedtls_md5_update(mbedtls_md5_context* ctx, const unsigned char* input, size_t ilen);
    void mbedtls_md5_finish(mbedtls_md5_context* ctx, unsigned char output[16]);
    void mbedtls_md5(const uint8_t* input, size_t ilen, uint8_t output[16]);

    #ifdef __cplusplus
    }
    #endif

#else
    #include <stdint.h>
    #include <stddef.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    void mbedtls_md5(const uint8_t* input, size_t ilen, uint8_t output[16]);

    #ifdef __cplusplus
    }
    #endif
#endif

#endif
