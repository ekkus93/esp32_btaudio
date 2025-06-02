# ESP32 Bluetooth Audio Source

This project implements the Bluetooth A2DP audio source component of the ESP32 Audio Project. It receives I2S audio data from another ESP32 and streams it to Bluetooth speakers or headphones.

## Features

- **A2DP Audio Source:** Implements the Bluetooth Advanced Audio Distribution Profile
- **I2S Audio Input:** Receives digital audio via I2S interface from the WiFi Controller ESP32
- **Serial Command Interface:** Accepts control commands over UART
- **Multiple Device Support:** Can scan for and connect to various Bluetooth audio sinks
- **Pairing Management:** Supports different pairing methods including "Just Works" and PIN-based pairing

## Hardware Configuration

### GPIO Assignments (ESP32 WROOM32)

**I2S Audio Interface:**
- BCLK (Bit Clock): GPIO26
- WCLK/LRCLK (Word/LR Clock): GPIO25
- DATA IN: GPIO22 (receives audio data from WiFi ESP32)
- DATA OUT: GPIO21 (optional, if needed)

**Serial Communication:**
- Using UART1 (separate from USB programming port UART0)
- RX: GPIO16 (receives commands)
- TX: GPIO17 (sends responses/events)

## Implementation Tasks

- [x] Initial A2DP source implementation
- [ ] Add I2S driver configuration for receiving audio
- [ ] Implement serial command protocol
- [ ] Add pairing management functionality
- [ ] Add volume/mute control
- [ ] Implement device scanning and connection management
- [ ] Add persistent settings storage in NVS

## Serial Command Protocol

This ESP32 accepts commands via UART using the text-based protocol described in the main project README. Key command categories include:

- **Bluetooth Connection Management:** SCAN, CONNECT, DISCONNECT, PAIRED
- **Audio Control:** START, STOP, VOLUME, MUTE, UNMUTE
- **Status and System Commands:** STATUS, VERSION, RESET, DEBUG
- **Audio Configuration:** SAMPLE_RATE, I2S_CONFIG
- **Pairing Commands:** PAIR, CONFIRM_PIN, ENTER_PIN, etc.

## Detailed Serial Command Protocol

All commands use a simple text-based format ending with a newline character (`\n`). Responses follow the `<STATUS>|<COMMAND>|<RESULT>[|<DATA>]` format.

### Connection Commands

| Command | Description | Parameters | Response | Example |
|---------|-------------|------------|----------|---------|
| `SCAN` | Start scanning for Bluetooth devices | None | `INFO` messages for each device found, then `OK` | `SCAN` |
| `CONNECT` | Connect to device by MAC | MAC address | `OK\|CONNECT\|CONNECTED\|<MAC>` or error | `CONNECT AA:BB:CC:DD:EE:FF` |
| `CONNECT_NAME` | Connect to device by name | Device name | Same as CONNECT | `CONNECT_NAME Kitchen Speaker` |
| `DISCONNECT` | Disconnect current connection | None | `OK\|DISCONNECT\|SUCCESS` | `DISCONNECT` |
| `PAIRED` | List paired devices | None | `INFO` for each device, then `OK` | `PAIRED` |
| `SET_NAME` | Set local Bluetooth device name | Name string | `OK\|SET_NAME\|SUCCESS\|<NAME>` | `SET_NAME ESP32_BT_SOURCE` |

### Audio Control Commands

| Command | Description | Parameters | Response | Example |
|---------|-------------|------------|----------|---------|
| `START` | Start audio streaming | None | `OK\|START\|SUCCESS` or error | `START` |
| `STOP` | Stop audio streaming | None | `OK\|STOP\|SUCCESS` | `STOP` |
| `VOLUME` | Set volume level | 0-100 | `OK\|VOLUME\|SET\|<LEVEL>` | `VOLUME 75` |
| `MUTE` | Mute audio output | None | `OK\|MUTE\|SUCCESS` | `MUTE` |
| `UNMUTE` | Unmute audio output | None | `OK\|UNMUTE\|SUCCESS` | `UNMUTE` |

