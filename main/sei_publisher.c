/* SEI NAL Unit Publisher Implementation
 * 
 * Portable SEI NAL unit publisher for WebRTC video streams.
 * Handles creation and insertion of SEI (Supplemental Enhancement Information)
 * NAL units into H.264 video streams for transmitting metadata alongside video frames.
 */

#include "sei_publisher.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <inttypes.h>

static const char *TAG = "SEI_PUBLISHER";

// Constants for SEI NAL unit construction
#define SEI_TYPE_USER_DATA_UNREGISTERED 0x05
#define SEI_PAYLOAD_TERMINATION 0x80
#define NAL_UNIT_TYPE_SEI 0x06

// UUID for identifying our SEI messages (unique v4 UUID: 3f8a2b1c-4d5e-6f70-8192-a3b4c5d6e7f8)
static const uint8_t SEND_SEI_UUID[16] = {
    0x3F, 0x8A, 0x2B, 0x1C, 0x4D, 0x5E, 0x6F, 0x70,
    0x81, 0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8
};

/**
 * @brief SEI publisher internal structure
 */
typedef struct sei_publisher_s {
    int max_retry_attempts;
    sei_message_t message_queue[SEI_MAX_QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_count;
    SemaphoreHandle_t mutex;
} sei_publisher_t;

/**
 * @brief Convert length to variable-length encoding used in SEI
 */
static size_t length_to_uint8(int length, uint8_t *output) {
    size_t bytes_written = 0;
    
    while (length >= 255) {
        output[bytes_written++] = 0xFF;
        length -= 255;
    }
    output[bytes_written++] = (uint8_t)length;
    
    return bytes_written;
}

/**
 * @brief Create SEI NAL unit header
 */
static size_t create_header(const uint8_t *uuid, size_t payload_length, uint8_t *output) {
    size_t pos = 0;
    
    // Start code (4-byte to match H.264 frames)
    output[pos++] = 0x00;
    output[pos++] = 0x00;
    output[pos++] = 0x00;
    output[pos++] = 0x01;
    
    // NAL unit type and SEI type
    output[pos++] = NAL_UNIT_TYPE_SEI;
    output[pos++] = SEI_TYPE_USER_DATA_UNREGISTERED;
    
    // Length encoding
    pos += length_to_uint8(payload_length + 16, &output[pos]); // +16 for UUID
    
    // UUID
    memcpy(&output[pos], uuid, 16);
    pos += 16;
    
    return pos;
}

/**
 * @brief Apply emulation prevention to avoid start code conflicts
 */
static size_t do_emulation_prevention(const uint8_t *input, size_t input_size, uint8_t *output) {
    size_t output_pos = 0;
    
    // Copy start code (first 4 bytes) as-is without emulation prevention
    size_t start_bytes = (input_size >= 4) ? 4 : input_size;
    for (size_t i = 0; i < start_bytes; i++) {
        output[output_pos++] = input[i];
    }
    
    if (input_size <= 4) {
        return output_pos;
    }
    
    // Process remaining bytes with emulation prevention (skip start code)
    for (size_t i = 4; i < input_size; i++) {
        // Check for potential start code emulation in payload data
        if (output_pos >= 2 && 
            output[output_pos - 2] == 0x00 && 
            output[output_pos - 1] == 0x00 && 
            (input[i] == 0x00 || input[i] == 0x01 || input[i] == 0x02 || input[i] == 0x03)) {
            // Insert emulation prevention byte
            output[output_pos++] = 0x03;
        }
        output[output_pos++] = input[i];
    }
    
    return output_pos;
}

/**
 * @brief Create a complete SEI NAL unit
 */
static size_t create_sei_nal_unit(const uint8_t *uuid, const uint8_t *payload, size_t payload_size, uint8_t *output) {
    uint8_t temp_buffer[SEI_MAX_PAYLOAD_SIZE + 100]; // Extra space for header and termination
    size_t pos = 0;
    
    // Create header
    pos += create_header(uuid, payload_size, &temp_buffer[pos]);
    
    // Add payload
    memcpy(&temp_buffer[pos], payload, payload_size);
    pos += payload_size;
    
    // Add termination
    temp_buffer[pos++] = SEI_PAYLOAD_TERMINATION;
    
    // Apply emulation prevention
    size_t final_size = do_emulation_prevention(temp_buffer, pos, output);
    
    // Debug: Log the created SEI NAL unit
    ESP_LOGV(TAG, "Created SEI NAL unit: %zu bytes", final_size);
    
    return final_size;
}

/**
 * @brief Find position to insert SEI NAL unit (before first video slice)
 */
static int find_insert_position(const uint8_t *frame_data, size_t frame_size) {
    for (size_t i = 0; i < frame_size - 4; i++) {
        // Check for 3-byte start code
        if (frame_data[i] == 0x00 && frame_data[i + 1] == 0x00 && frame_data[i + 2] == 0x01) {
            uint8_t nal_unit_type = frame_data[i + 3] & 0x1F;
            // Insert before video slice NAL units (types 1-5)
            if (nal_unit_type >= 1 && nal_unit_type <= 5) {
                return i;
            }
            i += 3;
        }
        // Check for 4-byte start code
        else if (i < frame_size - 5 && 
                 frame_data[i] == 0x00 && frame_data[i + 1] == 0x00 && 
                 frame_data[i + 2] == 0x00 && frame_data[i + 3] == 0x01) {
            uint8_t nal_unit_type = frame_data[i + 4] & 0x1F;
            // Insert before video slice NAL units (types 1-5)
            if (nal_unit_type >= 1 && nal_unit_type <= 5) {
                return i;
            }
            i += 4;
        }
    }
    return -1; // Not found, will prepend to beginning
}

/**
 * @brief Insert SEI unit into frame data at optimal position
 */
static bool insert_sei_unit(const uint8_t *frame_data, size_t frame_size, 
                           const uint8_t *sei_unit, size_t sei_size,
                           uint8_t **output_data, size_t *output_size) {
    int insert_position = find_insert_position(frame_data, frame_size);
    
    *output_size = frame_size + sei_size;
    *output_data = malloc(*output_size);
    if (!*output_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for modified frame");
        return false;
    }
    
    if (insert_position >= 0) {
        // Insert SEI unit before the found NAL unit
        memcpy(*output_data, frame_data, insert_position);
        memcpy(*output_data + insert_position, sei_unit, sei_size);
        memcpy(*output_data + insert_position + sei_size, 
               frame_data + insert_position, frame_size - insert_position);
    } else {
        // Fallback: prepend to beginning of frame
        memcpy(*output_data, sei_unit, sei_size);
        memcpy(*output_data + sei_size, frame_data, frame_size);
    }
    
    return true;
}

// Public API implementations

sei_publisher_handle_t sei_publisher_init(int max_retry_attempts) {
    sei_publisher_t *publisher = calloc(1, sizeof(sei_publisher_t));
    if (!publisher) {
        ESP_LOGE(TAG, "Failed to allocate SEI publisher");
        return NULL;
    }
    
    publisher->max_retry_attempts = max_retry_attempts;
    publisher->queue_head = 0;
    publisher->queue_tail = 0;
    publisher->queue_count = 0;
    
    publisher->mutex = xSemaphoreCreateMutex();
    if (!publisher->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(publisher);
        return NULL;
    }
    
    ESP_LOGI(TAG, "📡 SEI Publisher initialized with UUID: 3f8a2b1c-4d5e-6f70-8192-a3b4c5d6e7f8");
    return publisher;
}

void sei_publisher_deinit(sei_publisher_handle_t handle) {
    if (!handle) return;
    
    sei_publisher_t *publisher = (sei_publisher_t *)handle;
    
    // Clear queue and free any allocated payloads
    sei_publisher_clear_queue(handle);
    
    if (publisher->mutex) {
        vSemaphoreDelete(publisher->mutex);
    }
    
    free(publisher);
    ESP_LOGI(TAG, "📡 SEI Publisher deinitialized");
}

bool sei_publisher_publish_text(sei_publisher_handle_t handle, const char *text, int repeat_count) {
    if (!handle || !text) return false;
    
    // Create JSON payload with timestamp
    char json_buffer[SEI_MAX_PAYLOAD_SIZE];
    uint32_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
    
    int json_len = snprintf(json_buffer, sizeof(json_buffer),
                           "{\"text\":\"%s\",\"timestamp\":%" PRIu32 ",\"type\":\"text_content\"}", 
                           text, timestamp);
    
    if (json_len >= sizeof(json_buffer)) {
        ESP_LOGW(TAG, "Text message truncated due to size limit");
        json_len = sizeof(json_buffer) - 1;
    }
    
    return sei_publisher_publish_json(handle, json_buffer, repeat_count);
}

bool sei_publisher_publish_json(sei_publisher_handle_t handle, const char *json_str, int repeat_count) {
    if (!handle || !json_str) return false;
    
    sei_publisher_t *publisher = (sei_publisher_t *)handle;
    size_t json_len = strlen(json_str);
    
    if (json_len > SEI_MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "JSON payload too large: %zu bytes (max %d)", json_len, SEI_MAX_PAYLOAD_SIZE);
        return false;
    }
    
    if (xSemaphoreTake(publisher->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return false;
    }
    
    // Check if queue is full
    if (publisher->queue_count >= SEI_MAX_QUEUE_SIZE) {
        ESP_LOGW(TAG, "SEI message queue full, dropping oldest message");
        // Free the oldest message payload
        if (publisher->message_queue[publisher->queue_head].payload) {
            free(publisher->message_queue[publisher->queue_head].payload);
        }
        publisher->queue_head = (publisher->queue_head + 1) % SEI_MAX_QUEUE_SIZE;
        publisher->queue_count--;
    }
    
    // Allocate and copy payload
    sei_message_t *msg = &publisher->message_queue[publisher->queue_tail];
    
    // Add extra byte for null terminator safety
    msg->payload = malloc(json_len + 1);
    if (!msg->payload) {
        ESP_LOGE(TAG, "Failed to allocate payload memory");
        xSemaphoreGive(publisher->mutex);
        return false;
    }
    
    // Clear the allocated memory
    memset(msg->payload, 0, json_len + 1);
    
    memcpy(msg->payload, json_str, json_len);
    msg->payload_size = json_len;
    msg->repeat_count = repeat_count > 0 ? repeat_count : SEI_DEFAULT_REPEAT_COUNT;
    msg->timestamp = esp_timer_get_time() / 1000;
    
    publisher->queue_tail = (publisher->queue_tail + 1) % SEI_MAX_QUEUE_SIZE;
    publisher->queue_count++;
    
    ESP_LOGI(TAG, "📡 Queued SEI message: %zu bytes, queue: %d/%d, repeat: %d", 
             json_len, publisher->queue_count, SEI_MAX_QUEUE_SIZE, msg->repeat_count);
    
    xSemaphoreGive(publisher->mutex);
    return true;
}

bool sei_publisher_process_frame(sei_publisher_handle_t handle, 
                                const uint8_t *frame_data, size_t frame_size,
                                uint8_t **output_data, size_t *output_size) {
    if (!handle || !frame_data || !output_data || !output_size) return false;
    
    sei_publisher_t *publisher = (sei_publisher_t *)handle;
    
    if (xSemaphoreTake(publisher->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        // If we can't get the mutex quickly, just return original frame
        *output_size = frame_size;
        *output_data = malloc(frame_size);
        if (*output_data) {
            memcpy(*output_data, frame_data, frame_size);
            return true;
        }
        return false;
    }
    
    // If no messages queued, return original frame
    if (publisher->queue_count == 0) {
        *output_size = frame_size;
        *output_data = malloc(frame_size);
        if (*output_data) {
            memcpy(*output_data, frame_data, frame_size);
        }
        xSemaphoreGive(publisher->mutex);
        return (*output_data != NULL);
    }
    
    // Check if this is a keyframe (contains SPS/PPS/IDR)
    bool is_keyframe = false;
    if (frame_size >= 4) {
        // Look for SPS (NAL type 7), PPS (NAL type 8), or IDR (NAL type 5)
        for (size_t i = 0; i < frame_size - 4; i++) {
            if ((frame_data[i] == 0x00 && frame_data[i+1] == 0x00 && frame_data[i+2] == 0x00 && frame_data[i+3] == 0x01) ||
                (frame_data[i] == 0x00 && frame_data[i+1] == 0x00 && frame_data[i+2] == 0x01)) {
                
                int nal_start = (frame_data[i+2] == 0x01) ? i+3 : i+4;
                if (nal_start < frame_size) {
                    uint8_t nal_type = frame_data[nal_start] & 0x1F;
                    if (nal_type == 5 || nal_type == 7 || nal_type == 8) { // IDR, SPS, or PPS
                        is_keyframe = true;
                        break;
                    }
                }
            }
        }
    }
    
    // Only inject SEI into keyframes for reliable delivery
    if (!is_keyframe) {
        *output_size = frame_size;
        *output_data = malloc(frame_size);
        if (*output_data) {
            memcpy(*output_data, frame_data, frame_size);
        }
        xSemaphoreGive(publisher->mutex);
        return (*output_data != NULL);
    }
    
    // Process all queued messages
    uint8_t *current_data = malloc(frame_size);
    size_t current_size = frame_size;
    if (!current_data) {
        xSemaphoreGive(publisher->mutex);
        return false;
    }
    memcpy(current_data, frame_data, frame_size);
    
    int processed_messages = 0;
    while (publisher->queue_count > 0) {
        sei_message_t *msg = &publisher->message_queue[publisher->queue_head];
        
        // Create SEI NAL unit
        uint8_t sei_unit[SEI_MAX_PAYLOAD_SIZE + 100];
        size_t sei_size = create_sei_nal_unit(SEND_SEI_UUID, msg->payload, msg->payload_size, sei_unit);
        
        // Insert SEI unit multiple times for reliability
        for (int i = 0; i < msg->repeat_count; i++) {
            uint8_t *new_data;
            size_t new_size;
            
            if (insert_sei_unit(current_data, current_size, sei_unit, sei_size, &new_data, &new_size)) {
                free(current_data);
                current_data = new_data;
                current_size = new_size;
            } else {
                ESP_LOGE(TAG, "Failed to insert SEI unit (attempt %d)", i+1);
                break;
            }
        }
        
        ESP_LOGI(TAG, "📡 Inserted SEI unit: %zu bytes, repeated %d times", sei_size, msg->repeat_count);
        
        // Free message payload and advance queue
        free(msg->payload);
        msg->payload = NULL;
        publisher->queue_head = (publisher->queue_head + 1) % SEI_MAX_QUEUE_SIZE;
        publisher->queue_count--;
        processed_messages++;
    }
    
    *output_data = current_data;
    *output_size = current_size;
    
    if (processed_messages > 0) {
        ESP_LOGI(TAG, "📡 Processed %d SEI messages, frame size: %zu -> %zu bytes", 
                 processed_messages, frame_size, current_size);
    }
    
    xSemaphoreGive(publisher->mutex);
    return true;
}

int sei_publisher_get_queue_size(sei_publisher_handle_t handle) {
    if (!handle) return 0;
    
    sei_publisher_t *publisher = (sei_publisher_t *)handle;
    
    if (xSemaphoreTake(publisher->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return -1; // Indicate error
    }
    
    int count = publisher->queue_count;
    xSemaphoreGive(publisher->mutex);
    return count;
}

void sei_publisher_clear_queue(sei_publisher_handle_t handle) {
    if (!handle) return;
    
    sei_publisher_t *publisher = (sei_publisher_t *)handle;
    
    if (xSemaphoreTake(publisher->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for queue clear");
        return;
    }
    
    int cleared_count = publisher->queue_count;
    
    // Free all allocated payloads
    while (publisher->queue_count > 0) {
        sei_message_t *msg = &publisher->message_queue[publisher->queue_head];
        if (msg->payload) {
            free(msg->payload);
            msg->payload = NULL;
        }
        publisher->queue_head = (publisher->queue_head + 1) % SEI_MAX_QUEUE_SIZE;
        publisher->queue_count--;
    }
    
    publisher->queue_head = 0;
    publisher->queue_tail = 0;
    
    if (cleared_count > 0) {
        ESP_LOGI(TAG, "🗑️  Cleared %d queued SEI messages", cleared_count);
    }
    
    xSemaphoreGive(publisher->mutex);
}