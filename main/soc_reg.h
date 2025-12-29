/**
 * @file soc_reg.h
 * @brief Shim for register access
 */

#ifndef SOC_REG_H_SHIM
#define SOC_REG_H_SHIM

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register access macros
#define REG_WRITE(reg, val)  (*(volatile uint32_t*)(reg) = (uint32_t)(val))
#define REG_READ(reg)       (*(volatile uint32_t*)(reg))

#ifdef __cplusplus
}
#endif

#endif // SOC_REG_H_SHIM
