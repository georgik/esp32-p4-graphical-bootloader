/**
 * @file vdma_protection.h
 * @brief VDMA display protection functions for ESP32-P4
 *
 * These functions provide coordination between LVGL display rendering
 * and SD card OTA operations to prevent display flickering caused by
 * DMA bandwidth contention between MIPI-DSI and SD card controllers.
 */

#ifndef VDMA_PROTECTION_H
#define VDMA_PROTECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable VDMA display protection mode
 *
 * When enabled, intensive operations (like SD card reads) should be
 * avoided or minimized to prevent display flickering.
 */
void vdma_enable_display_protection(void);

/**
 * @brief Disable VDMA display protection mode
 *
 * When disabled, intensive operations can proceed normally.
 */
void vdma_disable_display_protection(void);

/**
 * @brief Check if VDMA display protection is currently enabled
 *
 * @return true if display protection is enabled, false otherwise
 */
bool vdma_is_display_protected(void);

/**
 * @brief Ensure minimum time between display refresh operations
 *
 * This function blocks if necessary to ensure that at least the specified
 * amount of time has passed since the last display refresh.
 *
 * @param min_interval_ms Minimum interval in milliseconds between operations
 */
void vdma_ensure_display_refresh(uint32_t min_interval_ms);

#ifdef __cplusplus
}
#endif

#endif // VDMA_PROTECTION_H