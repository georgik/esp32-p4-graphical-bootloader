#ifndef SOC_LP_SYSTEM_REG_H_SHIM
#define SOC_LP_SYSTEM_REG_H_SHIM
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// RTC register for boot requests
#define LP_SYSTEM_REG_LP_STORE0_REG  (0x600B7000)
#define LP_SYS_REG_OP_HOLD          (1 << 6)
#ifdef __cplusplus
}
#endif
#endif
