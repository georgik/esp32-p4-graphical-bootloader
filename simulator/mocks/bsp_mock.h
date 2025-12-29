/**
 * @file bsp_mock.h
 * @brief Mock BSP (Board Support Package) declarations
 */

#ifndef BSP_MOCK_H
#define BSP_MOCK_H

#include "esp_system_mock.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// BSP display configuration
typedef struct {
    struct {
        uint32_t phy_clk_src;
        uint32_t lane_bit_rate_mbps;
    } dsi_bus;
    uint32_t hdmi_resolution;
    void* lvgl_port_cfg;
    uint32_t buffer_size;
    bool double_buffer;
    struct {
        bool buff_dma;
        bool buff_spiram;
        bool sw_rotate;
    } flags;
} bsp_display_cfg_t;

// Forward declaration for LVGL display (don't define it here, LVGL does)
struct lv_display_t;

// BSP functions
struct lv_display_t* bsp_display_start_with_config(const bsp_display_cfg_t* cfg);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);
void bsp_set_active_display(struct lv_display_t* display);

// ESP LCD types (simplified)
typedef void* esp_lcd_panel_handle_t;

// SD card types
typedef struct sdmmc_card_t sdmmc_card_t;

// SD card functions
esp_err_t bsp_sdcard_mount(void);
esp_err_t bsp_sdcard_unmount(void);
sdmmc_card_t* bsp_sdcard_get_handle(void);

// Board initialization
esp_err_t board_init_display(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_MOCK_H