### Pairing Commands

| Command | Description | Parameters | Response | Example |
|---------|-------------|------------|----------|---------|
| `PAIR` | Initiate pairing with device | MAC address | `OK\|PAIR\|STARTED\|<MAC>` or error | `PAIR AA:BB:CC:DD:EE:FF` |
| `CONFIRM_PIN` | Confirm PIN match during SSP | YES/NO | `OK\|CONFIRM_PIN\|<RESULT>` | `CONFIRM_PIN YES` |
| `ENTER_PIN` | Enter PIN when requested | PIN code | `OK\|ENTER_PIN\|ACCEPTED` or rejected | `ENTER_PIN 0000` |
| `SET_DEFAULT_PIN` | Set default PIN code | PIN code | `OK\|SET_DEFAULT_PIN\|SUCCESS\|<PIN>` | `SET_DEFAULT_PIN 1234` |
| `UNPAIR` | Remove specific paired device | MAC address | `OK\|UNPAIR\|SUCCESS\|<MAC>` or error | `UNPAIR AA:BB:CC:DD:EE:FF` |
| `UNPAIR_ALL` | Remove all paired devices | None | `OK\|UNPAIR_ALL\|SUCCESS\|<COUNT>` | `UNPAIR_ALL` |

### System Commands

| Command | Description | Parameters | Response | Example |
|---------|-------------|------------|----------|---------|
| `STATUS` | Get current status | None | `OK\|STATUS\|<BT_STATUS>,<AUDIO_STATUS>,<VOLUME>` | `STATUS` |
| `VERSION` | Get firmware version | None | `OK\|VERSION\|<VERSION_STRING>` | `VERSION` |
| `RESET` | Reset the ESP32 | None | `OK\|RESET\|REBOOTING` | `RESET` |
| `DEBUG` | Toggle debug messages | ON/OFF | `OK\|DEBUG\|<STATE>` | `DEBUG ON` |

### Configuration Commands

| Command | Description | Parameters | Response | Example |
|---------|-------------|------------|----------|---------|
| `SAMPLE_RATE` | Set I2S sample rate | Rate in Hz | `OK\|SAMPLE_RATE\|SET\|<RATE>` | `SAMPLE_RATE 44100` |
| `I2S_CONFIG` | Configure I2S pins | BCLK,WCLK,DOUT,DIN | `OK\|I2S_CONFIG\|SUCCESS` | `I2S_CONFIG 26,25,22,21` |

### Events

In addition to command responses, the ESP32 may send unsolicited event messages:

| Event | Description | Format |
|-------|-------------|--------|
| Device found | When scanning detects a device | `INFO\|SCAN\|DEVICE_FOUND\|<MAC>,<NAME>` |
| PIN request | When pairing requires PIN | `EVENT\|PAIR\|PIN_REQUEST\|<MAC>` |
| PIN confirm | When SSP requires confirmation | `EVENT\|PAIR\|CONFIRM\|<PIN>` |
| Pairing result | Result of pairing operation | `EVENT\|PAIR\|SUCCESS\|<MAC>` or FAILED |
| Connection changed | When connection state changes | `EVENT\|CONNECTION\|<STATE>\|<MAC>` |
| Audio state | When audio streaming state changes | `EVENT\|AUDIO\|<STATE>` |

## Example Use Cases

### Connecting to a Bluetooth Speaker
```
> SCAN
< INFO|SCAN|DEVICE_FOUND|AA:BB:CC:DD:EE:FF,Living Room Speaker
< OK|SCAN|COMPLETE|1
> CONNECT AA:BB:CC:DD:EE:FF
< OK|CONNECT|CONNECTED|AA:BB:CC:DD:EE:FF
> START
< OK|START|SUCCESS
```

