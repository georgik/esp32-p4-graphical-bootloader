/**
 * @file bsp_mock.c
 * @brief Mock BSP implementation - delegates to SDL2
 */

#include "bsp_mock.h"
#include "esp_log_mock.h"
#include "lvgl.h"
#include <SDL2/SDL.h>

static const char* TAG = "bsp_mock";

// Display will be initialized by lvgl_sdl_init
static struct lv_display_t* active_display = NULL;

void bsp_set_active_display(struct lv_display_t* display) {
    active_display = (struct lv_display_t*)display;
}

struct lv_display_t* bsp_display_start_with_config(const bsp_display_cfg_t* cfg) {
    ESP_LOGI(TAG, "Mock BSP display start (delegated to SDL2)");
    ESP_LOGI(TAG, "  Buffer size: %lu", (unsigned long)cfg->buffer_size);
    ESP_LOGI(TAG, "  Double buffer: %s", cfg->double_buffer ? "yes" : "no");
    ESP_LOGI(TAG, "  DMA: %s, SPIRAM: %s",
             cfg->flags.buff_dma ? "yes" : "no",
             cfg->flags.buff_spiram ? "yes" : "no");

    // Display should already be created by SDL2 init
    if (active_display) {
        ESP_LOGI(TAG, "BSP display using active SDL2 display");
        return (struct lv_display_t*)active_display;
    }

    ESP_LOGW(TAG, "No active display, call init_lvgl_sdl() first");
    return NULL;
}

esp_err_t bsp_display_backlight_on(void) {
    ESP_LOGI(TAG, "Mock backlight ON");
    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void) {
    ESP_LOGI(TAG, "Mock backlight OFF");
    return ESP_OK;
}

// SD card mock implementation
// Define the complete sdmmc_card_t structure
struct sdmmc_card_t {
    uint32_t capacity;
    char name[32];
};

static struct sdmmc_card_t* mock_sd_card = NULL;

esp_err_t bsp_sdcard_mount(void) {
    ESP_LOGI(TAG, "Mock SD card mount");
    // In simulator, create a mock SD card handle
    static struct sdmmc_card_t mock_card;
    mock_sd_card = &mock_card;
    mock_card.capacity = 32 * 1024 * 1024;  // 32GB
    snprintf(mock_card.name, sizeof(mock_card.name), "MockSD");
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(void) {
    ESP_LOGI(TAG, "Mock SD card unmount");
    mock_sd_card = NULL;
    return ESP_OK;
}

sdmmc_card_t* bsp_sdcard_get_handle(void) {
    return mock_sd_card;
}

// Board initialization function
esp_err_t board_init_display(void) {
    ESP_LOGI(TAG, "Mock board display init");
    // Display should already be initialized by SDL2
    return ESP_OK;
}
