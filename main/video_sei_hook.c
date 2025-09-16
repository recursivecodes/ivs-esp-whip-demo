/* Video SEI Hook Implementation
 * 
 * Provides hooks to intercept video frames and inject SEI NAL units
 * before they are sent via WebRTC
 */

#include "video_sei_hook.h"
#include "sei.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <inttypes.h>

static const char *TAG = "VIDEO_SEI_HOOK";

typedef struct {
    bool initialized;
    video_frame_processor_t custom_processor;
    void *user_ctx;
    SemaphoreHandle_t mutex;
    
    // Statistics
    uint32_t frames_processed;
    uint32_t sei_units_inserted;
    uint32_t total_sei_bytes;
} video_sei_hook_t;

static video_sei_hook_t g_hook = {0};

/**
 * @brief Default SEI frame processor using the test SEI publisher
 */
static bool default_sei_processor(const uint8_t *frame_data, size_t frame_size,
                                 uint8_t **output_data, size_t *output_size,
                                 void *user_ctx) {
    sei_publisher_handle_t publisher = sei_get_publisher();
    if (!publisher) {
        // No SEI publisher available, just copy frame as-is
        *output_data = malloc(frame_size);
        if (!*output_data) {
            return false;
        }
        memcpy(*output_data, frame_data, frame_size);
        *output_size = frame_size;
        return true;
    }
    
    // Process frame with SEI publisher
    return sei_publisher_process_frame(publisher, frame_data, frame_size, output_data, output_size);
}

bool video_sei_hook_init(void) {
    if (g_hook.initialized) {
        ESP_LOGW(TAG, "Video SEI hook already initialized");
        return true;
    }
    
    memset(&g_hook, 0, sizeof(g_hook));
    
    g_hook.mutex = xSemaphoreCreateMutex();
    if (!g_hook.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }
    
    // Set default processor
    g_hook.custom_processor = default_sei_processor;
    g_hook.user_ctx = NULL;
    g_hook.initialized = true;
    
    ESP_LOGI(TAG, "✅ Video SEI hook initialized");
    return true;
}

void video_sei_hook_deinit(void) {
    if (!g_hook.initialized) {
        return;
    }
    
    if (g_hook.mutex) {
        vSemaphoreDelete(g_hook.mutex);
        g_hook.mutex = NULL;
    }
    
    memset(&g_hook, 0, sizeof(g_hook));
    ESP_LOGI(TAG, "✅ Video SEI hook deinitialized");
}

void video_sei_hook_set_processor(video_frame_processor_t processor, void *user_ctx) {
    if (!g_hook.initialized) {
        ESP_LOGE(TAG, "Video SEI hook not initialized");
        return;
    }
    
    if (xSemaphoreTake(g_hook.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return;
    }
    
    g_hook.custom_processor = processor ? processor : default_sei_processor;
    g_hook.user_ctx = user_ctx;
    
    ESP_LOGI(TAG, "📹 Set custom video frame processor: %p", processor);
    
    xSemaphoreGive(g_hook.mutex);
}

bool video_sei_hook_process_frame(const uint8_t *frame_data, size_t frame_size,
                                 uint8_t **output_data, size_t *output_size) {
    if (!g_hook.initialized || !frame_data || !output_data || !output_size) {
        return false;
    }
    
    if (xSemaphoreTake(g_hook.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        // If we can't get the mutex quickly, just copy frame as-is
        *output_data = malloc(frame_size);
        if (!*output_data) {
            return false;
        }
        memcpy(*output_data, frame_data, frame_size);
        *output_size = frame_size;
        return true;
    }
    
    bool result = false;
    size_t original_size = frame_size;
    
    if (g_hook.custom_processor) {
        result = g_hook.custom_processor(frame_data, frame_size, output_data, output_size, g_hook.user_ctx);
        
        if (result) {
            // Update statistics
            g_hook.frames_processed++;
            
            if (*output_size > original_size) {
                // SEI data was added
                uint32_t sei_bytes_added = *output_size - original_size;
                g_hook.total_sei_bytes += sei_bytes_added;
                g_hook.sei_units_inserted++;
                
                ESP_LOGD(TAG, "📹 Frame processed: %zu -> %zu bytes (+%" PRIu32 " SEI bytes)", 
                         original_size, *output_size, sei_bytes_added);
            }
        }
    } else {
        // No processor, just copy frame as-is
        *output_data = malloc(frame_size);
        if (*output_data) {
            memcpy(*output_data, frame_data, frame_size);
            *output_size = frame_size;
            result = true;
        }
    }
    
    xSemaphoreGive(g_hook.mutex);
    return result;
}

void video_sei_hook_get_stats(uint32_t *frames_processed, uint32_t *sei_units_inserted, uint32_t *total_sei_bytes) {
    if (!g_hook.initialized) {
        if (frames_processed) *frames_processed = 0;
        if (sei_units_inserted) *sei_units_inserted = 0;
        if (total_sei_bytes) *total_sei_bytes = 0;
        return;
    }
    
    if (xSemaphoreTake(g_hook.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for stats");
        return;
    }
    
    if (frames_processed) *frames_processed = g_hook.frames_processed;
    if (sei_units_inserted) *sei_units_inserted = g_hook.sei_units_inserted;
    if (total_sei_bytes) *total_sei_bytes = g_hook.total_sei_bytes;
    
    xSemaphoreGive(g_hook.mutex);
}

void video_sei_hook_reset_stats(void) {
    if (!g_hook.initialized) {
        return;
    }
    
    if (xSemaphoreTake(g_hook.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for stats reset");
        return;
    }
    
    g_hook.frames_processed = 0;
    g_hook.sei_units_inserted = 0;
    g_hook.total_sei_bytes = 0;
    
    ESP_LOGI(TAG, "📊 Statistics reset");
    
    xSemaphoreGive(g_hook.mutex);
}