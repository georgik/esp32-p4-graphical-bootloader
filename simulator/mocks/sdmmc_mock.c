/**
 * @file sdmmc_mock.c
 * @brief Mock implementation of SDMMC functions
 */

#include "sdmmc_cmd.h"
#include "esp_log_mock.h"
#include <stdio.h>

static const char* TAG = "sdmmc_mock";

void sdmmc_card_print_info(FILE* stream, const struct sdmmc_card* card) {
    (void)card;

    if (stream == NULL) {
        stream = stdout;
    }

    fprintf(stream, "Mock SD Card Information (simulator)\n");
    ESP_LOGI(TAG, "SD card info requested (simulator mode)");
}
