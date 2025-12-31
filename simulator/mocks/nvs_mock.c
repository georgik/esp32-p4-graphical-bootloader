/**
 * @file nvs_mock.c
 * @brief Mock implementation of NVS using flash emulator
 *         NVS is stored in the NVS partition at 0x9000 (simulating real device behavior)
 */

#include "nvs_mock.h"
#include "esp_log_mock.h"
#include "esp_partition_mock.h"
#include "../platform/flash_emulator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <json-c/json.h>

static const char* TAG = "nvs_mock";

// NVS partition details (from esp_partition_mock.c)
#define NVS_PARTITION_ADDRESS  0x9000
#define NVS_PARTITION_SIZE     0x6000  // 24KB

// In-memory JSON object for fast access (flash is kept in sync)
static json_object* nvs_root = NULL;
static bool nvs_dirty = false;

// Helper to read NVS from flash partition
static esp_err_t nvs_read_from_flash(void) {
    if (nvs_root != NULL) {
        return ESP_OK;  // Already loaded
    }

    // Read NVS partition from flash emulator
    uint8_t* buffer = malloc(NVS_PARTITION_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for NVS");
        return ESP_ERR_NO_MEM;
    }

    // Initialize buffer to 0xFF (erased flash state)
    memset(buffer, 0xFF, NVS_PARTITION_SIZE);

    // Read from flash emulator
    esp_err_t ret = flash_emulator_read(NVS_PARTITION_ADDRESS, buffer, NVS_PARTITION_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read NVS from flash: %s", esp_err_to_name(ret));
        free(buffer);
        return ret;
    }

    // Check if NVS contains valid data (not all 0xFF)
    bool is_erased = true;
    for (size_t i = 0; i < NVS_PARTITION_SIZE; i++) {
        if (buffer[i] != 0xFF) {
            is_erased = false;
            break;
        }
    }

    if (!is_erased) {
        // Parse JSON from flash
        // Find null terminator
        size_t json_len = 0;
        for (size_t i = 0; i < NVS_PARTITION_SIZE; i++) {
            if (buffer[i] == '\0') {
                json_len = i;
                break;
            }
        }

        if (json_len > 0) {
            nvs_root = json_tokener_parse((char*)buffer);
            if (nvs_root) {
                ESP_LOGI(TAG, "Loaded NVS from flash partition @ 0x%X (%zu bytes)",
                         NVS_PARTITION_ADDRESS, json_len);
            } else {
                ESP_LOGW(TAG, "Failed to parse NVS JSON from flash, creating new");
                nvs_root = json_object_new_object();
            }
        } else {
            ESP_LOGW(TAG, "No null terminator found in NVS data");
            nvs_root = json_object_new_object();
        }
    } else {
        ESP_LOGI(TAG, "NVS partition is erased, creating new storage");
        nvs_root = json_object_new_object();
    }

    free(buffer);
    nvs_dirty = false;
    return ESP_OK;
}

// Helper to write NVS to flash partition
static esp_err_t nvs_write_to_flash(void) {
    if (!nvs_root) {
        return ESP_ERR_INVALID_STATE;
    }

    // Convert JSON to string
    const char* json_str = json_object_to_json_string_ext(nvs_root, JSON_C_TO_STRING_PRETTY);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize NVS JSON");
        return ESP_FAIL;
    }

    size_t json_len = strlen(json_str) + 1;  // Include null terminator

    if (json_len > NVS_PARTITION_SIZE) {
        ESP_LOGE(TAG, "NVS data too large: %zu bytes (max %u bytes)",
                 json_len, NVS_PARTITION_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Erase NVS partition first
    esp_err_t ret = flash_emulator_erase(NVS_PARTITION_ADDRESS, NVS_PARTITION_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS partition: %s", esp_err_to_name(ret));
        return ret;
    }

    // Write JSON to flash
    ret = flash_emulator_write(NVS_PARTITION_ADDRESS, json_str, json_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write NVS to flash: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "NVS saved to flash partition @ 0x%X (%zu bytes)",
             NVS_PARTITION_ADDRESS, json_len);

    nvs_dirty = false;
    return ESP_OK;
}

esp_err_t nvs_flash_init(void) {
    // Create flash emulator directory if needed
    mkdir(".esp32-simulator", 0755);

    // Load NVS from flash partition
    return nvs_read_from_flash();
}

esp_err_t nvs_flash_erase(void) {
    if (nvs_root) {
        json_object_put(nvs_root);
        nvs_root = NULL;
    }

    // Erase NVS partition in flash
    esp_err_t ret = flash_emulator_erase(NVS_PARTITION_ADDRESS, NVS_PARTITION_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS partition: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "NVS partition erased @ 0x%X", NVS_PARTITION_ADDRESS);
    nvs_dirty = false;
    return ESP_OK;
}

esp_err_t nvs_flash_deinit(void) {
    // Commit any pending changes before deinitializing
    if (nvs_dirty && nvs_root) {
        nvs_write_to_flash();
    }

    if (nvs_root) {
        json_object_put(nvs_root);
        nvs_root = NULL;
    }

    return ESP_OK;
}

esp_err_t nvs_open(const char* namespace_name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle) {
    (void)open_mode;  // Unused for now

    if (!namespace_name || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    // Ensure NVS is loaded
    if (!nvs_root) {
        esp_err_t ret = nvs_read_from_flash();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    // Create namespace object if it doesn't exist
    if (!json_object_object_get_ex(nvs_root, namespace_name, NULL)) {
        json_object* namespace_obj = json_object_new_object();
        json_object_object_add(nvs_root, namespace_name, namespace_obj);
        nvs_dirty = true;
    }

    // Return the namespace string as handle
    *out_handle = (nvs_handle_t)strdup(namespace_name);

    ESP_LOGD(TAG, "Opened NVS namespace: %s", namespace_name);
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    if (handle) {
        free((void*)handle);  // Free the duplicated namespace string
    }
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
    if (!handle || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;

    // Get namespace object
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        namespace_obj = json_object_new_object();
        json_object_object_add(nvs_root, namespace_name, namespace_obj);
    }

    // Set string value
    json_object_object_add(namespace_obj, key, json_object_new_string(value));
    nvs_dirty = true;

    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        namespace_obj = json_object_new_object();
        json_object_object_add(nvs_root, namespace_name, namespace_obj);
    }

    json_object_object_add(namespace_obj, key, json_object_new_int(value));
    nvs_dirty = true;
    return ESP_OK;
}

esp_err_t nvs_set_u16(nvs_handle_t handle, const char* key, uint16_t value) {
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        namespace_obj = json_object_new_object();
        json_object_object_add(nvs_root, namespace_name, namespace_obj);
    }

    json_object_object_add(namespace_obj, key, json_object_new_int(value));
    nvs_dirty = true;
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value) {
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        namespace_obj = json_object_new_object();
        json_object_object_add(nvs_root, namespace_name, namespace_obj);
    }

    json_object_object_add(namespace_obj, key, json_object_new_int64(value));
    nvs_dirty = true;
    return ESP_OK;
}

