#ifndef DRIVER_SDMMC_HOST_H_SHIM
#define DRIVER_SDMMC_HOST_H_SHIM
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sdmmc_host_t {
    int slot;
    int width;
} sdmmc_host_t;
#ifdef __cplusplus
}
#endif
#endif
