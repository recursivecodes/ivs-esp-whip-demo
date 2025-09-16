/* WHIP client WebRTC application code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
// clang-format off
#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "media_lib_os.h"
#include "video_sei_hook.h"
// clang-format on

#define TAG "WHIP_DEMO"

static esp_webrtc_handle_t webrtc;

// SEI video frame callback - called for each outgoing video frame
static int sei_video_send_callback(esp_peer_video_frame_t *frame, void *ctx) {
  if (!frame || !frame->data || frame->size == 0) {
    return 0; // Pass through unchanged
  }

  // Process frame through our SEI hook
  uint8_t *sei_output_data = NULL;
  size_t sei_output_size = 0;

  bool result = video_sei_hook_process_frame(
      frame->data, frame->size, &sei_output_data, &sei_output_size);

  if (result && sei_output_data && sei_output_size > 0) {
    // SEI data was processed - update frame info
    ESP_LOGD(TAG, "SEI processed: %zu -> %zu bytes", frame->size,
             sei_output_size);

    // Update frame data and size
    frame->data = sei_output_data;
    frame->size = sei_output_size;
    return 0; // Success
  }

  // Return success (frame unchanged)
  return 0;
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx) {
  // For now, just log events
  return 0;
}

int start_webrtc(char *url, char *token) {
  if (network_is_connected() == false) {
    ESP_LOGE(TAG, "Wifi not connected yet");
    return -1;
  }
  if (url[0] == 0) {
    ESP_LOGE(TAG, "Room Url not set yet");
    return -1;
  }
  if (webrtc) {
    esp_webrtc_close(webrtc);
    webrtc = NULL;
  }
  esp_peer_signaling_whip_cfg_t whip_cfg = {
      .auth_type = ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BEARER,
      .token = token,
  };

  // Log the video configuration being used
  ESP_LOGI(TAG, "🎥 Configuring WebRTC with video: %dx%d@%dfps", VIDEO_WIDTH,
           VIDEO_HEIGHT, VIDEO_FPS);

#if CONFIG_IDF_TARGET_ESP32P4
  ESP_LOGI(TAG, "✅ ESP32P4 target detected - using 1920x1080@25fps");
#else
  ESP_LOGI(TAG, "⚠️  Non-ESP32P4 target - using 320x240@10fps");
#endif

  // Compile-time verification of the actual values
  ESP_LOGI(
      TAG,
      "📊 Compile-time values: VIDEO_WIDTH=%d, VIDEO_HEIGHT=%d, VIDEO_FPS=%d",
      VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS);

  esp_webrtc_cfg_t cfg = {
      .peer_cfg =
          {
              .server_lists = NULL,
              .server_num = 0,
              .audio_info =
                  {
#ifdef WEBRTC_SUPPORT_OPUS
                      .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                      .sample_rate = 48000,
                      .channel = 2,
#else
                      .codec = ESP_PEER_AUDIO_CODEC_G711A,
                      .sample_rate = 8000,
                      .channel = 1,
#endif
                  },
              .video_info =
                  {
                      .codec = ESP_PEER_VIDEO_CODEC_H264,
                      .width = VIDEO_WIDTH,
                      .height = VIDEO_HEIGHT,
                      .fps = VIDEO_FPS,
                  },
              .audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
              .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
              .no_auto_reconnect =
                  true, // No auto connect peer when signaling connected
              .on_video_send =
                  sei_video_send_callback, // Hook for SEI injection
          },
      .signaling_cfg =
          {
              .signal_url = url,
              .extra_cfg = token ? &whip_cfg : NULL,
              .extra_size = token ? sizeof(whip_cfg) : 0,
          },
      .peer_impl = esp_peer_get_default_impl(),
      .signaling_impl = esp_signaling_get_whip_impl(),
  };
  int ret = esp_webrtc_open(&cfg, &webrtc);
  if (ret != 0) {
    ESP_LOGE(TAG, "Fail to open webrtc");
    return ret;
  }
  // Set media provider
  esp_webrtc_media_provider_t media_provider = {};
  media_sys_get_provider(&media_provider);
  esp_webrtc_set_media_provider(webrtc, &media_provider);

  // Set event handler
  esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

  // Default disable auto connect of peer connection
  esp_webrtc_enable_peer_connection(webrtc, true);

  // Start webrtc
  ret = esp_webrtc_start(webrtc);
  if (ret != 0) {
    ESP_LOGE(TAG, "Fail to start webrtc");
  }
  return ret;
}

void query_webrtc(void) {
  if (webrtc) {
    esp_webrtc_query(webrtc);
  }
}

int stop_webrtc(void) {
  if (webrtc) {
    esp_webrtc_handle_t handle = webrtc;
    webrtc = NULL;
    ESP_LOGI(TAG, "Start to close webrtc %p", handle);
    esp_webrtc_close(handle);
  }
  return 0;
}