/* WHIP Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
// clang-format off
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_webrtc.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "webrtc_utils_time.h"
#include "esp_cpu.h"
#include "settings.h"
#include "common.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include <inttypes.h>
#include "sei.h"
#include "video_sei_hook.h"
#include "esp_capture.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#if SEI_ENABLE_DHT11
#include "dht.h"
#endif

// clang-format on
#define RUN_ASYNC(name, body)                                                  \
  void run_async##name(void *arg) {                                            \
    body;                                                                      \
    media_lib_thread_destroy(NULL);                                            \
  }                                                                            \
  media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

#define BUTTON_GPIO GPIO_NUM_35 // Use GPIO 35 (BOOT button)
#define BUTTON_ACTIVE_LEVEL 0   // 0 for pull-up (pressed = LOW)

// DHT-11 Configuration
#define DHT11_GPIO GPIO_NUM_23  // GPIO23 (J1 Pin 7)
#define DHT11_READ_INTERVAL_MS 5000  // Read every 5 seconds

// Token API configuration
// clang-format off
#define TOKEN_REQUEST_BODY                                                     \
  "{\"stageArn\": \"" STAGE_ARN "\", \"capabilities\": [\"PUBLISH\"]," \
  "\"attributes\": {\"username\": \"" PARTICIPANT_NAME "\"}}"
// clang-format on

static const char *TAG = "IVS_WHIP_DEMO";
static bool publishing_active = false;
static bool last_button_state = true;  // Assume released initially (pull-up)
static char *current_token = NULL;     // Dynamically fetched token
static bool sei_system_active = false; // Track SEI system state

#if SEI_ENABLE_DHT11
static bool dht11_initialized = false;
static float last_temperature = 0.0;
static float last_humidity = 0.0;
#endif

// Forward declarations
static bool fetch_token(void);
static void sei_message_task(void *arg); // Temporarily removed
#if SEI_ENABLE_DHT11
static bool dht11_init(void);
static bool dht11_read(float *temperature, float *humidity);
static void dht11_task(void *arg);
#endif

static int start_publish(int argc, char **argv) {
  static bool sntp_synced = false;
  if (sntp_synced == false) {
    if (0 == webrtc_utils_time_sync_init()) {
      sntp_synced = true;
    }
  }
  if (argc == 1) {
    // Fetch fresh token and use it
    if (fetch_token() && current_token) {
      ESP_LOGI(TAG, "🚀 Starting WHIP stream with fresh token via console");
      start_webrtc(WHIP_SERVER, current_token);
    } else {
      ESP_LOGE(TAG, "❌ Failed to fetch token, using fallback token");
      start_webrtc(WHIP_SERVER, WHIP_TOKEN);
    }
  } else {
    start_webrtc(argv[1], argc > 2 ? argv[2] : NULL);
  }
  return 0;
}

static int stop_publish(int argc, char **argv) {
  RUN_ASYNC(leave, { stop_webrtc(); });
  return 0;
}

static int assert_cli(int argc, char **argv) {
  *(int *)0 = 0;
  return 0;
}

static int sys_cli(int argc, char **argv) {
  sys_state_show();
  return 0;
}

static int wifi_cli(int argc, char **argv) {
  if (argc < 1) {
    return -1;
  }
  char *ssid = argv[1];
  char *password = argc > 2 ? argv[2] : NULL;
  return network_connect_wifi(ssid, password);
}

static int capture_to_player_cli(int argc, char **argv) {
  return test_capture_to_player();
}

static int measure_cli(int argc, char **argv) {
  void measure_enable(bool enable);
  void show_measure(void);
  measure_enable(true);
  media_lib_thread_sleep(1500);
  measure_enable(false);
  return 0;
}

static int sei_text_cli(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: sei_text <message>\n");
    return -1;
  }

  if (!sei_system_active) {
    printf("SEI system not active\n");
    return -1;
  }

  if (sei_send_text(argv[1])) {
    printf("SEI text message queued: %s\n", argv[1]);
  } else {
    printf("Failed to queue SEI text message\n");
  }
  return 0;
}

static int sei_json_cli(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: sei_json <role> <content>\n");
    return -1;
  }

  if (!sei_system_active) {
    printf("SEI system not active\n");
    return -1;
  }

  if (sei_send_json(argv[1], argv[2])) {
    printf("SEI JSON message queued: role=%s, content=%s\n", argv[1], argv[2]);
  } else {
    printf("Failed to queue SEI JSON message\n");
  }
  return 0;
}

static int sei_status_cli(int argc, char **argv) {
  if (!sei_system_active) {
    printf("SEI system not active\n");
    return -1;
  }

  int queue_status = sei_get_queue_status();
  if (queue_status >= 0) {
    printf("SEI queue status: %d messages pending\n", queue_status);

    // Get video hook stats
    uint32_t frames_processed, sei_units_inserted, total_sei_bytes;
    video_sei_hook_get_stats(&frames_processed, &sei_units_inserted,
                             &total_sei_bytes);
    printf("Video hook stats: %" PRIu32 " frames, %" PRIu32
           " SEI units, %" PRIu32 " bytes\n",
           frames_processed, sei_units_inserted, total_sei_bytes);
  } else {
    printf("Failed to get SEI queue status\n");
  }
  return 0;
}

static int sei_test_hook_cli(int argc, char **argv) {
  if (!sei_system_active) {
    printf("SEI system not active\n");
    return -1;
  }

  // Create a fake H.264 frame to test SEI injection
  uint8_t fake_h264_frame[] = {
      0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E, // SPS NAL unit
      0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x3C, 0x80, // PPS NAL unit
      0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, // IDR slice
      0xFF, 0xFF, 0xFF, 0xFF                          // Some fake slice data
  };

  printf("Testing SEI hook with fake H.264 frame (%zu bytes)...\n",
         sizeof(fake_h264_frame));

  uint8_t *output_data = NULL;
  size_t output_size = 0;

  bool result = video_sei_hook_process_frame(
      fake_h264_frame, sizeof(fake_h264_frame), &output_data, &output_size);

  if (result && output_data) {
    printf("SEI hook test successful: %zu -> %zu bytes\n",
           sizeof(fake_h264_frame), output_size);
    if (output_size > sizeof(fake_h264_frame)) {
      printf("✅ SEI data was added (+%zu bytes)\n",
             output_size - sizeof(fake_h264_frame));
    } else {
      printf("ℹ️  No SEI data added (no messages queued)\n");
    }
    free(output_data);
  } else {
    printf("❌ SEI hook test failed\n");
  }

  return 0;
}

#if SEI_ENABLE_DHT11
static int dht11_read_cli(int argc, char **argv) {
  if (!dht11_initialized) {
    printf("DHT-11 sensor not initialized\n");
    return -1;
  }

  float temperature, humidity;
  if (dht11_read(&temperature, &humidity)) {
    printf("🌡️ DHT-11 Reading: Temperature: %.1f°C, Humidity: %.1f%%\n", temperature, humidity);
    
    // Also send via SEI if system is active
    if (sei_system_active) {
      char json_message[250];
      uint32_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
      snprintf(json_message, sizeof(json_message),
              "{\"sensor\":\"DHT11\",\"temperature_c\":%.1f,\"humidity_percent\":%.1f,\"timestamp\":%" PRIu32 ",\"status\":\"manual_read\",\"type\":\"sensor_data\"}",
              temperature, humidity, timestamp);
      
      if (sei_send_raw_json(json_message)) {
        printf("📤 DHT-11 data sent via SEI as raw JSON\n");
      }
    }
  } else {
    printf("❌ Failed to read DHT-11 sensor\n");
  }
  return 0;
}

static int dht11_status_cli(int argc, char **argv) {
  printf("🌡️ DHT-11 Status:\n");
  printf("  Initialized: %s\n", dht11_initialized ? "Yes" : "No");
  printf("  GPIO Pin: %d\n", DHT11_GPIO);
  printf("  Read Interval: %d seconds\n", DHT11_READ_INTERVAL_MS / 1000);
  printf("  Last Temperature: %.1f°C\n", last_temperature);
  printf("  Last Humidity: %.1f%%\n", last_humidity);
  printf("  SEI Publishing: %s\n", (sei_system_active && publishing_active) ? "Active" : "Inactive");
  return 0;
}
#endif

static int sei_clear_cli(int argc, char **argv) {
  if (!sei_system_active) {
    printf("SEI system not active\n");
    return -1;
  }

  sei_clear_queue();
  printf("SEI queue cleared\n");
  return 0;
}

static int sei_raw_json_cli(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: sei_raw_json <json_string>\n");
    return -1;
  }

  if (!sei_system_active) {
    printf("SEI system not active\n");
    return -1;
  }

  if (sei_send_raw_json(argv[1])) {
    printf("SEI raw JSON message queued: %s\n", argv[1]);
  } else {
    printf("Failed to queue SEI raw JSON message\n");
  }
  return 0;
}

static int init_console() {
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = "esp>";
  repl_config.task_stack_size = 10 * 1024;
  repl_config.task_priority = 22;
  repl_config.max_cmdline_length = 1024;
  // install console REPL environment
#if CONFIG_ESP_CONSOLE_UART
  esp_console_dev_uart_config_t uart_config =
      ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
  esp_console_dev_usb_cdc_config_t cdc_config =
      ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(
      esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  esp_console_dev_usb_serial_jtag_config_t usbjtag_config =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config,
                                                       &repl_config, &repl));
#endif
  esp_console_cmd_t cmds[] = {
      {
          .command = "start",
          .help = "Start WHIP publish\r\n",
          .func = start_publish,
      },
      {
          .command = "stop",
          .help = "Stop WHIP publish\n",
          .func = stop_publish,
      },
      {
          .command = "i",
          .help = "Show system status\r\n",
          .func = sys_cli,
      },
      {
          .command = "assert",
          .help = "Assert system\r\n",
          .func = assert_cli,
      },
      {
          .command = "rec2play",
          .help = "Play capture content\n",
          .func = capture_to_player_cli,
      },
      {
          .command = "wifi",
          .help = "wifi ssid psw\r\n",
          .func = wifi_cli,
      },
      {
          .command = "m",
          .help = "measure system loading\r\n",
          .func = measure_cli,
      },
      {
          .command = "sei_text",
          .help = "Send SEI text message: sei_text <message>\r\n",
          .func = sei_text_cli,
      },
      {
          .command = "sei_json",
          .help = "Send SEI JSON message: sei_json <role> <content>\r\n",
          .func = sei_json_cli,
      },
      {
          .command = "sei_status",
          .help = "Show SEI system status\r\n",
          .func = sei_status_cli,
      },
      {
          .command = "sei_clear",
          .help = "Clear SEI message queue\r\n",
          .func = sei_clear_cli,
      },
      {
          .command = "sei_test_hook",
          .help = "Test SEI hook with fake frame\r\n",
          .func = sei_test_hook_cli,
      },
      {
          .command = "sei_raw_json",
          .help = "Send raw JSON message via SEI: sei_raw_json <json>\r\n",
          .func = sei_raw_json_cli,
      },
#if SEI_ENABLE_DHT11
      {
          .command = "dht11_read",
          .help = "Read DHT-11 sensor manually\r\n",
          .func = dht11_read_cli,
      },
      {
          .command = "dht11_status",
          .help = "Show DHT-11 sensor status\r\n",
          .func = dht11_status_cli,
      },
#endif
  };
  for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
  }
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
  return 0;
}

static void thread_scheduler(const char *thread_name,
                             media_lib_thread_cfg_t *schedule_cfg) {
  ESP_LOGI(TAG, "Thread name: %s", thread_name);
  if (strcmp(thread_name, "venc_0") == 0) {
    // For H264 may need huge stack if use hardware encoder can set it to small
    // value
    schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
    schedule_cfg->stack_size = 20 * 1024;
#endif
  }
#ifdef WEBRTC_SUPPORT_OPUS
  else if (strcmp(thread_name, "aenc_0") == 0) {
    ESP_LOGI(TAG,
             "🎵 Configuring aenc_0 with OPUS support - setting 128KB stack");
    // For OPUS encoder it need huge stack, especially on ESP32-P4
    schedule_cfg->stack_size = 128 * 1024; // Increased from 40KB to 128KB
    schedule_cfg->priority = 10;
    schedule_cfg->core_id = 1;
  }
#else
  else if (strcmp(thread_name, "aenc_0") == 0) {
    ESP_LOGW(TAG, "⚠️  OPUS support NOT enabled - using default aenc_0 config");
  }
#endif
  else if (strcmp(thread_name, "AUD_SRC") == 0) {
    schedule_cfg->priority = 15;
  } else if (strcmp(thread_name, "pc_task") == 0) {
    schedule_cfg->stack_size = 25 * 1024;
    schedule_cfg->priority = 18;
    schedule_cfg->core_id = 1;
  } else if (strcmp(thread_name, "start") == 0) {
    schedule_cfg->stack_size = 6 * 1024;
  } else {
    // Catch any audio-related threads and give them large stacks
    if (strstr(thread_name, "aenc") || strstr(thread_name, "audio") ||
        strstr(thread_name, "opus")) {
      ESP_LOGI(TAG, "🎵 Found audio thread '%s' - setting 128KB stack",
               thread_name);
      schedule_cfg->stack_size = 128 * 1024;
      schedule_cfg->priority = 10;
      schedule_cfg->core_id = 1;
    } else {
      ESP_LOGW(TAG, "⚠️  Unhandled thread: '%s'", thread_name);
    }
  }
}

static void capture_scheduler(const char *name,
                              esp_capture_thread_schedule_cfg_t *schedule_cfg) {
  media_lib_thread_cfg_t cfg = {
      .stack_size = schedule_cfg->stack_size,
      .priority = schedule_cfg->priority,
      .core_id = schedule_cfg->core_id,
  };
  schedule_cfg->stack_in_ext = true;
  thread_scheduler(name, &cfg);
  schedule_cfg->stack_size = cfg.stack_size;
  schedule_cfg->priority = cfg.priority;
  schedule_cfg->core_id = cfg.core_id;
}

// HTTP event handler for token API response
static esp_err_t token_http_event_handler(esp_http_client_event_t *evt) {
  static char *response_buffer = NULL;
  static int response_len = 0;

  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    // Allocate buffer for response data
    if (response_buffer == NULL) {
      response_buffer = malloc(2048); // Allocate 2KB for JSON response
      response_len = 0;
    }
    if (response_buffer && response_len + evt->data_len < 2048) {
      memcpy(response_buffer + response_len, evt->data, evt->data_len);
      response_len += evt->data_len;
      response_buffer[response_len] = '\0';
    }
    break;

  case HTTP_EVENT_ON_FINISH:
    if (response_buffer) {
      ESP_LOGI(TAG, "🔑 Token API Response: %s", response_buffer);

      // Parse JSON to extract token
      cJSON *json = cJSON_Parse(response_buffer);
      if (json) {
        cJSON *token_item = cJSON_GetObjectItem(json, "token");
        if (token_item && cJSON_IsString(token_item)) {
          // Free old token and store new one
          if (current_token) {
            free(current_token);
          }
          current_token = strdup(token_item->valuestring);
          ESP_LOGI(TAG, "✅ Token extracted successfully (length: %d)",
                   strlen(current_token));
        } else {
          ESP_LOGE(TAG, "❌ Failed to extract token from JSON response");
        }
        cJSON_Delete(json);
      } else {
        ESP_LOGE(TAG, "❌ Failed to parse JSON response");
      }

      free(response_buffer);
      response_buffer = NULL;
      response_len = 0;
    }
    break;

  default:
    break;
  }
  return ESP_OK;
}

// Fetch fresh token from API
static bool fetch_token(void) {
  ESP_LOGI(TAG, "🔄 Fetching fresh token from API...");

  esp_http_client_config_t config = {
      .url = TOKEN_API_URL,
      .event_handler = token_http_event_handler,
      .timeout_ms = 10000, // 10 second timeout
      .crt_bundle_attach =
          esp_crt_bundle_attach, // Use certificate bundle for HTTPS
      .skip_cert_common_name_check = false,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "❌ Failed to initialize HTTP client");
    return false;
  }

  // Set POST method and headers
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, TOKEN_REQUEST_BODY,
                                 strlen(TOKEN_REQUEST_BODY));

  // Perform the request
  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);

  esp_http_client_cleanup(client);

  if (err == ESP_OK && status_code == 200) {
    ESP_LOGI(TAG, "✅ Token fetch successful (HTTP %d)", status_code);
    return (current_token != NULL);
  } else {
    ESP_LOGE(TAG, "❌ Token fetch failed: %s (HTTP %d)", esp_err_to_name(err),
             status_code);
    return false;
  }
}

// Button task for manual publish control
static void button_task(void *arg) {
  bool current_state;

  while (1) {
    current_state = gpio_get_level(BUTTON_GPIO);

    // Detect button press (transition from released to pressed)
    if (last_button_state != current_state &&
        current_state == BUTTON_ACTIVE_LEVEL) {
      // Button just pressed - toggle publishing state
      if (publishing_active) {
        ESP_LOGI(TAG, "🔴 Button pressed - Stopping WHIP stream");
        // Use async task to avoid stack overflow
        RUN_ASYNC(stop, {
          stop_webrtc();
          publishing_active = false;
        });
      } else {
        ESP_LOGI(TAG, "🟢 Button pressed - Starting WHIP stream");
        // Use async task to avoid stack overflow
        RUN_ASYNC(start, {
          // Fetch fresh token before starting
          if (fetch_token() && current_token) {
            ESP_LOGI(TAG, "🚀 Starting WHIP stream with fresh token");
            if (start_webrtc(WHIP_SERVER, current_token) == 0) {
              publishing_active = true;
            } else {
              ESP_LOGE(TAG, "Failed to start WHIP stream");
            }
          } else {
            ESP_LOGE(TAG, "❌ Failed to fetch token, cannot start stream");
          }
        });
      }

      // Debounce delay
      vTaskDelay(pdMS_TO_TICKS(300));
    }

    last_button_state = current_state;
    vTaskDelay(pdMS_TO_TICKS(50)); // Check every 50ms
  }
}

#if SEI_ENABLE_DHT11
// DHT-11 sensor functions
static bool dht11_init(void) {
  // The esp-idf-lib DHT library handles GPIO configuration internally
  dht11_initialized = true;
  ESP_LOGI(TAG, "🌡️  DHT-11 sensor initialized on GPIO%d using esp-idf-lib", DHT11_GPIO);
  
  // Wait a bit for sensor to stabilize
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  // Test initial read
  float test_temp, test_hum;
  if (dht11_read(&test_temp, &test_hum)) {
    ESP_LOGI(TAG, "✅ DHT-11 initial test successful: %.1f°C, %.1f%%", test_temp, test_hum);
  } else {
    ESP_LOGW(TAG, "⚠️ DHT-11 initial test failed - sensor may need more time to stabilize");
  }
  
  return true;
}

static bool dht11_read(float *temperature, float *humidity) {
  if (!dht11_initialized) {
    ESP_LOGW(TAG, "DHT-11 not initialized");
    return false;
  }

  int16_t temp_raw, hum_raw;
  esp_err_t result = dht_read_data(DHT_TYPE_DHT11, DHT11_GPIO, &hum_raw, &temp_raw);
  
  if (result == ESP_OK) {
    *temperature = (float)temp_raw / 10.0;
    *humidity = (float)hum_raw / 10.0;
    
    // Sanity check values
    if (*humidity < 0 || *humidity > 100 || *temperature < -40 || *temperature > 80) {
      ESP_LOGW(TAG, "DHT-11 values out of range: T=%.1f°C, H=%.1f%%", *temperature, *humidity);
      return false;
    }
    
    ESP_LOGD(TAG, "DHT-11 read successful: T=%.1f°C, H=%.1f%%", *temperature, *humidity);
    return true;
  } else {
    ESP_LOGW(TAG, "DHT-11 read failed: %s", esp_err_to_name(result));
    return false;
  }
}

static void dht11_task(void *arg) {
  ESP_LOGI(TAG, "🌡️  DHT-11 task started - reading sensor every %d seconds", DHT11_READ_INTERVAL_MS / 1000);
  
  while (1) {
    if (dht11_initialized && publishing_active && sei_system_active) {
      float temperature, humidity;
      
      if (dht11_read(&temperature, &humidity)) {
        last_temperature = temperature;
        last_humidity = humidity;
        
        ESP_LOGI(TAG, "🌡️  DHT-11: Temperature: %.1f°C, Humidity: %.1f%%", temperature, humidity);
        
        // Create complete JSON message for SEI
        char json_message[300];
        uint32_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
        
        snprintf(json_message, sizeof(json_message),
                "{\"sensor\":\"DHT11\",\"temperature_c\":%.1f,\"humidity_percent\":%.1f,\"timestamp\":%" PRIu32 ",\"status\":\"ok\",\"type\":\"sensor_data\"}",
                temperature, humidity, timestamp);
        
        if (sei_send_raw_json(json_message)) {
          ESP_LOGI(TAG, "📤 DHT-11 data published via SEI as raw JSON");
        } else {
          ESP_LOGW(TAG, "⚠️ Failed to publish DHT-11 data via SEI");
        }
      } else {
        ESP_LOGW(TAG, "⚠️ Failed to read DHT-11 sensor");
        
        // Send error status via SEI
        char error_message[150];
        uint32_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
        snprintf(error_message, sizeof(error_message),
                "{\"sensor\":\"DHT11\",\"timestamp\":%" PRIu32 ",\"status\":\"read_error\",\"type\":\"sensor_error\"}", timestamp);
        sei_send_raw_json(error_message);
      }
    }
    
    // Wait for next reading interval
    vTaskDelay(pdMS_TO_TICKS(DHT11_READ_INTERVAL_MS));
  }
}
#endif

// SEI message task - sends test messages every 3 seconds (if enabled)
static void sei_message_task(void *arg) {
#if SEI_ENABLE_TEST_MESSAGES
  ESP_LOGI(
      TAG,
      "📡 SEI message task started - sending test messages every 3 seconds");

  int message_counter = 0;

  while (1) {
    if (sei_system_active && publishing_active) {
      // Send different types of test messages
      switch (message_counter % 3) {
      case 0:
        if (sei_send_text("Periodic test message from ESP32-P4")) {
          ESP_LOGI(TAG, "📤 Sent SEI text message #%d", message_counter);
        }
        break;

      case 1:
        if (sei_send_json("system", "ESP32-P4 streaming active")) {
          ESP_LOGI(TAG, "📤 Sent SEI JSON message #%d", message_counter);
        }
        break;

      case 2:
        if (sei_send_status("uptime", message_counter * 3)) {
          ESP_LOGI(TAG, "📤 Sent SEI status message #%d", message_counter);
        }
        break;
      }
      message_counter++;
    }

    // Wait 3 seconds before next message
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
#else
  ESP_LOGI(TAG, "📡 SEI test messages disabled in settings - task will exit");
  vTaskDelete(NULL);
#endif
}

static int network_event_handler(bool connected) {
  if (connected) {
    ESP_LOGI(TAG, "📶 Network connected - Press BOOT button to start/stop WHIP "
                  "streaming");
    // Don't auto-start anymore - wait for button press
    // RUN_ASYNC(start, { start_webrtc(WHIP_SERVER, WHIP_TOKEN); });
  } else {
    ESP_LOGI(TAG, "📶 Network disconnected - Stopping WHIP stream");
    if (publishing_active) {
      stop_webrtc();
      publishing_active = false;
    }
  }
  return 0;
}

void app_main(void) {
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("AGENT", ESP_LOG_DEBUG);
  esp_log_level_set("WHIP_SIGNALING", ESP_LOG_DEBUG);

  // Debug: Check if OPUS is properly defined
#ifdef WEBRTC_SUPPORT_OPUS
  ESP_LOGI(TAG, "✅ WEBRTC_SUPPORT_OPUS is defined");
#else
  ESP_LOGW(TAG, "⚠️  WEBRTC_SUPPORT_OPUS is NOT defined");
#endif

  media_lib_add_default_adapter();
  esp_capture_set_thread_scheduler(capture_scheduler);
  media_lib_thread_set_schedule_cb(thread_scheduler);
  init_board();
  media_sys_buildup();
  init_console();

  // Configure button GPIO
  gpio_config_t button_config = {
      .pin_bit_mask = (1ULL << BUTTON_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE, // Enable pull-up for BOOT button
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&button_config);

  ESP_LOGI(TAG,
           "🔘 Button configured on GPIO %d - Press BOOT button to toggle WHIP "
           "streaming",
           BUTTON_GPIO);

  // Check heap status before WiFi init
  size_t free_heap = esp_get_free_heap_size();
  size_t min_free_heap = esp_get_minimum_free_heap_size();
  ESP_LOGI(TAG, "💾 Heap status: %zu bytes free, %zu bytes minimum", free_heap,
           min_free_heap);

  // Initialize SEI system
  if (sei_init() && video_sei_hook_init()) {
    sei_system_active = true;
    ESP_LOGI(TAG, "📡 SEI system initialized successfully");
    // Small delay to let system stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
  } else {
    ESP_LOGE(TAG, "❌ Failed to initialize SEI system");
    sei_system_active = false;
  }

#if SEI_ENABLE_DHT11
  // Initialize DHT-11 sensor
  if (dht11_init()) {
    ESP_LOGI(TAG, "🌡️  DHT-11 sensor ready - readings will be published via SEI every %d seconds", DHT11_READ_INTERVAL_MS / 1000);
  } else {
    ESP_LOGE(TAG, "❌ DHT-11 sensor initialization failed");
  }
#else
  ESP_LOGI(TAG, "🌡️  DHT-11 sensor support disabled in settings");
#endif

  // Create button monitoring task with larger stack
  xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);

  // Create SEI message publishing task with larger stack and lower priority
  if (sei_system_active) {
#if SEI_ENABLE_TEST_MESSAGES
    xTaskCreate(sei_message_task, "sei_message_task", 8192, NULL, 3, NULL);
    ESP_LOGI(TAG, "📡 SEI test message task created successfully");
#else
    ESP_LOGI(TAG, "📡 SEI test messages disabled - task not created");
#endif
  }

#if SEI_ENABLE_DHT11
  // Create DHT-11 sensor task
  if (dht11_initialized && sei_system_active) {
    xTaskCreate(dht11_task, "dht11_task", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "🌡️  DHT-11 sensor task created successfully");
  }
#endif

  // Re-enable WiFi to test without SEI code
  network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler);
  while (1) {
    media_lib_thread_sleep(2000);
    query_webrtc();
  }
}