### Pairing with PIN Authentication
```
> SCAN
< INFO|SCAN|DEVICE_FOUND|11:22:33:44:55:66,Car Audio
< OK|SCAN|COMPLETE|1
> PAIR 11:22:33:44:55:66
< OK|PAIR|STARTED|11:22:33:44:55:66
< EVENT|PAIR|PIN_REQUEST|11:22:33:44:55:66
> ENTER_PIN 0000
< OK|ENTER_PIN|ACCEPTED
< EVENT|PAIR|SUCCESS|11:22:33:44:55:66
> CONNECT 11:22:33:44:55:66
< OK|CONNECT|CONNECTED|11:22:33:44:55:66
```

## Build and Installation Guide

### Prerequisites

- ESP-IDF v4.4 or newer
- Python 3.6 or newer
- Git
- A compatible ESP32 development board (WROOM32)
- USB cable for connecting to your development board

### Setting up the Environment

1. Install ESP-IDF following the [official installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)

2. Activate the ESP-IDF environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh  # Adjust path if necessary
   ```

### Configuring the Project

1. Navigate to the project directory:
   ```bash
   cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source
   ```

2. Configure the project using the ESP-IDF tool:
   ```bash
   idf.py menuconfig
   ```

3. Ensure the following settings are configured:
   - In "Component config" → "Bluetooth" → Enable "Bluedroid Enable"
   - In "Bluedroid Options" → Enable "Classic Bluetooth" and "A2DP"

### Building and Flashing

1. Build the project:
   ```bash
   idf.py build
   ```

2. Flash the firmware to the ESP32:
   ```bash
   idf.py -p PORT flash monitor
   ```
   Replace `PORT` with your device's serial port (e.g., `/dev/ttyUSB0` on Linux)

## Unit Testing Framework

This project uses a dual testing approach for faster Test-Driven Development (TDD):

### 1. Host-based Testing (Primary for TDD)

Host-based tests run on your development computer rather than on the ESP32, providing:
- Much faster test cycles without device flashing
- Better debugging capabilities
- Easy integration with CI/CD pipelines

**Setup:**

1. Create a test directory structure:
   ```
   esp_bt_audio_source/
   ├── test/
   │   ├── host_test/          # Tests that run on development PC
   │   │   ├── CMakeLists.txt
   │   │   ├── test_commands.c
   │   │   ├── test_bluetooth.c
   │   │   └── mocks/          # Mock implementations of ESP-IDF
   │   │       ├── esp_bt.h
   │   │       ├── mock_i2s.c
   │   │       └── mock_uart.c
   ```

2. Create a CMake-based build system for host tests:
   ```bash
   mkdir -p build_host_tests && cd build_host_tests
   cmake ../test/host_test
   make
   ./run_tests
   ```

### 2. On-device Testing (for Integration Verification)

On-device tests use ESP-IDF's Unity framework integration:

1. Create an on-device test component:
   ```
   esp_bt_audio_source/
   ├── test_app/              # Separate test application
   │   ├── main/
   │   │   └── test_app_main.c
   │   ├── components/
   │   └── CMakeLists.txt
   ```

2. Use ESP-IDF's built-in Unity test framework:
   ```c
   #include "unity.h"
   #include "bt_source.h"
   
   TEST_CASE("Test Bluetooth connection", "[bluetooth]")
   {
       // Test code here
       TEST_ASSERT_EQUAL(ESP_OK, bt_init());
   }
   ```

3. Flash and run:
   ```bash
   cd test_app
   idf.py -p PORT flash monitor
   ```

### 3. Component Design for Testability

To make your code more testable:

- Create clear interfaces and modules with single responsibilities
- Use dependency injection to allow mocking of ESP-IDF components
- Separate hardware-dependent code from business logic
- Use function pointers for hardware access to enable mocks

**Example testable component structure:**
```
esp_bt_audio_source/
├── components/
│   ├── bt_manager/          # Bluetooth functionality
│   │   ├── include/         # Public headers
│   │   └── bt_manager.c     # Implementation
│   ├── command_interface/   # Serial command handling
│   │   ├── include/
│   │   └── commands.c
│   └── audio_pipeline/      # Audio processing 
```

### 4. Sample Test Implementation 

Example test for the command parser:

```c
// test_commands.c
#include "unity.h"
#include "command_interface.h"

