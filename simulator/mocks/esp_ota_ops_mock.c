/**
 * @file esp_ota_ops_mock.c
 * @brief Mock implementation of OTA operations using flash emulator
 */

#include "esp_ota_ops.h"
#include "esp_log_mock.h"
#include "../platform/flash_emulator.h"
#include "esp_partition_mock.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "esp_ota_ops_mock";

// OTA state tracking
typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t* partition;
    uint32_t offset;
    uint32_t total_size;
    bool active;
} ota_state_t;

// Track up to 4 simultaneous OTA operations
#define MAX_OTA_HANDLES 4
static ota_state_t ota_states[MAX_OTA_HANDLES];
static bool ota_initialized = false;

// Initialize OTA state tracking
static void init_ota_state(void) {
    if (!ota_initialized) {
        memset(ota_states, 0, sizeof(ota_states));
        ota_initialized = true;
    }
}

// Find OTA state by handle
static ota_state_t* find_ota_state(esp_ota_handle_t handle) {
    for (int i = 0; i < MAX_OTA_HANDLES; i++) {
        if (ota_states[i].active && ota_states[i].handle == handle) {
            return &ota_states[i];
        }
    }
    return NULL;
}

// Allocate new OTA handle
static ota_state_t* allocate_ota_state(void) {
    init_ota_state();

    for (int i = 0; i < MAX_OTA_HANDLES; i++) {
        if (!ota_states[i].active) {
            ota_states[i].active = true;
            ota_states[i].handle = (esp_ota_handle_t)(i + 1);
            ota_states[i].offset = 0;
            ota_states[i].total_size = 0;
            ota_states[i].partition = NULL;
            return &ota_states[i];
        }
    }
    return NULL;
}

esp_err_t esp_ota_begin(const esp_partition_t* partition, uint32_t update_size, esp_ota_handle_t* out_handle) {
    if (!partition || !out_handle) {
        ESP_LOGE(TAG, "Invalid arguments to esp_ota_begin");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🚀 OTA begin: partition=%s, update_size=%u bytes",
             partition->label, (unsigned int)update_size);

    // Check if partition has enough space
    if (update_size > partition->size) {
        ESP_LOGE(TAG, "Update size %u exceeds partition size %u",
                 (unsigned int)update_size, (unsigned int)partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Allocate OTA state
    ota_state_t* state = allocate_ota_state();
    if (!state) {
        ESP_LOGE(TAG, "No free OTA handles");
        return ESP_ERR_NO_MEM;
    }

    state->partition = partition;
    state->total_size = update_size;
    state->offset = 0;

    *out_handle = state->handle;

    ESP_LOGI(TAG, "✅ OTA begin successful: handle=%u", (unsigned int)state->handle);
    return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size) {
    ota_state_t* state = find_ota_state(handle);
    if (!state) {
        ESP_LOGE(TAG, "Invalid OTA handle: %u", (unsigned int)handle);
        return ESP_ERR_INVALID_ARG;
    }

    if (!data || size == 0) {
        ESP_LOGE(TAG, "Invalid data or size");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if write would exceed partition size
    if (state->offset + size > state->partition->size) {
        ESP_LOGE(TAG, "Write exceeds partition size: offset=%u + size=%zu > partition_size=%u",
                 state->offset, size, (unsigned int)state->partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Calculate absolute flash address
    uint32_t flash_addr = state->partition->address + state->offset;

    // Write to flash emulator
    esp_err_t ret = flash_emulator_write(flash_addr, data, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to flash: %s", esp_err_to_name(ret));
        return ret;
    }

    state->offset += size;

    // Log progress every 64KB
    if (state->offset % (64 * 1024) == 0 || state->offset == state->total_size) {
        ESP_LOGI(TAG, "📝 OTA progress: %u / %u bytes (%.1f%%)",
                 state->offset, state->total_size,
                 (state->total_size > 0) ? (state->offset * 100.0 / state->total_size) : 0.0);
    }

    return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    ota_state_t* state = find_ota_state(handle);
    if (!state) {
        ESP_LOGE(TAG, "Invalid OTA handle: %u", (unsigned int)handle);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "✅ OTA end: handle=%u, bytes_written=%u",
             (unsigned int)handle, state->offset);

    // Verify expected size was written
    if (state->total_size > 0 && state->offset != state->total_size) {
        ESP_LOGW(TAG, "⚠️  OTA size mismatch: expected %u, wrote %u",
                 state->total_size, state->offset);
        // Don't fail - allow partial writes for testing
    }

    // Mark OTA state as inactive
    state->active = false;

    return ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    ota_state_t* state = find_ota_state(handle);
    if (!state) {
        ESP_LOGE(TAG, "Invalid OTA handle: %u", (unsigned int)handle);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "❌ OTA abort: handle=%u, bytes_written=%u",
             (unsigned int)handle, state->offset);

    // Mark OTA state as inactive
    state->active = false;

    return ESP_OK;
}

const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start_from) {
    ESP_LOGD(TAG, "Getting next update partition (start_from=%p)", start_from);

    // If start_from is NULL, begin from the first OTA partition
    if (!start_from) {
        // Return ota_0 as default update partition
        const esp_partition_t* ota_0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_OTA_0,
            NULL
        );
        if (ota_0) {
            ESP_LOGI(TAG, "Next update partition: ota_0 @ 0x%x", (unsigned int)ota_0->address);
            return ota_0;
        }

        // Fallback to factory partition
        const esp_partition_t* factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_FACTORY,
            NULL
        );
        if (factory) {
            ESP_LOGI(TAG, "Next update partition: factory @ 0x%x", (unsigned int)factory->address);
            return factory;
        }

        ESP_LOGE(TAG, "No suitable update partition found");
        return NULL;
    }

    // Find next OTA partition after start_from
    const esp_partition_t* next = esp_partition_next(start_from);
    while (next != NULL) {
        if (next->type == ESP_PARTITION_TYPE_APP &&
            (next->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
             next->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ||
             next->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_2)) {
            ESP_LOGI(TAG, "Next update partition: %s @ 0x%x", next->label, (unsigned int)next->address);
            return next;
        }
        next = esp_partition_next(next);
    }

    ESP_LOGI(TAG, "No more OTA partitions available");
    return NULL;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
    if (!partition) {
        ESP_LOGE(TAG, "NULL partition");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🎯 Set boot partition: %s @ 0x%x",
             partition->label, (unsigned int)partition->address);

    // In a real implementation, this would write to OTA data partition
    // For simulator, we just log it
    // TODO: Store boot partition selection in NVS or flash

    return ESP_OK;
}
