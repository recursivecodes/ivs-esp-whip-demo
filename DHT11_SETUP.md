# DHT-11 Sensor Setup for ESP32-P4-Function-EV-Board

This document provides instructions for connecting and configuring a DHT-11 temperature and humidity sensor to your ESP32-P4-Function-EV-Board for streaming sensor data via SEI (Supplemental Enhancement Information).

## Hardware Requirements

- ESP32-P4-Function-EV-Board
- DHT-11 temperature and humidity sensor module
- 3 female-to-female jumper wires
- Breadboard (optional, for easier connections)

## DHT-11 Sensor Overview

The DHT-11 is a basic, low-cost digital temperature and humidity sensor. It provides:

- Temperature range: 0-50°C (±2°C accuracy)
- Humidity range: 20-90% RH (±5% accuracy)
- 3.3V or 5V operation (3.3V recommended for ESP32-P4)
- Single digital output pin

## Wiring Instructions

### DHT-11 Pinout

```
DHT-11 Module:
┌──────────────┐
│  +   DATA  - │
│ VCC  OUT  GND│
└──────────────┘
```

### ESP32-P4-Function-EV-Board Connections

Based on the J1 header pinout, connect the DHT-11 as follows:

| DHT-11 Pin | ESP32-P4 J1 Header | Wire Color (Suggested) |
| ---------- | ------------------ | ---------------------- |
| VCC (+)    | Pin 1 (3V3)        | Red                    |
| DATA (OUT) | Pin 7 (GPIO23)     | Yellow/Green           |
| GND (-)    | Pin 6 (GND)        | Black                  |

### Step-by-Step Wiring

1. **Power Off** the ESP32-P4 board before making connections
2. **Connect VCC**: DHT-11 VCC → J1 Pin 1 (3V3) - Red wire
3. **Connect Data**: DHT-11 DATA → J1 Pin 7 (GPIO23) - Yellow wire
4. **Connect Ground**: DHT-11 GND → J1 Pin 6 (GND) - Black wire

### J1 Header Reference (Top View)

Connect the DHT-11 to the red pins as shown below:

```
J1 Header Layout:
Pin 1  [3V3]    🔴⚪️  [5V] Pin 2
Pin 3  [VOUT]   ⚪️⚪️  [5V] Pin 4
Pin 5  [GPIO8]  ⚪️🔴  [GND] Pin 6
Pin 7  [GPIO23] 🔴⚪️  [GPIO37] Pin 8
Pin 9  [GND]    ⚪️⚪️  [GPIO38] Pin 10
...
```

**Important**: Use Pin 7 (GPIO23) for the DHT-11 data connection.

## Software Configuration

### 1. Library Dependency

This implementation uses the esp-idf-lib DHT component for reliable sensor communication. The component is automatically downloaded when you build the project via the ESP-IDF Component Manager.

The dependency is configured in `main/idf_component.yml`:

```yaml
dependencies:
  esp-idf-lib/dht:
    version: "^1.1.7"
```

### 2. Enable DHT-11 Support

Edit your `main/settings.h` file and change:

```c
/**
 * @brief  Enable DHT-11 sensor support for temperature and humidity readings via SEI
 */
#define SEI_ENABLE_DHT11 true  // Change from false to true
```

### 3. GPIO Pin Configuration

The DHT-11 data pin is configured to use **GPIO23** by default. This is defined in the firmware and matches the wiring instructions above.

## Features

When `SEI_ENABLE_DHT11` is enabled, the system will:

1. **Initialize DHT-11** sensor on GPIO23 during startup
2. **Read sensor data** every 5 seconds during streaming
3. **Publish via SEI** temperature and humidity as JSON metadata
4. **Handle errors** gracefully if sensor is disconnected or fails

### SEI Data Format

The DHT-11 readings are published as raw JSON via SEI in this format:

```json
{
  "sensor": "DHT11",
  "temperature_c": 23.5,
  "humidity_percent": 65.2,
  "timestamp": 1234567890,
  "status": "ok",
  "type": "sensor_data"
}
```

**Note**: This uses the `sei_send_raw_json()` function to send the complete JSON object without additional wrapping, ensuring clean JSON structure on the receiving side.

## Testing Your Setup

### 1. Hardware Test

1. Power on the ESP32-P4 board
2. Check the serial console for DHT-11 initialization messages
3. Look for temperature/humidity readings in the logs

### 2. SEI Stream Test

1. Start WHIP streaming (press BOOT button or use `start` command)
2. Use `sei_status` command to check if DHT-11 data is being queued
3. Monitor your video stream for embedded sensor metadata

### 3. Manual Commands

- `dht11_read` - Manually read DHT-11 sensor and publish via SEI
- `dht11_status` - Show DHT-11 sensor status and last readings
- `sei_raw_json <json>` - Send custom raw JSON message via SEI
- `sei_status` - Check SEI queue and sensor status
- `sei_clear` - Clear the SEI message queue if needed

## Troubleshooting

### Common Issues

**No sensor readings:**

- Check wiring connections, especially power (3V3) and ground
- Verify GPIO23 connection to DHT-11 DATA pin
- Ensure DHT-11 module is compatible (some require 5V)
- Check that the DHT-11 module has a built-in pull-up resistor
- Try using `dht11_read` command to test manually
- Wait at least 2 seconds between readings (DHT-11 limitation)
- The esp-idf-lib library handles timing automatically for better reliability

**Intermittent readings:**

- DHT-11 sensors can be sensitive to timing and interference
- Try a different DHT-11 module if readings are unreliable
- Check for loose connections
- Ensure stable power supply (avoid long wires for power)
- Try reducing system load during sensor readings

**SEI not working:**

- Ensure `SEI_ENABLE_DHT11` is set to `true` in settings.h
- Verify streaming is active (DHT-11 only publishes during streaming)
- Check SEI system status with `sei_status` command

### Serial Console Messages

Look for these messages during startup:

```
I (xxxx) DHT11: DHT-11 sensor initialized on GPIO23
I (xxxx) DHT11: DHT-11 readings will be published via SEI every 5 seconds
```

During operation:

```
I (xxxx) DHT11: Temperature: 23.5°C, Humidity: 65.2%
I (xxxx) DHT11: DHT-11 data published via SEI
```

## Hardware Notes

- **Voltage**: DHT-11 works with 3.3V from the ESP32-P4 (no level shifting needed)
- **Pull-up Resistor**: Most DHT-11 modules include a built-in pull-up resistor (4.7kΩ - 10kΩ)
- **Update Rate**: DHT-11 sensors should not be read faster than once every 2 seconds
- **Accuracy**: DHT-11 is a basic sensor; consider DHT-22 for better accuracy
- **Library**: Uses the reliable esp-idf-lib DHT library for robust sensor communication

## Safety

- Always power off the board before making wiring changes
- Double-check connections before powering on
- Use proper wire gauge for connections (22-26 AWG recommended)
- Avoid short circuits between power and ground pins

## Next Steps

Once your DHT-11 is working:

1. Monitor the embedded sensor data in your video stream
2. Build applications that consume the SEI metadata
3. Consider adding more sensors using available GPIO pins
4. Explore other ESP32-P4 peripherals for expanded functionality

For more information about SEI features, see [SEI_README.md](SEI_README.md).