void setUp(void) {
    // Initialize before each test
    cmd_init();
}

void tearDown(void) {
    // Clean up after each test
    cmd_deinit();
}

TEST_CASE("Parse SCAN command", "[commands]") {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SCAN", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_SCAN, ctx.type);
}
```

Running tests becomes part of your normal development cycle:
1. Write a failing test
2. Implement functionality to pass the test
3. Refactor while keeping tests passing
4. Repeat

## Project Architecture

This project follows a modular architecture with several key components:

### Component Structure

The ESP32 Bluetooth Audio Source is organized into the following components:

#### 1. Audio Component (`/components/audio`)
The audio component handles all audio processing functionality:
- I2S driver configuration and initialization
- Audio buffer management for receiving and processing data
- PCM format handling (bit depth, endianness)
- Stereo/mono channel configuration
- Sample rate conversion and validation
- Audio pipeline management for processing blocks

#### 2. Bluetooth Manager (`/components/bt_manager`)
The Bluetooth manager handles all Bluetooth-related functionality:
- Bluetooth stack initialization and configuration
- A2DP source profile implementation
- Device scanning, discovery, and connection management
- Pairing and security (PIN, SSP, "Just Works")
- Audio streaming control
- Bluetooth event handling and notifications
- Device persistence (paired devices storage/retrieval)

#### 3. Command Interface (`/components/command_interface`)
The command interface provides a serial control protocol:
- UART communication handling
- Command parsing and validation
- Command execution routing
- Response formatting and sending
- Asynchronous event notifications
- Error handling and reporting

### Main Application Structure

The `/main` directory contains the core application files that integrate all components. Here's a breakdown of the key files:

#### Core Files

##### `main.c`
- **Purpose**: Application entry point
- **Key Functions**:
  - `app_main()`: Main entry function called by ESP-IDF
  - System initialization sequence
  - Component initialization and coordination
  - Main event loop handling
- **Integration Points**: Initializes and connects all components (bt_manager, audio, command_interface)

##### `bt_source.h`
- **Purpose**: Bluetooth A2DP source public interface
- **Key Declarations**:
  - Device type and profile enumerations
  - Bluetooth device structures
  - Connection and pairing state structures
  - API functions for Bluetooth initialization, scanning, pairing, and streaming
- **Usage**: Imported by components needing Bluetooth functionality

##### `i2s_audio.c/h`
- **Purpose**: I2S audio interface implementation
- **Key Functions**:
  - I2S driver initialization and configuration 
  - Audio buffer management
  - Sample rate and format handling
- **Hardware Interaction**: Configures ESP32's I2S peripheral for audio input

##### `nvs_storage.c/h`
- **Purpose**: Persistent storage implementation using ESP32's NVS
- **Key Functions**:
  - Store and retrieve paired device information
  - Save configuration settings and parameters
  - Maintain settings across reboots
- **Integration**: Used by various modules for persistent data storage

##### `system_config.c/h`
- **Purpose**: Global system configuration
- **Key Features**:
  - Default settings and parameters
  - System-wide configuration structures
  - Configuration loading/saving functions
- **Usage**: Provides configuration context for all components

#### Utility Files

##### `utils.c/h`
- **Purpose**: Common utility functions
- **Key Features**:
  - Logging helpers
  - Buffer manipulation
  - String parsing
  - Error handling macros
- **Usage**: Used throughout the codebase for common operations

##### `debug.c/h`
- **Purpose**: Debugging support
- **Key Functions**:
  - Debug message formatting
  - Conditional debug output
  - Runtime debug level control
- **Usage**: Provides enhanced debugging capabilities beyond ESP_LOG

This main application structure follows a modular design pattern, where each file has a clear responsibility and interfaces with other parts through well-defined APIs. This organization facilitates testing, maintenance, and future enhancements.

## Additional Resources

See the [main project README](/home/phil/work/esp32/esp32_btaudio/README.md) for:
- Complete system architecture
- Detailed command protocol specification
- Connection diagram for both ESP32s
- Audio format requirements
