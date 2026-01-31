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
  - **NVS (Non-Volatile Storage) initialization** - SINGLE call to `nvs_storage_init()` early in boot
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

## Initialization Ownership and Layering

### NVS (Non-Volatile Storage) Ownership
**Decision:** main.c owns NVS initialization (Option A)

**Rationale:**
- NVS is a **platform service** (like memory, filesystems) - it should be initialized once at boot
- Multiple components use NVS (bt_manager, audio_processor, nvs_storage abstraction)
- **Single ownership** prevents redundant init calls and race conditions
- Early initialization ensures NVS is ready before any component needs it
- Follows ESP-IDF best practice: platform services initialized in app_main()

**Implementation:**
- main.c calls `nvs_storage_init()` **once** early in app_main() (after BLE mem release, before BT init)
- `nvs_storage_init()` handles version mismatch and erase-on-error internally
- bt_manager, audio_processor, and other components **assume NVS is already initialized**
- Components call nvs_storage_get/set_* functions directly without re-initializing

**What NOT to do:**
- ❌ bt_manager must NOT call `nvs_flash_init()` or `nvs_storage_init()`
- ❌ No component should redundantly initialize NVS "just in case"
- ❌ No lazy initialization - NVS must be ready before subsystems start

**Benefits:**
- Clear ownership (main.c owns platform, components own logic)
- Prevents duplicate initialization
- Easier to reason about boot sequence
- Supports future two-ESP32 architecture (each ESP32's main.c initializes its own NVS)

### UART (Console) Ownership
**Decision:** Option C - Split ownership: main.c installs early for diagnostics, cmd_init assumes ready

**Rationale:**
- Early boot diagnostics are **critical** for programmatic test harness and host injectors
- Diagnostic markers (DIAG|BOOT|EARLY_BOOT_MARKER, DIAG|BOOT|UART_READY_FOR_CMD_LAYER) must appear before subsystem init
- printf() and esp_rom_printf() alone are **insufficient** - they are buffered/unreliable for host capture
- UART driver installation is required for unbuffered uart_write_bytes() diagnostic output
- cmd_init() and other components need UART already operational for synchronous I/O
- **Single install** at boot avoids driver reinstall complexity and state confusion

**Implementation:**
- main.c installs UART driver **once** early in app_main() (after early boot markers, before NVS)
- Installation is **best-effort** with error checking but continues on failure
- main.c uses uart_write_bytes() for critical diagnostic markers before subsystems init
- cmd_init() and command_interface **assume UART is already installed** - no reinstall
- command_interface checks `uart_is_driver_installed()` before writes (defensive but not required)

**What NOT to do:**
- ❌ main.c must NOT call `uart_driver_delete()` after install (breaks logging, esp-console, cmd layer)
- ❌ cmd_init() must NOT reinstall UART driver (causes state reset, double-init)
- ❌ No component should assume UART is uninstalled and try to install it

**Boot sequence:**
1. Very early: printf/esp_rom_printf for EARLY_BOOT_MARKER (before driver)
2. Early: main.c installs UART driver for unbuffered diagnostics
3. Early: main.c writes UART_READY_FOR_CMD_LAYER via uart_write_bytes()
4. Platform init: NVS, BT manager (all emit diagnostic markers)
5. cmd_init: command interface ready (assumes UART operational)

**Benefits:**
- Early diagnostics reliable and visible to test harness
- UART installed exactly once - no reinstall complexity
- cmd_init can immediately read/write without driver setup
- Clear contract: main.c owns platform UART, cmd_init owns command protocol
- Supports test injection and automated capture from first boot moment

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