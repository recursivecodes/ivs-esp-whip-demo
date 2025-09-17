# Amazon IVS ESP32-P4 WHIP Demo

## Overview

This standalone demo shows how to use an ESP32-P4 board as a WHIP publish client to stream media directly to Amazon IVS (Interactive Video Service) using WebRTC. This is a standalone version of the demo originally located in the [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) repository.

## Prerequisites

### Hardware Requirements

- An [ESP32P4-Function-Ev-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html) (includes a SC2336 camera)

### Software Requirements

- ESP-IDF v5.4 or newer
- The [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) repository

### Amazon IVS Setup

1. **Create an IVS Stage** in your AWS account
2. **Deploy a Token Vending Endpoint** - A publicly accessible API endpoint that can generate IVS stage participant tokens

## Setup Instructions

### 1. Clone Dependencies

First, clone the required esp-webrtc-solution repository:

```bash
git clone https://github.com/espressif/esp-webrtc-solution.git
```

### 2. Configure Build System

Copy the CMakeLists template and configure for your environment:

```bash
cp CMakeLists.txt.template CMakeLists.txt
```

Edit `CMakeLists.txt` and update the paths to point to your local esp-webrtc-solution directory:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS "/path/to/your/esp-webrtc-solution/components")
list(APPEND EXTRA_COMPONENT_DIRS "/path/to/your/esp-webrtc-solution/solutions/common")
```

### 3. Configure Settings

Copy the settings template and configure for your environment:

```bash
cp main/settings.h.template main/settings.h
```

**Note**: Both `CMakeLists.txt` and `main/settings.h` are excluded from git to keep your personal configuration private.

Edit `main/settings.h` with your specific configuration:

#### Required Settings

- **WIFI_SSID**: Your Wi-Fi network name
- **WIFI_PASSWORD**: Your Wi-Fi password
- **STAGE_ARN**: Your Amazon IVS stage ARN (format: `arn:aws:ivs:region:account:stage/stage-id`)
- **TOKEN_API_URL**: Your token vending endpoint URL
- **PARTICIPANT_NAME**: Unique identifier for this ESP32 device
- **SEI_ENABLE_TEST_MESSAGES**: Enable/disable automatic SEI test messages (true/false)
- **SEI_ENABLE_DHT11**: Enable/disable DHT-11 sensor readings via SEI (true/false)

#### Token Vending Endpoint

The `TOKEN_API_URL` must point to a publicly accessible endpoint that:

- Accepts POST requests with JSON payload containing `stageArn`, `capabilities`, and `attributes`
- Returns a valid IVS stage participant token
- Example payload:
  ```json
  {
    "stageArn": "arn:aws:ivs:us-east-1:123456789012:stage/abcd1234",
    "capabilities": ["PUBLISH"],
    "attributes": { "username": "esp32-p4" }
  }
  ```
- Example response:
  ```json
  {
    "attributes": { "username": "esp32-p4" },
    "capabilities": ["PUBLISH"],
    "duration": 60,
    "expirationTime": "2025-09-16T21:53:01.000Z",
    "participantId": "Y5qm2ej9FVBO",
    "token": "eyJhbGciOiJLTVMiLCJ0eXAiOiJKV1QifQ..."
  }
  ```

You can implement this using AWS Lambda or any web service that can call the IVS `CreateParticipantToken` API.

#### Example Node.js Implementation

Here's a complete AWS Lambda function using the AWS SDK for JavaScript v3:

```javascript
import { IVSRealTimeClient, CreateParticipantTokenCommand } from "@aws-sdk/client-ivs-realtime";

const client = new IVSRealTimeClient({ region: "us-east-1" });

