/* SEI Publisher API Implementation
 * 
 * Simple API functions for SEI NAL unit publishing
 */

#include "sei.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <inttypes.h>

static const char *TAG = "SEI";
static sei_publisher_handle_t g_sei_publisher = NULL;

bool sei_init(void) {
    if (g_sei_publisher) {
        ESP_LOGW(TAG, "SEI system already initialized");
        return true;
    }
    
    // Check available heap before initializing
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap before SEI init: %zu bytes", free_heap);
    
    if (free_heap < 50000) { // Require at least 50KB free
        ESP_LOGE(TAG, "Insufficient heap memory for SEI system: %zu bytes", free_heap);
        return false;
    }
    
    g_sei_publisher = sei_publisher_init(3); // Max 3 retry attempts
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "Failed to initialize SEI publisher");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ SEI system initialized");
    return true;
}

void sei_deinit(void) {
    if (g_sei_publisher) {
        sei_publisher_deinit(g_sei_publisher);
        g_sei_publisher = NULL;
        ESP_LOGI(TAG, "✅ SEI system deinitialized");
    }
}

bool sei_send_text(const char *text) {
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "SEI publisher not initialized");
        return false;
    }
    
    if (!text) {
        ESP_LOGE(TAG, "Text parameter is NULL");
        return false;
    }
    
    bool result = sei_publisher_publish_text(g_sei_publisher, text, SEI_DEFAULT_REPEAT_COUNT);
    if (result) {
        ESP_LOGI(TAG, "📤 Queued text message: \"%.50s%s\"", 
                 text, strlen(text) > 50 ? "..." : "");
    } else {
        ESP_LOGE(TAG, "❌ Failed to queue text message");
    }
    
    return result;
}

bool sei_send_json(const char *role, const char *content) {
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "SEI publisher not initialized");
        return false;
    }
    
    if (!role || !content) {
        ESP_LOGE(TAG, "Role or content parameter is NULL");
        return false;
    }
    
    // Create JSON message
    char json_buffer[SEI_MAX_PAYLOAD_SIZE];
    uint32_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
    
    int json_len = snprintf(json_buffer, sizeof(json_buffer),
                           "{\"role\":\"%s\",\"content\":\"%s\",\"timestamp\":%" PRIu32 ",\"type\":\"chat_message\"}", 
                           role, content, timestamp);
    
    if (json_len >= sizeof(json_buffer)) {
        ESP_LOGW(TAG, "JSON message truncated due to size limit");
        json_len = sizeof(json_buffer) - 1;
        json_buffer[json_len] = '\0';
    }
    
    bool result = sei_publisher_publish_json(g_sei_publisher, json_buffer, SEI_DEFAULT_REPEAT_COUNT);
    if (result) {
        ESP_LOGI(TAG, "📤 Queued JSON message: %s - \"%.30s%s\"", 
                 role, content, strlen(content) > 30 ? "..." : "");
    } else {
        ESP_LOGE(TAG, "❌ Failed to queue JSON message");
    }
    
    return result;
}

bool sei_send_raw_json(const char *json_data) {
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "SEI publisher not initialized");
        return false;
    }
    
    if (!json_data) {
        ESP_LOGE(TAG, "JSON data parameter is NULL");
        return false;
    }
    
    size_t json_len = strlen(json_data);
    if (json_len >= SEI_MAX_PAYLOAD_SIZE) {
        ESP_LOGW(TAG, "Raw JSON message too large (%zu bytes), truncating to %d", 
                 json_len, SEI_MAX_PAYLOAD_SIZE - 1);
    }
    
    bool result = sei_publisher_publish_json(g_sei_publisher, json_data, SEI_DEFAULT_REPEAT_COUNT);
    if (result) {
        ESP_LOGI(TAG, "📤 Queued raw JSON message: \"%.50s%s\"", 
                 json_data, json_len > 50 ? "..." : "");
    } else {
        ESP_LOGE(TAG, "❌ Failed to queue raw JSON message");
    }
    
    return result;
}

bool sei_send_status(const char *status, int value) {
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "SEI publisher not initialized");
        return false;
    }
    
    if (!status) {
        ESP_LOGE(TAG, "Status parameter is NULL");
        return false;
    }
    
    // Create status JSON message
    char json_buffer[SEI_MAX_PAYLOAD_SIZE];
    uint32_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
    
    int json_len = snprintf(json_buffer, sizeof(json_buffer),
                           "{\"status\":\"%s\",\"value\":%d,\"timestamp\":%" PRIu32 ",\"type\":\"status_update\"}", 
                           status, value, timestamp);
    
    if (json_len >= sizeof(json_buffer)) {
        ESP_LOGW(TAG, "Status message truncated due to size limit");
        json_len = sizeof(json_buffer) - 1;
        json_buffer[json_len] = '\0';
    }
    
    bool result = sei_publisher_publish_json(g_sei_publisher, json_buffer, SEI_DEFAULT_REPEAT_COUNT);
    if (result) {
        ESP_LOGI(TAG, "📤 Queued status message: %s = %d", status, value);
    } else {
        ESP_LOGE(TAG, "❌ Failed to queue status message");
    }
    
    return result;
}

int sei_get_queue_status(void) {
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "SEI publisher not initialized");
        return -1;
    }
    
    return sei_publisher_get_queue_size(g_sei_publisher);
}

void sei_clear_queue(void) {
    if (!g_sei_publisher) {
        ESP_LOGE(TAG, "SEI publisher not initialized");
        return;
    }
    
    sei_publisher_clear_queue(g_sei_publisher);
    ESP_LOGI(TAG, "🗑️  SEI queue cleared");
}

sei_publisher_handle_t sei_get_publisher(void) {
    return g_sei_publisher;
}