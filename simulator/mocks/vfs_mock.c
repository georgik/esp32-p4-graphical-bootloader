/**
 * @file vfs_mock.c
 * @brief VFS path translation implementation for simulator
 */

#ifdef __SIMULATOR_BUILD__

#include "vfs_mock.h"
#include "esp_log_mock.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "vfs_mock";

/**
 * @brief Translate ESP-IDF VFS path to simulator filesystem path
 *
 * Translates paths like:
 *   /sdcard/firmwares -> sdcard/firmwares
 *   /sdcard/xyz       -> sdcard/xyz
 *
 * Leaves other paths unchanged
 */
const char* vfs_translate_path(const char* esp_path) {
    if (!esp_path) {
        return NULL;
    }

    // Static buffer for translated path
    static char translated_path[512];

    ESP_LOGI(TAG, "vfs_translate_path called with: '%s' (len=%zu)", esp_path, strlen(esp_path));

    // Check if it starts with /sdcard (length 7, not 8!)
    if (strlen(esp_path) >= 7 && strncmp(esp_path, "/sdcard", 7) == 0) {
        // Translate /sdcard/... to sdcard/...
        const char* rest = esp_path + 7;  // Skip "/sdcard"

        // If rest starts with /, skip it too
        if (rest[0] == '/') {
            rest++;
        }

        // Build translated path: sdcard/{rest}
        snprintf(translated_path, sizeof(translated_path), "sdcard/%s", rest);

        ESP_LOGI(TAG, "VFS path translation: '%s' -> '%s'", esp_path, translated_path);
        return translated_path;
    }

    // No translation needed, return original
    ESP_LOGD(TAG, "VFS path not translated: '%s'", esp_path);
    return esp_path;
}

#endif // __SIMULATOR_BUILD__
