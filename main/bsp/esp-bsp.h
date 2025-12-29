#ifndef BSP_ESP_BSP_H_SHIM
#define BSP_ESP_BSP_H_SHIM

#ifdef __SIMULATOR_BUILD__
    #include "../simulator/mocks/bsp_mock.h"
#else
    #include "esp_err.h"

    #ifdef __cplusplus
    extern "C" {
    #endif

    typedef struct {
        uint32_t buffer_size;
        bool double_buffer;
    } bsp_display_cfg_t;

    #ifdef __cplusplus
    }
    #endif
#endif

#endif
