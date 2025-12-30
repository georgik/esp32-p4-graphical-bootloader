/**
 * @file nvs.h
 * @brief NVS definitions
 */

#ifndef NVS_H_MOCK
#define NVS_H_MOCK

#ifdef __SIMULATOR_BUILD__
    #include "nvs_mock.h"
#else
    #include_next "nvs.h"
#endif

#endif // NVS_H_MOCK