export const handler = async (event) => {
  try {
    // Parse the request body
    const body = JSON.parse(event.body);
    const { stageArn, capabilities = ["PUBLISH"], attributes = {} } = body;

    // Validate required parameters
    if (!stageArn) {
      return {
        statusCode: 400,
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ error: "stageArn is required" }),
      };
    }

    // Create the participant token
    const command = new CreateParticipantTokenCommand({
      stageArn,
      capabilities,
      attributes,
      duration: 60, // Token valid for 60 minutes
    });

    const response = await client.send(command);

    return {
      statusCode: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*", // Add CORS if needed
      },
      body: JSON.stringify({
        token: response.participantToken.token,
        participantId: response.participantToken.participantId,
        expirationTime: response.participantToken.expirationTime,
        capabilities: response.participantToken.capabilities,
        attributes: response.participantToken.attributes,
        duration: response.participantToken.duration,
      }),
    };
  } catch (error) {
    console.error("Error creating participant token:", error);
    return {
      statusCode: 500,
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        error: "Failed to create participant token",
        details: error.message,
      }),
    };
  }
};
```

Make sure your Lambda function has the necessary IAM permissions:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": ["ivs:CreateParticipantToken"],
      "Resource": "arn:aws:ivs:*:*:stage/*"
    }
  ]
}
```

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

- **Button Press**: Press the GPIO button on the ESP32-P4 board to start/stop WHIP streaming

### CLI Commands

You can control the device via serial console:

- `start` : Begin streaming to IVS
- `stop` : Stop streaming
- `i` : Display system information
- `wifi <ssid> <password>` : Connect to a new Wi-Fi network

### SEI Publishing

This demo includes SEI (Supplemental Enhancement Information) publishing capabilities for embedding metadata directly into H.264 video streams. For detailed information about SEI features, configuration, and usage, see [SEI_README.md](SEI_README.md).

### DHT-11 Sensor Integration

The demo supports optional DHT-11 temperature and humidity sensor integration that publishes sensor readings as JSON metadata via SEI every 5 seconds during streaming. Uses the reliable esp-idf-lib DHT component library for robust sensor communication. For wiring instructions and configuration details, see [DHT11_SETUP.md](DHT11_SETUP.md).

#### SEI Test Messages

The demo can automatically send test SEI messages every 3 seconds during streaming. This feature is controlled by the `SEI_ENABLE_TEST_MESSAGES` setting in `main/settings.h`:

- Set to `true` to enable automatic test messages (default)
- Set to `false` to disable automatic test messages and only use manual CLI commands

SEI CLI commands:

- `sei_text <message>` : Send text message via SEI
- `sei_json <role> <content>` : Send JSON message via SEI
- `sei_raw_json <json>` : Send raw JSON message via SEI without wrapping
- `sei_status` : Show SEI system status and statistics
- `sei_clear` : Clear SEI message queue
- `sei_test_hook` : Test SEI hook with fake frame (for debugging)

DHT-11 CLI commands (when enabled):

- `dht11_read` : Manually read DHT-11 sensor and publish via SEI
- `dht11_status` : Show DHT-11 sensor status and last readings

### Viewing the Stream

Once streaming, you can view the live stream through:

- Amazon IVS console (if stage is configured for playback)
- Your application using the IVS Player SDK
- WebRTC-enabled applications connected to the same stage

## Technical Details

This demo uses the `esp_signaling_get_whip_impl` implementation to establish WebRTC connections with Amazon IVS. The process includes:

1. **Token Request**: Fetches a participant token from your configured endpoint
2. **WHIP Handshake**: Exchanges SDP offers/answers with IVS WHIP endpoint
3. **ICE Negotiation**: Establishes peer-to-peer connection using STUN/TURN servers
4. **Media Streaming**: Transmits camera feed using H.264 video and audio codecs

For detailed WebRTC connection flow, refer to the [esp-webrtc documentation](https://github.com/espressif/esp-webrtc-solution/blob/main/components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).

## Troubleshooting

- **Build errors**: Ensure CMakeLists.txt paths point to your local esp-webrtc-solution directory
- **Connection issues**: Verify your token endpoint is accessible and returns valid tokens
- **Streaming problems**: Check that your IVS stage ARN is correct and the stage is active
