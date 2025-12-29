/**
 * @file sdmmc_cmd.h
 * @brief Shim for SDMMC commands
 */
#ifndef SDMMC_CMD_H_SHIM
#define SDMMC_CMD_H_SHIM

#ifdef __SIMULATOR_BUILD__
    #include <stdio.h>
    #include <stdint.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    // Define the complete sdmmc_card_t structure here
    struct sdmmc_card_t {
        uint32_t capacity;
        char name[32];
    };
    typedef struct sdmmc_card_t sdmmc_card_t;

    // Print SD card info
    static inline void sdmmc_card_print_info(FILE* stream, const sdmmc_card_t* card) {
        if (card) {
            fprintf(stream, "[Mock] SD Card: %s (%u GB)\n",
                    card->name, (unsigned int)(card->capacity / (1024 * 1024)));
        } else {
            fprintf(stream, "[Mock] SD Card: NULL\n");
        }
    }

    #ifdef __cplusplus
    }
    #endif

#else
    #include "esp_err.h"

    #ifdef __cplusplus
    extern "C" {
    #endif

    // Minimal SDMMC command types
    typedef enum {
        SDMMC_CMD_GO_IDLE_STATE = 0,
        SDMMC_CMD_ALL_SEND_CID = 2,
        // Add more as needed
    } sdmmc_command_t;

    #ifdef __cplusplus
    }
    #endif
#endif

#endif
