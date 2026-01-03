/**
 * @file soc_reg.h
 * @brief SoC register definitions
 */

#ifndef SOC_REG_H_MOCK
#define SOC_REG_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "esp_system_mock.h"
    #include <stdint.h>

    #ifdef __cplusplus
    extern "C" {
    #endif

    // Mock register I/O for simulator
    #define REG_WRITE(reg, val) do { \
        (void)(reg); \
        (void)(val); \
    } while(0)

    #define REG_READ(reg) (0)

    #ifdef __cplusplus
    }
    #endif

#else
    #include_next "soc_reg.h"
#endif

#endif // SOC_REG_H_MOCK
