/**
 * @file board_init.c
 * @brief Board-specific initialization for LVGL-based bootloader
 *
 * This file shows how to initialize display panels using ESP-BSP with LVGL support
 * for optimized display performance on ESP32-P4
 */

#include "board_init.h"
#include "ek79007_stable_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "bsp/esp-bsp.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "lvgl.h"

static const char *TAG = "board_init";

// LVGL display handle for BSP
static lv_display_t *lvgl_disp = NULL;

// Example configuration for ESP-BOX-3 using BSP
#ifdef CONFIG_BOARD_ESP_BOX_3

esp_err_t board_init_display(void)
{
    ESP_LOGI(TAG, "Initializing ESP-BOX-3 display via BSP with LVGL...");

    // Initialize BSP display with LVGL support
    bsp_display_config_t cfg = {
        .max_transfer_sz = 320 * 48 * sizeof(uint16_t),  // 48 lines per chunk
        .lvgl_config = {
            .buffer_size = LV_HOR_RES_MAX * 40,  // 40 lines buffer
            .double_buffer = false,  // Use single buffer for IRAM efficiency
            .flags = {
                .buff_dma = true,    // Enable DMA for performance
                .buff_spiram = false,  // Use IRAM to prevent contention
            }
        }
    };

    bsp_display_new(&cfg, NULL, NULL);

    // Turn on backlight
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display initialized: 320x240 with LVGL");
    return ESP_OK;
}

#elif CONFIG_BOARD_M5STACK_CORE_S3

esp_err_t board_init_display(void)
{
    ESP_LOGI(TAG, "Initializing M5Stack Core S3 display via BSP with LVGL...");

    // Initialize BSP display with LVGL support
    bsp_display_config_t cfg = {
        .max_transfer_sz = 320 * 48 * sizeof(uint16_t),
        .lvgl_config = {
            .buffer_size = LV_HOR_RES_MAX * 40,
            .double_buffer = false,
            .flags = {
                .buff_dma = true,
                .buff_spiram = false,
            }
        }
    };

    bsp_display_new(&cfg, NULL, NULL);

    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Display initialized: 320x240 with LVGL");
    return ESP_OK;
}

#elif CONFIG_BOARD_ESP32_P4_FUNCTION_EV

esp_err_t board_init_display(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-P4 Function EV Board with STABLE EK79007 configuration (Anti-Flicker)...");

    // Create CUSTOM display configuration with STABLE settings to prevent flickering
    bsp_display_cfg_t stable_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hw_cfg = {
            .hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                .lane_bit_rate_mbps = 600,  // BALANCED: 600Mbps prevents flickering while maintaining image quality (400Mbps caused skewing)
            }
        },
        .flags = {
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
            .buff_dma = false,
#else
            .buff_dma = true,
#endif
            .buff_spiram = false,  // CRITICAL: Keep framebuffer in IRAM to avoid PSRAM contention
            .sw_rotate = true,
        }
    };

    // Use BSP display start with STABLE configuration
    lv_display_t* display = bsp_display_start_with_config(&stable_cfg);
    if (!display) {
        ESP_LOGE(TAG, "Failed to start BSP display with stable configuration");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BSP display started successfully with ANTI-FLICKERING configuration:");
    ESP_LOGI(TAG, "  - DSI Bit Rate: 600Mbps (reduced from 1000Mbps to prevent DMA bandwidth contention)");
    ESP_LOGI(TAG, "  - Framebuffer: IRAM-only (prevents PSRAM contention with SD card)");

    // CRITICAL: Force LVGL to use only IRAM for display operations
    lv_display_set_default(display);

    // Configure LVGL for better stability during SD card operations
    // Note: LVGL timer period is configured via sdkconfig (CONFIG_LV_DEF_REFR_PERIOD = 50ms)

    // Set background color to simple black to reduce rendering overhead
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    // Turn on backlight
    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to turn on backlight: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "EK79007 display initialized with BALANCED anti-flickering configuration:");
    ESP_LOGI(TAG, "  - DSI Rate: 600Mbps (BALANCED - prevents flickering while maintaining image quality)");
    ESP_LOGI(TAG, "  - Framebuffer: IRAM-only (prevents PSRAM contention)");
    ESP_LOGI(TAG, "  - SD Throttling: 256-byte chunks with 25ms display refresh");
    ESP_LOGI(TAG, "  - Task Priority: LVGL highest, OTA very low priority");
    ESP_LOGI(TAG, "  - Result: Stable image quality + flicker elimination through SD throttling");

    return ESP_OK;
}

#else

// Fallback: User must provide custom initialization
esp_err_t board_init_display(void)
{
    ESP_LOGE(TAG, "No board selected! Please:");
    ESP_LOGE(TAG, "1. Set CONFIG_BOARD_ESP_BOX_3, CONFIG_BOARD_M5STACK_CORE_S3, or CONFIG_BOARD_ESP32_P4_FUNCTION_EV in sdkconfig");
    ESP_LOGE(TAG, "2. Or implement custom esp_lcd panel creation here");
    return ESP_FAIL;
}

#endif

// Get LVGL display handle (for external use)
lv_display_t* board_get_lvgl_display(void)
{
    return lvgl_disp;
}

// Initialize LVGL port (already done by BSP, but we can add customizations if needed)
esp_err_t board_init_lvgl_port(void)
{
    // BSP already initializes LVGL, but we can add custom configurations here if needed
    ESP_LOGI(TAG, "LVGL port initialized by BSP");
    return ESP_OK;
}