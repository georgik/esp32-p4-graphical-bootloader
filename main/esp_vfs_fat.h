/**
 * @file esp_vfs_fat.h
 * @brief Shim for FAT filesystem VFS
 */

#ifndef ESP_VFS_FAT_H_SHIM
#define ESP_VFS_FAT_H_SHIM

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct sdmmc_card_t sdmmc_card_t;

typedef struct {
    uint32_t max_files;
    uint32_t allocation_unit_size;
} esp_vfs_fat_mount_config_t;

esp_err_t esp_vfs_fat_sdmmc_mount(const char* base_path,
                                   const sdmmc_card_t* card,
                                   const esp_vfs_fat_mount_config_t* mount_config,
                                   sdmmc_card_t** out_card);

esp_err_t esp_vfs_fat_sdcard_unmount(const char* base_path,
                                      const sdmmc_card_t* card);

#ifdef __cplusplus
}
#endif

#endif // ESP_VFS_FAT_H_SHIM
