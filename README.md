# Amazon IVS ESP32-P4 WHIP Demo

## Overview

This standalone demo shows how to use an ESP32-P4 board as a WHIP publish client to stream media directly to Amazon IVS (Interactive Video Service) using WebRTC. This is a standalone version of the demo originally located in the [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) repository.

## Prerequisites

### Hardware Requirements

-   An [ESP32P4-Function-Ev-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32p4-function-ev-board/user_guide.html) (includes a SC2336 camera)

### Software Requirements

-   ESP-IDF v5.4 or master branch
-   The [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) repository

### Amazon IVS Setup

1. **Create an IVS Stage** in your AWS account
2. **Deploy a Token Vending Endpoint** - A publicly accessible API endpoint that can generate IVS stage participant tokens

## Setup Instructions

### 1. Clone Dependencies

First, clone the required esp-webrtc-solution repository:

```bash
git clone https://github.com/espressif/esp-webrtc-solution.git
```

### 2. Update CMakeLists.txt

Update the `CMakeLists.txt` file to point to your local esp-webrtc-solution directory:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS "/path/to/your/esp-webrtc-solution/components")
list(APPEND EXTRA_COMPONENT_DIRS "/path/to/your/esp-webrtc-solution/solutions/common")
```

### 3. Configure Settings

Copy the settings template and configure for your environment:

```bash
cp main/settings.h.template main/settings.h
```

Edit `main/settings.h` with your specific configuration:

#### Required Settings

-   **WIFI_SSID**: Your Wi-Fi network name
-   **WIFI_PASSWORD**: Your Wi-Fi password
-   **STAGE_ARN**: Your Amazon IVS stage ARN (format: `arn:aws:ivs:region:account:stage/stage-id`)
-   **TOKEN_API_URL**: Your token vending endpoint URL
-   **PARTICIPANT_NAME**: Unique identifier for this ESP32 device

#### Token Vending Endpoint

The `TOKEN_API_URL` must point to a publicly accessible endpoint that:

-   Accepts POST requests with JSON payload containing `stageArn`, `capabilities`, and `attributes`
-   Returns a valid IVS stage participant token
-   Example payload:
    ```json
    {
        "stageArn": "arn:aws:ivs:us-east-1:123456789012:stage/abcd1234",
        "capabilities": ["PUBLISH"],
        "attributes": { "username": "esp32-p4" }
    }
    ```

You can implement this using AWS Lambda, API Gateway, or any web service that can call the IVS `CreateParticipantToken` API.

## Building and Flashing

1. **Set up ESP-IDF environment**:

    ```bash
    . $HOME/esp/esp-idf/export.sh
    ```

2. **Build and flash**:
    ```bash
    idf.py -p YOUR_SERIAL_DEVICE flash monitor
    ```

## Usage

After booting, the board will:

1. Connect to the configured Wi-Fi network
2. Wait for GPIO button press or CLI command to start streaming
3. Request a participant token from your endpoint
4. Connect to the Amazon IVS stage and begin streaming

### GPIO Control

-   **Button Press**: Press the GPIO button on the ESP32-P4 board to start/stop WHIP streaming

### CLI Commands

You can control the device via serial console:

-   `start` : Begin streaming to IVS
-   `stop` : Stop streaming
-   `i` : Display system information
-   `wifi <ssid> <password>` : Connect to a new Wi-Fi network

### SEI Publishing

This demo includes SEI (Supplemental Enhancement Information) publishing capabilities for embedding metadata directly into H.264 video streams. For detailed information about SEI features, configuration, and usage, see [SEI_README.md](SEI_README.md).

SEI CLI commands:

-   `sei_text <message>` : Send text message via SEI
-   `sei_json <role> <content>` : Send JSON message via SEI
-   `sei_status` : Show SEI system status and statistics
-   `sei_clear` : Clear SEI message queue
-   `sei_test_hook` : Test SEI hook with fake frame (for debugging)

### Viewing the Stream

Once streaming, you can view the live stream through:

-   Amazon IVS console (if stage is configured for playback)
-   Your application using the IVS Player SDK
-   WebRTC-enabled applications connected to the same stage

## Technical Details

This demo uses the `esp_signaling_get_whip_impl` implementation to establish WebRTC connections with Amazon IVS. The process includes:

1. **Token Request**: Fetches a participant token from your configured endpoint
2. **WHIP Handshake**: Exchanges SDP offers/answers with IVS WHIP endpoint
3. **ICE Negotiation**: Establishes peer-to-peer connection using STUN/TURN servers
4. **Media Streaming**: Transmits camera feed using H.264 video and audio codecs

For detailed WebRTC connection flow, refer to the [esp-webrtc documentation](https://github.com/espressif/esp-webrtc-solution/blob/main/components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).

## Troubleshooting

-   **Build errors**: Ensure CMakeLists.txt paths point to your local esp-webrtc-solution directory
-   **Connection issues**: Verify your token endpoint is accessible and returns valid tokens
-   **Streaming problems**: Check that your IVS stage ARN is correct and the stage is active
