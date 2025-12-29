/**
 * @file esp_system_mock.h
 * @brief Mock implementation of ESP system functions for simulator
 */

#ifndef ESP_SYSTEM_MOCK_H
#define ESP_SYSTEM_MOCK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ESP error codes
typedef int esp_err_t;

#define ESP_OK                    0
#define ESP_FAIL                 -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_NOT_FOUND       0x104
#define ESP_ERR_NOT_SUPPORTED   0x105
#define ESP_ERR_TIMEOUT         0x107
#define ESP_ERR_INVALID_SIZE    0x108
#define ESP_ERR_NVS_NOT_FOUND   0x110  // NVS specific error
#define ESP_ERR_NVS_INVALID_HANDLE 0x111  // NVS invalid handle
#define ESP_ERR_NVS_NO_FREE_PAGES 0x112  // NVS no free pages
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x113  // NVS new version
#define ESP_ERR_NVS_NOT_INITIALIZED 0x114  // NVS not initialized
#define ESP_ERR_INVALID_RESPONSE 0x115     // Invalid response

// System functions
void esp_restart(void) __attribute__((noreturn));

static inline uint32_t esp_get_free_heap_size(void) {
    return 8 * 1024 * 1024;  // 8MB mock heap
}

static inline uint32_t esp_get_minimum_free_heap_size(void) {
    return 4 * 1024 * 1024;  // 4MB minimum
}

// Heap capabilities for memory types
#define MALLOC_CAP_IRAM_8BIT   0x01
#define MALLOC_CAP_SPIRAM      0x02
#define MALLOC_CAP_DMA         0x04
#define MALLOC_CAP_INTERNAL    0x08
#define MALLOC_CAP_DEFAULT     0x00

static inline uint32_t heap_caps_get_free_size(uint32_t caps) {
    switch(caps) {
        case MALLOC_CAP_IRAM_8BIT:
            return 4 * 1024 * 1024;  // 4MB IRAM
        case MALLOC_CAP_SPIRAM:
            return 8 * 1024 * 1024;  // 8MB PSRAM
        default:
            return esp_get_free_heap_size();
    }
}

static inline uint32_t heap_caps_get_largest_free_block(uint32_t caps) {
    return heap_caps_get_free_size(caps) / 2;
}

// Memory allocation with capabilities
void* heap_caps_malloc(size_t size, uint32_t caps);
void heap_caps_free(void* ptr);

// CRC32 calculation
uint32_t esp_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // ESP_SYSTEM_MOCK_H
