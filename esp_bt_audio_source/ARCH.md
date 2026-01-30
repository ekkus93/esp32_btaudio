# ESP32 Bluetooth + WiFi Split Architecture

## Overview

This project uses two ESP32 devices to overcome the limitations of running WiFi and Bluetooth Classic simultaneously on a single ESP32:

1. **ESP32 #1: Bluetooth Audio Source**
   - Handles all Bluetooth A2DP audio streaming
   - Receives audio from ESP32 #2 via I2S
   - Streams audio to Bluetooth speakers/headphones

2. **ESP32 #2: WiFi and Web Interface**
   - Provides WiFi connectivity (Access Point or client)
   - Hosts web server for user interface
   - Sends audio data to Bluetooth ESP32 via I2S
   - Controls Bluetooth ESP32 via UART

This separation ensures better reliability and performance than trying to run both wireless stacks on a single ESP32.

## Detailed Architecture

### ESP32 #1 (Bluetooth-focused)

**Primary Responsibilities:**
- Connect to Bluetooth speakers/headphones using A2DP profile
- Receive and buffer audio data from ESP32 #2
- Stream audio data to connected Bluetooth devices
- Accept control commands from ESP32 #2 (via UART)
- Send status updates to ESP32 #2 (via UART)

**Key Components:**
- Bluetooth Classic A2DP source profile
- I2S slave receiver for audio input
- UART for command/control interface
- Optional: status LEDs

### ESP32 #2 (WiFi-focused)

**Primary Responsibilities:**
- Provide WiFi Access Point or client connection
- Host web server for user interface
- Generate or process audio data
- Send audio data to ESP32 #1
- Send control commands to ESP32 #1
- Receive status updates from ESP32 #1

**Key Components:**
- WiFi stack (AP or client mode)
- Web server with HTML/CSS/JS interface
- I2S master transmitter for audio output
- UART for command/control interface
- Optional: Additional audio processing (effects, volume control)

## Communication Interfaces

### 1. I2S Audio Interface

Used for high-quality digital audio transmission between the ESP32s:

**Connection Diagram:**
```
ESP32 #2 (WiFi)                     ESP32 #1 (Bluetooth)
----------------                    -------------------
I2S_BCK (GPIO26, Master) ---------> I2S_BCK (GPIO26, Slave)
I2S_WS (GPIO25, Master)  ---------> I2S_WS (GPIO25, Slave)
I2S_DO (GPIO22, Master)  ---------> I2S_DI (GPIO22, Slave)
GND                      ---------> GND
```

**Note:** For ESP32 WROOM32 modules, these are recommended GPIO pins for I2S. They avoid pins used for boot modes or connected to internal flash.

### 2. UART Control Interface

Used for commands and status updates between the ESP32s:

**Connection Diagram:**
```
ESP32 #2 (WiFi)                 ESP32 #1 (Bluetooth)
----------------                -------------------
TX (GPIO17)       ------------> RX (GPIO16)
RX (GPIO16)       <------------ TX (GPIO17)
GND               ------------> GND
```

**Note:** These UART pins (GPIO16/17) are chosen to avoid conflicts with other functions on ESP32 WROOM32 modules. For higher reliability, use a baud rate of 115200.

## Audio Pipeline

1. Audio is generated or processed on ESP32 #2 (WiFi)
2. Audio data is sent via I2S to ESP32 #1 (Bluetooth)
3. ESP32 #1 receives the audio via I2S and buffers it
4. ESP32 #1 streams the audio via A2DP to connected Bluetooth speakers/headphones

This separation allows each ESP32 to focus on its primary wireless protocol, ensuring better performance and reliability.

## Future Expansion Possibilities

- Add a microSD card to ESP32 #2 for audio file playback
- Implement streaming audio from web sources on ESP32 #2
- Add audio effects processing on ESP32 #2
- Use additional GPIO pins for hardware controls (buttons, rotary encoders)
- Add a display to ESP32 #2 for local user interface
- Implement battery level monitoring if devices are battery powered
- Add OTA (Over-The-Air) updates for the WiFi ESP32
- Create mobile app interface for remote control
- Add multi-room audio synchronization with multiple BT transmitters

## Recommended Hardware Configuration

### ESP32 #1 (Bluetooth)
- ESP32 DevKit or similar
- Connected to power source
- Optional: Power LED indicator
- Optional: Status LED for Bluetooth connection

### ESP32 #2 (WiFi)
- ESP32 DevKit or similar with more flash memory (for web interface)
- Connected to power source
- Optional: SSD1306 OLED display for status
- Optional: microSD card for audio storage
- Optional: Control buttons or rotary encoder for volume control

## Software Architecture on ESP32 #1 (Bluetooth)

### main.c Bootstrap (lines 1-226)
- **Purpose:** Clean entry point for ESP32 firmware
- **Responsibilities:**
  - Early boot diagnostics and UART initialization
  - NVS (Non-Volatile Storage) flash initialization
  - Memory optimization (`esp_bt_controller_mem_release` for BLE)
  - Delegate ALL Bluetooth initialization to `bt_manager`
  - Initialize command interface and audio processor
  - Auto-configure I2S pins from NVS storage
- **What main.c MUST NOT contain:**
  - Direct Bluetooth API calls (esp_a2d_*, esp_avrc_*, esp_bt_gap_*, esp_bluedroid_*)
  - Bluetooth state machines or callbacks
  - Device-specific BT logic
- **Policy:** ALL Bluetooth logic lives in the `bt_manager` component (enforced by CI)

### bt_manager Component (Single Source of Truth for BT)
- **Purpose:** Centralized Bluetooth lifecycle management
- **Responsibilities:**
  - Initialize Bluetooth controller and Bluedroid stack
  - Register ALL Bluetooth callbacks (A2DP, AVRCP, GAP)
  - Manage Bluetooth state machines (connection, pairing, streaming)
  - Handle device discovery and pairing
  - Coordinate with audio_processor for streaming
- **Sub-components:**
  - A2DP Source profile implementation
  - AVRCP profile for remote control
  - GAP for device discovery and pairing
  - Connection manager for device lifecycle
  - Streaming manager for audio data flow

### Audio Processor Component
- **Purpose:** Audio pipeline orchestration
- **Responsibilities:**
  - I2S slave configuration and management
  - Audio buffer management and ring buffer
  - Audio queue for multiple sources (I2S, WAV, beep, synth)
  - Coordinate with bt_manager for A2DP streaming
  - Generate keepalive tones and beeps
  - WAV file playback from SPIFFS

### Command Interface Component
- **Purpose:** UART-based control protocol
- **Responsibilities:**
  - Command parser and dispatcher
  - Status reporting to ESP32 #2
  - Error handling and validation
  - Command processing task (polls every 20ms)

## Software Architecture on ESP32 #2 (WiFi)

### WiFi Core
- Access Point or client mode
- Connection management
- Network security

### Web Server
- HTML/CSS/JS interface
- Control endpoints (REST API)
- WebSocket for real-time updates

### Audio Processing
- Audio generation or streaming
- I2S master configuration
- Audio effects (optional)

### UART Command Interface
- Command generation
- Status reception
- Error handling

### User Interface
- Display driver (if display is used)
- Button/encoder handling
- User feedback (LEDs, display)