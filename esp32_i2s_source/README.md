# ESP32 I2S Audio Source with WebSocket Interface

This project implements an ESP32-based I2S audio source with a WebSocket interface for control. It generates audio data and sends it to the Bluetooth Audio Source ESP32 via I2S.

## Build and Installation Guide

### Prerequisites

- ESP-IDF v4.4 or newer
- Python 3.6 or newer
- Git
- A compatible ESP32 development board (WROOM32)
- USB cable for connecting to your development board
- WiFi network for the WebSocket interface

### Setting up the Environment

1. Install ESP-IDF following the [official installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)

2. Activate the ESP-IDF environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh  # Adjust path if necessary
   ```

### Configuring the Project

1. Navigate to the project directory:
   ```bash
   cd /home/phil/work/esp32/esp32_btaudio/esp32_i2s_source
   ```

2. Configure the project using the ESP-IDF tool:
   ```bash
   idf.py menuconfig
   ```

3. Configure WiFi settings:
   - Navigate to "Example Connection Configuration"
   - Enter your WiFi SSID and password

4. Configure any additional project settings:
   - I2S pin assignments (if necessary)
   - Other project-specific options

### Building the Project

1. Build the project:
   ```bash
   idf.py build
   ```

2. This will generate the binary files in the `build` directory.

### Flashing to ESP32

1. Connect your ESP32 board to your computer with a USB cable.

2. Flash the firmware to the ESP32:
   ```bash
   idf.py -p PORT flash
   ```
   Replace `PORT` with your device's serial port:
   - Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
   - macOS: `/dev/cu.usbserial-XXX`
   - Windows: `COM3` (or other COM port number)

3. To build, flash, and monitor output in one command:
   ```bash
   idf.py -p PORT flash monitor
   ```

4. To exit the serial monitor, press `Ctrl+]`.

### Verifying the Installation

1. After flashing, the ESP32 will restart and attempt to connect to the configured WiFi network.

2. Check the serial monitor for IP address information:
   ```
   I (4942) example_connect: Connected to [YOUR_WIFI_SSID]
   I (4942) example_connect: IPv4 address: 192.168.x.x
   ```

3. Once connected, the WebSocket server will start:
   ```
   I (4962) ws_server: Starting server on port: '80'
   I (4962) ws_server: Registering URI handlers
   ```

4. The I2S output will be available on the configured pins to connect to the Bluetooth ESP32.

## WebSocket API

The WebSocket interface allows control over the I2S audio source and provides a bridge to the Bluetooth ESP32. Commands and responses are sent as JSON objects.

### Command Format

Commands are sent as JSON objects:
```json
{
  "cmd": "COMMAND_NAME",
  "params": {
    "param1": "value1",
    "param2": "value2"
  }
}
```

Responses are also JSON objects:
```json
{
  "status": "ok|error|info",
  "cmd": "COMMAND_NAME",
  "result": "RESULT",
  "data": {
    "key1": "value1",
    "key2": "value2"
  }
}
```

### WebSocket Commands

#### Audio Source Control
| Command | Description | Parameters | Example |
|---------|-------------|------------|---------|
| `audio_source` | Set audio source | `{"type": "sine/mp3/stream"}` | `{"cmd": "audio_source", "params": {"type": "sine"}}` |
| `play` | Start audio playback | None | `{"cmd": "play"}` |
| `stop` | Stop audio playback | None | `{"cmd": "stop"}` |
| `volume` | Set volume | `{"level": 0-100}` | `{"cmd": "volume", "params": {"level": 75}}` |

#### Bluetooth Control (Pass-through to Bluetooth ESP32)
| Command | Description | Parameters | Example |
|---------|-------------|------------|---------|
| `bt_scan` | Scan for Bluetooth devices | None | `{"cmd": "bt_scan"}` |
| `bt_connect` | Connect to Bluetooth device | `{"mac": "XX:XX:XX:XX:XX:XX"}` | `{"cmd": "bt_connect", "params": {"mac": "AA:BB:CC:DD:EE:FF"}}` |
| `bt_status` | Get Bluetooth status | None | `{"cmd": "bt_status"}` |

#### System Commands
| Command | Description | Parameters | Example |
|---------|-------------|------------|---------|
| `wifi_status` | Get WiFi connection status | None | `{"cmd": "wifi_status"}` |
| `system_info` | Get system information | None | `{"cmd": "system_info"}` |
| `restart` | Restart the ESP32 | None | `{"cmd": "restart"}` |

### WebSocket Events

The server will push events to connected clients:

| Event | Description | Format |
|-------|-------------|--------|
| `bt_status_change` | Bluetooth status update | `{"event": "bt_status_change", "data": {"status": "connected", "device": "Speaker"}}` |
| `audio_status` | Audio playback status change | `{"event": "audio_status", "data": {"status": "playing", "source": "sine"}}` |
| `bt_scan_result` | Device found during scan | `{"event": "bt_scan_result", "data": {"mac": "AA:BB:CC:DD:EE:FF", "name": "Speaker"}}` |

### Example WebSocket Communication

```
// Client -> Server: Start scanning for Bluetooth devices
{"cmd": "bt_scan"}

// Server -> Client: Device found (event)
{"event": "bt_scan_result", "data": {"mac": "AA:BB:CC:DD:EE:FF", "name": "Living Room Speaker"}}

// Server -> Client: Scan complete
{"status": "ok", "cmd": "bt_scan", "result": "complete", "data": {"count": 1}}

// Client -> Server: Connect to the device
{"cmd": "bt_connect", "params": {"mac": "AA:BB:CC:DD:EE:FF"}}

// Server -> Client: Connection result
{"status": "ok", "cmd": "bt_connect", "result": "connected", "data": {"mac": "AA:BB:CC:DD:EE:FF"}}

// Client -> Server: Start playback
{"cmd": "play"}

// Server -> Client: Playback started
{"status": "ok", "cmd": "play", "result": "success"}
```

## Hardware Connections

Please refer to the main project README for GPIO pin assignments and hardware connections between this ESP32 and the Bluetooth Audio Source ESP32.
