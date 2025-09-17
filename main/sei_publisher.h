/* SEI NAL Unit Publisher for WHIP Demo
 * 
 * This header provides functionality to insert SEI (Supplemental Enhancement Information)
 * NAL units into H.264 video streams for transmitting metadata alongside video frames.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum payload size for individual SEI messages (conservative limit)
#define SEI_MAX_PAYLOAD_SIZE 400

// Maximum number of queued messages
#define SEI_MAX_QUEUE_SIZE 15

// Default repeat count for reliability
#define SEI_DEFAULT_REPEAT_COUNT 3

/**
 * @brief SEI message structure
 */
typedef struct {
    uint8_t *payload;           /*!< Message payload data */
    size_t payload_size;        /*!< Size of payload in bytes */
    int repeat_count;           /*!< Number of times to repeat for reliability */
    uint32_t timestamp;         /*!< Message timestamp (milliseconds since boot) */
} sei_message_t;

/**
 * @brief SEI publisher handle
 */
typedef struct sei_publisher_s *sei_publisher_handle_t;

/**
 * @brief Initialize SEI publisher
 * 
 * @param max_retry_attempts Maximum number of retry attempts for publishing
 * @return SEI publisher handle or NULL on failure
 */
sei_publisher_handle_t sei_publisher_init(int max_retry_attempts);

/**
 * @brief Deinitialize SEI publisher and free resources
 * 
 * @param handle SEI publisher handle
 */
void sei_publisher_deinit(sei_publisher_handle_t handle);

/**
 * @brief Publish text content as SEI metadata
 * 
 * @param handle SEI publisher handle
 * @param text Text content to publish
 * @param repeat_count Number of times to repeat the message for reliability
 * @return true if successfully queued, false otherwise
 */
bool sei_publisher_publish_text(sei_publisher_handle_t handle, const char *text, int repeat_count);

/**
 * @brief Publish JSON string as SEI metadata
 * 
 * @param handle SEI publisher handle
 * @param json_str JSON string to publish
 * @param repeat_count Number of times to repeat the message for reliability
 * @return true if successfully queued, false otherwise
 */
bool sei_publisher_publish_json(sei_publisher_handle_t handle, const char *json_str, int repeat_count);

/**
 * @brief Process a video frame and insert any queued SEI messages
 * 
 * @param handle SEI publisher handle
 * @param frame_data Input video frame data
 * @param frame_size Size of input frame data
 * @param output_data Pointer to store output frame data (caller must free)
 * @param output_size Pointer to store output frame size
 * @return true if processing successful, false otherwise
 */
bool sei_publisher_process_frame(sei_publisher_handle_t handle, 
                                const uint8_t *frame_data, size_t frame_size,
                                uint8_t **output_data, size_t *output_size);

/**
 * @brief Get the current number of queued messages
 * 
 * @param handle SEI publisher handle
 * @return Number of queued messages
 */
int sei_publisher_get_queue_size(sei_publisher_handle_t handle);

/**
 * @brief Clear all queued messages
 * 
 * @param handle SEI publisher handle
 */
void sei_publisher_clear_queue(sei_publisher_handle_t handle);

#ifdef __cplusplus
}
#endif