esp_err_t nvs_set_u64(nvs_handle_t handle, const char* key, uint64_t value) {
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        namespace_obj = json_object_new_object();
        json_object_object_add(nvs_root, namespace_name, namespace_obj);
    }

    json_object_object_add(namespace_obj, key, json_object_new_int64(value));
    nvs_dirty = true;
    return ESP_OK;
}

esp_err_t nvs_set_i8(nvs_handle_t handle, const char* key, int8_t value) {
    return nvs_set_u8(handle, key, (uint8_t)value);
}

esp_err_t nvs_set_i16(nvs_handle_t handle, const char* key, int16_t value) {
    return nvs_set_u16(handle, key, (uint16_t)value);
}

esp_err_t nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value) {
    return nvs_set_u32(handle, key, (uint32_t)value);
}

esp_err_t nvs_set_i64(nvs_handle_t handle, const char* key, int64_t value) {
    return nvs_set_u64(handle, key, (uint64_t)value);
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length) {
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    json_object* value_obj = NULL;
    if (!json_object_object_get_ex(namespace_obj, key, &value_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    const char* value = json_object_get_string(value_obj);
    if (!value) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    if (out_value && length) {
        strncpy(out_value, value, *length - 1);
        out_value[*length - 1] = '\0';
    }

    if (length) {
        *length = strlen(value) + 1;
    }

    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value) {
    if (!handle || !key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    json_object* value_obj = NULL;
    if (!json_object_object_get_ex(namespace_obj, key, &value_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    *out_value = (uint8_t)json_object_get_int(value_obj);
    return ESP_OK;
}

esp_err_t nvs_get_u16(nvs_handle_t handle, const char* key, uint16_t* out_value) {
    if (!handle || !key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    json_object* value_obj = NULL;
    if (!json_object_object_get_ex(namespace_obj, key, &value_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    *out_value = (uint16_t)json_object_get_int(value_obj);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value) {
    if (!handle || !key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    json_object* value_obj = NULL;
    if (!json_object_object_get_ex(namespace_obj, key, &value_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    *out_value = (uint32_t)json_object_get_int64(value_obj);
    return ESP_OK;
}

esp_err_t nvs_get_u64(nvs_handle_t handle, const char* key, uint64_t* out_value) {
    if (!handle || !key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    json_object* value_obj = NULL;
    if (!json_object_object_get_ex(namespace_obj, key, &value_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    *out_value = (uint64_t)json_object_get_int64(value_obj);
    return ESP_OK;
}

esp_err_t nvs_get_i8(nvs_handle_t handle, const char* key, int8_t* out_value) {
    return nvs_get_u8(handle, key, (uint8_t*)out_value);
}

esp_err_t nvs_get_i16(nvs_handle_t handle, const char* key, int16_t* out_value) {
    return nvs_get_u16(handle, key, (uint16_t*)out_value);
}

esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value) {
    return nvs_get_u32(handle, key, (uint32_t*)out_value);
}

esp_err_t nvs_get_i64(nvs_handle_t handle, const char* key, int64_t* out_value) {
    return nvs_get_u64(handle, key, (uint64_t*)out_value);
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
    if (!handle || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    json_object_object_del(namespace_obj, key);
    nvs_dirty = true;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* namespace_name = (const char*)handle;
    json_object* namespace_obj = NULL;
    if (!json_object_object_get_ex(nvs_root, namespace_name, &namespace_obj)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    // Remove all keys from namespace
    json_object_object_foreach(namespace_obj, key, val) {
        json_object_object_del(namespace_obj, key);
    }

    nvs_dirty = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;  // Unused

    // Only write to flash if data has changed
    if (nvs_dirty) {
        return nvs_write_to_flash();
    }

    return ESP_OK;
}
