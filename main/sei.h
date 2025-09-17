/* SEI Publisher API
 * 
 * Simple API functions for SEI NAL unit publishing
 */

#pragma once

#include "sei_publisher.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SEI system
 * 
 * @return true if initialization successful, false otherwise
 */
bool sei_init(void);

/**
 * @brief Deinitialize SEI system
 */
void sei_deinit(void);

/**
 * @brief Send a text message via SEI
 * 
 * @param text Text message to send
 * @return true if message queued successfully, false otherwise
 */
bool sei_send_text(const char *text);

/**
 * @brief Send a JSON message via SEI
 * 
 * @param role Message role (e.g., "user", "assistant")
 * @param content Message content
 * @return true if message queued successfully, false otherwise
 */
bool sei_send_json(const char *role, const char *content);

/**
 * @brief Send raw JSON data via SEI without additional wrapping
 * 
 * @param json_data Complete JSON string to send as-is
 * @return true if message queued successfully, false otherwise
 */
bool sei_send_raw_json(const char *json_data);

/**
 * @brief Send a status message via SEI
 * 
 * @param status Status string
 * @param value Numeric value
 * @return true if message queued successfully, false otherwise
 */
bool sei_send_status(const char *status, int value);

/**
 * @brief Get current SEI queue status
 * 
 * @return Number of queued messages, or -1 on error
 */
int sei_get_queue_status(void);

/**
 * @brief Clear all queued SEI messages
 */
void sei_clear_queue(void);

/**
 * @brief Get the SEI publisher handle for direct use
 * 
 * @return SEI publisher handle or NULL if not initialized
 */
sei_publisher_handle_t sei_get_publisher(void);

#ifdef __cplusplus
}
#endif