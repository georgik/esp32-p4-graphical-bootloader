/**
 * @file esp_vfs_fat.h
 * @brief ESP VFS FAT definitions
 */

#ifndef ESP_VFS_FAT_H_MOCK
#define ESP_VFS_FAT_H_MOCK

#ifdef __SIMULATOR_BUILD__
    // Mock - not implemented in simulator
    #include "esp_system_mock.h"
#else
    #include_next "esp_vfs_fat.h"
#endif

#endif // ESP_VFS_FAT_H_MOCK
