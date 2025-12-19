/**
 * @file ek79007_stable_config.h
 * @brief Stable EK79007 configuration for SD card operations without flickering
 *
 * This configuration reduces the DPI clock frequency to prevent DMA bandwidth
 * contention between MIPI-DSI display controller and SD card controller.
 */

#ifndef EK79007_STABLE_CONFIG_H
#define EK79007_STABLE_CONFIG_H

#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STABLE MIPI DPI configuration structure for EK79007
 *
 * This configuration prioritizes display stability over maximum refresh rate
 * to prevent flickering during SD card operations.
 *
 * DPI clock reduced from 52MHz to 30MHz for stable SD card operations:
 * - Original: 52MHz (causes DMA bandwidth contention with SD card)
 * - Stable: 30MHz (prevents flickering during SD card operations)
 *
 * @note  refresh_rate = (dpi_clock_freq_mhz * 1000000) / (h_res + hsync_pulse_width + hsync_back_porch + hsync_front_porch)
 *                                                      / (v_res + vsync_pulse_width + vsync_back_porch + vsync_front_porch)
 *
 * With 30MHz DPI clock: ~35Hz refresh rate (stable, no flicker)
 * With 52MHz DPI clock: ~60Hz refresh rate (high refresh, but flickers with SD card)
 */
#define EK79007_1024_600_PANEL_STABLE_CONFIG(px_format)            \
    {                                                            \
        .virtual_channel = 0,                                    \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,             \
        .dpi_clock_freq_mhz = 30,                                /* STABLE: Reduced from 52MHz to prevent flickering */ \
        .pixel_format = px_format,                               \
        .num_fbs = 1,                                            \
        .video_timing = {                                        \
            .h_size = 1024,                                      \
            .v_size = 600,                                       \
            .hsync_pulse_width = 10,                             \
            .hsync_back_porch = 160,                             \
            .hsync_front_porch = 160,                            \
            .vsync_pulse_width = 1,                              \
            .vsync_back_porch = 23,                              \
            .vsync_front_porch = 12,                             \
        },                                                       \
        .flags = {                                               \
            .use_dma2d = true,                                   \
            .fb_in_psram = false,                                /* CRITICAL: Keep framebuffer in IRAM */ \
        },                                                       \
    }

/**
 * @brief STABLE MIPI DSI bus configuration with reduced bit rate
 *
 * Reduces DSI bit rate from 900Mbps to 600Mbps for additional stability
 */
#define EK79007_PANEL_BUS_DSI_STABLE_CONFIG()                   \
    {                                                           \
        .bus_id = 0,                                           \
        .num_data_lanes = 2,                                   \
        .phy_clk_src = 0,                                      \
        .lane_bit_rate_mbps = 600,                             /* STABLE: Reduced from 900Mbps */ \
    }

#ifdef __cplusplus
}
#endif

#endif // EK79007_STABLE_CONFIG_H