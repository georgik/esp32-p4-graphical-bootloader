/**
 * @file nvs_mock.c
 * @brief Mock implementation of NVS using JSON file storage
 */

#include "nvs_mock.h"
#include "esp_log_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef JSON_C_VER
// Use a simple key-value store if json-c is not available
#define USE_SIMPLE_NVS 1
#else
#include <json-c/json.h>
#define USE_SIMPLE_NVS 0
#endif

static const char* TAG = "nvs_mock";
#define NVS_FILE ".esp32-simulator/nvs.json"

#if USE_SIMPLE_NVS
// Simple key-value file-based storage
typedef struct {
    char key[128];
    char value[512];
} nvs_entry_t;

static nvs_entry_t* nvs_entries = NULL;
static size_t nvs_entry_count = 0;
static size_t nvs_entry_capacity = 0;

static void nvs_load(void) {
    FILE* f = fopen(NVS_FILE, "r");
    if (!f) {
        nvs_entry_capacity = 100;
        nvs_entries = calloc(nvs_entry_capacity, sizeof(nvs_entry_t));
        return;
    }

    // Count entries
    char line[1024];
    nvs_entry_count = 0;
    while (fgets(line, sizeof(line), f)) {
        nvs_entry_count++;
    }
    rewind(f);

    // Allocate storage
    nvs_entry_capacity = nvs_entry_count + 100;
    nvs_entries = calloc(nvs_entry_capacity, sizeof(nvs_entry_t));

    // Load entries
    size_t i = 0;
    while (fgets(line, sizeof(line), f) && i < nvs_entry_count) {
        char* equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            strncpy(nvs_entries[i].key, line, 127);
            strncpy(nvs_entries[i].value, equals + 1, 511);
            // Remove newline
            char* newline = strchr(nvs_entries[i].value, '\n');
            if (newline) *newline = '\0';
            i++;
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Loaded %zu NVS entries", nvs_entry_count);
}

static void nvs_save(void) {
    FILE* f = fopen(NVS_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to save NVS");
        return;
    }

    for (size_t i = 0; i < nvs_entry_count; i++) {
        fprintf(f, "%s=%s\n", nvs_entries[i].key, nvs_entries[i].value);
    }

    fclose(f);
}

static const char* nvs_get_full_key(nvs_handle_t handle, const char* key) {
    static char full_key[256];
    // Handle is actually the namespace string pointer
    const char* namespace_name = (const char*)handle;
    snprintf(full_key, sizeof(full_key), "%s.%s", namespace_name, key);
    return full_key;
}

esp_err_t nvs_flash_init(void) {
    mkdir(".esp32-simulator", 0755);
    nvs_load();
    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
    nvs_entry_count = 0;
    if (nvs_entries) {
        free(nvs_entries);
        nvs_entries = NULL;
    }
    remove(NVS_FILE);
    ESP_LOGI(TAG, "NVS erased");
    return ESP_OK;
}

esp_err_t nvs_flash_deinit(void) {
    // Save current state before deinitializing
    nvs_save();
    if (nvs_entries) {
        free(nvs_entries);
        nvs_entries = NULL;
    }
    nvs_entry_count = 0;
    nvs_entry_capacity = 0;
    ESP_LOGI(TAG, "NVS deinitialized");
    return ESP_OK;
}

esp_err_t nvs_open(const char* namespace_name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle) {
    *out_handle = (nvs_handle_t)namespace_name;
    ESP_LOGD(TAG, "NVS opened namespace: %s", namespace_name);
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    ESP_LOGD(TAG, "NVS closed");
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value) {
    const char* full_key = nvs_get_full_key(handle, key);

    // Check if entry exists
    for (size_t i = 0; i < nvs_entry_count; i++) {
        if (strcmp(nvs_entries[i].key, full_key) == 0) {
            snprintf(nvs_entries[i].value, sizeof(nvs_entries[i].value), "%u", (unsigned int)value);
            nvs_save();
            return ESP_OK;
        }
    }

    // Add new entry
    if (nvs_entry_count >= nvs_entry_capacity) {
        nvs_entry_capacity += 100;
        nvs_entries = realloc(nvs_entries, nvs_entry_capacity * sizeof(nvs_entry_t));
    }

    strncpy(nvs_entries[nvs_entry_count].key, full_key, 127);
    snprintf(nvs_entries[nvs_entry_count].value, sizeof(nvs_entries[nvs_entry_count].value), "%u", (unsigned int)value);
    nvs_entry_count++;
    nvs_save();

    ESP_LOGD(TAG, "NVS set: %s = %u", full_key, (unsigned int)value);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value) {
    const char* full_key = nvs_get_full_key(handle, key);

    for (size_t i = 0; i < nvs_entry_count; i++) {
        if (strcmp(nvs_entries[i].key, full_key) == 0) {
            *out_value = (uint32_t)atoi(nvs_entries[i].value);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) {
    const char* full_key = nvs_get_full_key(handle, key);

    for (size_t i = 0; i < nvs_entry_count; i++) {
        if (strcmp(nvs_entries[i].key, full_key) == 0) {
            strncpy(nvs_entries[i].value, value, 511);
            nvs_save();
            return ESP_OK;
        }
    }

    if (nvs_entry_count >= nvs_entry_capacity) {
        nvs_entry_capacity += 100;
        nvs_entries = realloc(nvs_entries, nvs_entry_capacity * sizeof(nvs_entry_t));
    }

    strncpy(nvs_entries[nvs_entry_count].key, full_key, 127);
    strncpy(nvs_entries[nvs_entry_count].value, value, 511);
    nvs_entry_count++;
    nvs_save();

    ESP_LOGD(TAG, "NVS set: %s = %s", full_key, value);
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length) {
    const char* full_key = nvs_get_full_key(handle, key);

    for (size_t i = 0; i < nvs_entry_count; i++) {
        if (strcmp(nvs_entries[i].key, full_key) == 0) {
            size_t len = strlen(nvs_entries[i].value) + 1;
            if (out_value) {
                if (*length < len) {
                    return ESP_ERR_INVALID_SIZE;
                }
                strcpy(out_value, nvs_entries[i].value);
            }
            *length = len;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) {
    const char* full_key = nvs_get_full_key(handle, key);

    for (size_t i = 0; i < nvs_entry_count; i++) {
        if (strcmp(nvs_entries[i].key, full_key) == 0) {
            // Remove entry by shifting
            memmove(&nvs_entries[i], &nvs_entries[i + 1],
                    (nvs_entry_count - i - 1) * sizeof(nvs_entry_t));
            nvs_entry_count--;
            nvs_save();
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    // Erase all entries by clearing the array
    (void)handle;  // Unused
    nvs_entry_count = 0;
    if (nvs_entries) {
        free(nvs_entries);
        nvs_entries = NULL;
        nvs_entry_capacity = 0;
    }
    nvs_save();
    ESP_LOGI(TAG, "NVS erased all");
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;  // Unused parameter
    nvs_save();
    return ESP_OK;
}

// Stub implementations for other types
esp_err_t nvs_set_i8(nvs_handle_t h, const char* k, int8_t v) { return nvs_set_u32(h, k, v); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char* k, uint8_t v) { return nvs_set_u32(h, k, v); }
esp_err_t nvs_set_i16(nvs_handle_t h, const char* k, int16_t v) { return nvs_set_u32(h, k, v); }
esp_err_t nvs_set_u16(nvs_handle_t h, const char* k, uint16_t v) { return nvs_set_u32(h, k, v); }
esp_err_t nvs_set_i32(nvs_handle_t h, const char* k, int32_t v) { return nvs_set_u32(h, k, v); }
esp_err_t nvs_set_i64(nvs_handle_t h, const char* k, int64_t v) { return nvs_set_u32(h, k, (uint32_t)v); }
esp_err_t nvs_set_u64(nvs_handle_t h, const char* k, uint64_t v) { return nvs_set_u32(h, k, (uint32_t)v); }
esp_err_t nvs_set_blob(nvs_handle_t h, const char* k, const void* v, size_t l) { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t nvs_get_i8(nvs_handle_t h, const char* k, int8_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (int8_t)tmp; return e; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char* k, uint8_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (uint8_t)tmp; return e; }
esp_err_t nvs_get_i16(nvs_handle_t h, const char* k, int16_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (int16_t)tmp; return e; }
esp_err_t nvs_get_u16(nvs_handle_t h, const char* k, uint16_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (uint16_t)tmp; return e; }
esp_err_t nvs_get_i32(nvs_handle_t h, const char* k, int32_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (int32_t)tmp; return e; }
esp_err_t nvs_get_i64(nvs_handle_t h, const char* k, int64_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (int64_t)tmp; return e; }
esp_err_t nvs_get_u64(nvs_handle_t h, const char* k, uint64_t* v) { uint32_t tmp; esp_err_t e = nvs_get_u32(h, k, &tmp); *v = (uint64_t)tmp; return e; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char* k, void* v, size_t* l) { return ESP_ERR_NOT_SUPPORTED; }

#endif // USE_SIMPLE_NVS
