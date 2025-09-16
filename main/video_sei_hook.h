/* Video SEI Hook Interface
 * 
 * Provides hooks to intercept video frames and inject SEI NAL units
 * before they are sent via WebRTC
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "sei_publisher.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Video frame processing callback type
 * 
 * @param frame_data Input video frame data
 * @param frame_size Size of input frame data
 * @param output_data Pointer to store output frame data (caller must free)
 * @param output_size Pointer to store output frame size
 * @param user_ctx User context pointer
 * @return true if processing successful, false otherwise
 */
typedef bool (*video_frame_processor_t)(const uint8_t *frame_data, size_t frame_size,
                                       uint8_t **output_data, size_t *output_size,
                                       void *user_ctx);

/**
 * @brief Initialize video SEI hook system
 * 
 * @return true if initialization successful, false otherwise
 */
bool video_sei_hook_init(void);

/**
 * @brief Deinitialize video SEI hook system
 */
void video_sei_hook_deinit(void);

/**
 * @brief Set custom video frame processor
 * 
 * @param processor Frame processor callback
 * @param user_ctx User context to pass to processor
 */
void video_sei_hook_set_processor(video_frame_processor_t processor, void *user_ctx);

/**
 * @brief Process video frame with SEI injection
 * 
 * This function should be called from the video encoding pipeline
 * to inject SEI NAL units into video frames.
 * 
 * @param frame_data Input video frame data
 * @param frame_size Size of input frame data
 * @param output_data Pointer to store output frame data (caller must free)
 * @param output_size Pointer to store output frame size
 * @return true if processing successful, false otherwise
 */
bool video_sei_hook_process_frame(const uint8_t *frame_data, size_t frame_size,
                                 uint8_t **output_data, size_t *output_size);

/**
 * @brief Get statistics about SEI processing
 * 
 * @param frames_processed Pointer to store number of frames processed
 * @param sei_units_inserted Pointer to store number of SEI units inserted
 * @param total_sei_bytes Pointer to store total SEI bytes added
 */
void video_sei_hook_get_stats(uint32_t *frames_processed, uint32_t *sei_units_inserted, uint32_t *total_sei_bytes);

/**
 * @brief Reset processing statistics
 */
void video_sei_hook_reset_stats(void);

#ifdef __cplusplus
}
#endif