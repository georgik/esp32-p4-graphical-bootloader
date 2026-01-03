/**
 * @file lp_system_reg.h
 * @brief LP system register definitions mock
 */

#ifndef SOC_LP_SYSTEM_REG_H_MOCK
#define SOC_LP_SYSTEM_REG_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include <stdint.h>

    // LP System register addresses (mocked, not used in simulator)
    #define LP_SYSTEM_REG_LP_STORE0_REG  (0x50005000)

    #define REG_WRITE(reg, val) do { \
        (void)(reg); \
        (void)(val); \
    } while(0)

    #define REG_READ(reg) (0)

#else
    #include_next "soc/lp_system_reg.h"
#endif

#endif // SOC_LP_SYSTEM_REG_H_MOCK
