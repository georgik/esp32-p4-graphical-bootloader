/**
 * @file sdmmc_cmd.h
 * @brief SDMMC command definitions
 */

#ifndef SDMMC_CMD_H_MOCK
#define SDMMC_CMD_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    #include <stdio.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    // Forward declaration (typedef is defined elsewhere)
    struct sdmmc_card;

    // Mock function declaration
    void sdmmc_card_print_info(FILE* stream, const struct sdmmc_card* card);

    #ifdef __cplusplus
    }
    #endif

#else
    #include_next "sdmmc_cmd.h"
#endif

#endif // SDMMC_CMD_H_MOCK
