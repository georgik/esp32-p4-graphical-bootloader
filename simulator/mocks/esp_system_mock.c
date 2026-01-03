/**
 * @file esp_system_mock.c
 * @brief Mock implementation of ESP system functions
 */

#include "esp_system_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void esp_restart(void) {
    printf("\n");
    printf("=============================================\n");
    printf("🔄 ESP Restart Requested\n");
    printf("=============================================\n");
    printf("In simulator, this would restart the device.\n");
    printf("Exiting simulator instead...\n");
    printf("=============================================\n");
    printf("\n");

    exit(0);
}

void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;  // Ignore capabilities in simulator
    return malloc(size);
}

void heap_caps_free(void* ptr) {
    free(ptr);
}

// CRC32 calculation (simple implementation using macOS's CommonCrypto)
#include <CommonCrypto/CommonDigest.h>
#include <zlib.h>

uint32_t esp_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len) {
    // Use zlib's crc32 function which is compatible
    uLong result = crc32(crc, buf, len);
    return (uint32_t)result;
}

// Note: esp_flash functions are now in esp_flash_mock.c with flash emulator support
