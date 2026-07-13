# ESP32 Audio Project
[![CI - Host Unit Tests](https://github.com/ekkus93/esp32_btaudio/actions/workflows/ci-host-tests.yml/badge.svg?branch=master)](https://github.com/ekkus93/esp32_btaudio/actions/workflows/ci-host-tests.yml)
[![CI - Device Build](https://github.com/ekkus93/esp32_btaudio/actions/workflows/ci-device-build.yml/badge.svg?branch=master)](https://github.com/ekkus93/esp32_btaudio/actions/workflows/ci-device-build.yml)
[![Coverage](https://img.shields.io/badge/coverage-78.1%25-green.svg)](#code-coverage)

This project centers on two ESP32 devices for an audio streaming pipeline:
- **esp_bt_audio_source** — Bluetooth A2DP audio source firmware (WROOM32 on `/dev/ttyUSB0`)
- **esp_i2s_source** — I2S audio provider (ESP32-S3 on `/dev/ttyACM0`)

The Raspberry Pi (`rpi_i2s_source`) and BeagleBone Green (`bbgw_i2s_source`) I2S source projects have been archived in `archive/`; they are no longer needed as `esp_i2s_source` provides the I2S input.

## Project Status (2026-07-13)

**Completed recently**
- **UART audio streaming (UARTAUDIO):** stream stereo 22.05 kHz PCM from a PC
  over the USB serial cable straight to the Bluetooth speaker/headset — the
  primary developer audio-test path (no I2S wiring needed). Verified
  bit-faithful end-to-end (`tools/compare_bt_capture.py`) and zero-defect to
  real earbuds. Host tool: `esp_bt_audio_source/tools/stream_audio_uart.py`.
- **Three audio-quality root causes fixed** while validating UARTAUDIO:
  audio engine production ceiling (1 chunk per 10 ms FreeRTOS tick — every
  source had been silently zero-filled ~40% since inception), UART ISR moved
  to IRAM, and UART RX-FIFO threshold lowered during streaming.
- **Secondary command UART (UART2, GPIO16/17):** the text command protocol is
  now served on UART2 alongside USB; responses route to the originating port,
  events broadcast to both, and UART2 keeps serving commands while UARTAUDIO
  streaming owns the USB port.
- Audio architecture: 4 sources — I2S, synth, **UART**, silence (beep overlay).
- `test_bluetooth` on-device suite revived (had been link-broken); full test
  estate green: 66 host binaries (~690 cases) + device 46/35/18.
- Earlier: PLAY/WAV playback and the SPIFFS partition removed (1 MB flash
  reclaimed).

**Active TODOs**
- Redo `esp_i2s_source` (planned next; expected to drive this board via UART2)
- Longer-duration UARTAUDIO pytest as an engine-throughput regression guard
- `tools/run_all_tests.py` counts build-failed suites as 0 failures (reporting gap)
- `archive/` contains superseded I2S source projects (rpi_i2s_source, bbgw_i2s_source) — kept for reference only

## System Architecture

## Running Unity Firmware Tests

The on-device Unity suites live in `esp_bt_audio_source/test/test_bluetooth`
(46 tests, BT/pairing/command coverage), `esp_bt_audio_source/test/test_app_audio`
(35 tests) and `esp_bt_audio_source/test/test_manager` (18 tests). The DUT is
assumed to be connected at `/dev/ttyUSB0` unless stated otherwise. Flashing a
test image replaces the production firmware — reflash production afterwards.

1. Ensure the ESP-IDF environment is active:
  ```bash
  . "$HOME/esp/esp-idf/export.sh"
  ```
2. Run one suite with the non-interactive Unity runner (builds, flashes,
   monitors, auto-stops on the Unity summary, saves
   `<suite>/build/one_run_unity.log`):
  ```bash
  cd esp_bt_audio_source
  conda run -n python310 python tools/run_unity.py -p /dev/ttyUSB0 -r test/test_bluetooth
  ```
3. Exit code `0` = all passed; the runner prints an `N/N passed` summary and
   the saved log holds the full Unity output for failure context.

### One-command regression sweep

Use `tools/run_all_tests.py` from the repository root to execute host CTest plus all three Unity suites with fresh builds, SPIFFS flashing, and log aggregation:

```bash
conda run -n python310 python tools/run_all_tests.py --port /dev/ttyUSB0 --timeout 300
```

Outputs:
- Host summary: `tmp/host_ctest_output.log`
- Unity logs: `esp_bt_audio_source/test_app*/build/one_run_unity.log`
- Aggregated counts: `tmp/run_all_tests_summary.json` (authoritative totals) and `tmp/canonical_unity_summary.json`

The script cleans prior artifacts before each run and reports pass/fail counts for every suite. Update the `--port` or `--timeout` arguments if you are using a different device path or need longer runs.

## Code Coverage

The project maintains **78.1% line coverage** across production code, measured using gcov/lcov. Coverage reports are automatically generated in CI and can be generated locally.

### Generate Coverage Report Locally

```bash
# Run tests with coverage enabled
python3 tools/run_all_tests.py --no-device --coverage --no-standalone

# View HTML report
xdg-open tmp/coverage_html/index.html
```

### Coverage Details

The coverage report includes:
- **Line coverage** across all production components
- Excludes: test code, mocks, system headers, build artifacts
- Components covered: audio_processor, bt_manager, command_interface, nvs_storage, platform_shim, util_safe

### CI Coverage Checks

GitHub Actions automatically:
- Runs coverage analysis on every pull request
- Comments coverage percentage on PRs
- Uploads detailed HTML reports as artifacts
- Prevents coverage regressions through visibility

For detailed coverage analysis, download the HTML report artifact from the CI run.

### ESP32 Bluetooth Audio Source
- **Function**: Captures audio input and transmits it over Bluetooth A2DP
- **Input**: I2S audio interface
  - I2S is ideal for this application because:
    - ESP32 has dedicated hardware I2S controllers
    - Supports high-quality digital audio transfer
    - Can interface with many types of audio devices (DACs, ADCs, codecs, microphones)
    - Avoids analog noise issues with direct digital transfer
    - ESP-IDF provides robust I2S drivers
- **Output**: Bluetooth A2DP (Advanced Audio Distribution Profile)
  - Uses SBC codec for audio compression
  - Streams to any A2DP sink device (speakers, headphones, etc.)
- **Control Interface**: Serial UART command system
  - Accept commands to control Bluetooth functions (connect, disconnect, etc.)
  - Configure audio parameters (volume, EQ settings if supported)
  - Report device status and connection information
  - Enable runtime changes without reflashing
  - Bridge communication with the WiFi controller ESP32

### ESP32 WiFi Controller
- **Function**: Provides web interface and network connectivity
- **Features**: TBD

## Hardware Configuration

### ESP32 Bluetooth Audio Source (ESP32 WROOM32)

#### GPIO Assignments

> **⚠️ I2S pin map superseded.** The GPIO26/25/22/21 values below are the old
> single-board plan. The actual two-board I2S contract (S3 slave-TX ↔ WROOM32
> master-RX, 16-bit-in-32-bit slots) is in
> [`esp_i2s_source/docs/SPEC.md`](esp_i2s_source/docs/SPEC.md) §3 and
> [`esp_i2s_source/README.md`](esp_i2s_source/README.md): S3 **BCLK=GPIO15,
> WS=GPIO16, DOUT=GPIO7**. Treat SPEC.md as authoritative for wiring.

**I2S Audio Interface (superseded — see note above):**
- BCLK (Bit Clock): GPIO26
- WCLK/LRCLK (Word/LR Clock): GPIO25
- DATA OUT: GPIO22 (from external device to ESP32)
- DATA IN: GPIO21 (from ESP32 to external device, if needed)

**Serial Communication (implemented as UART2):**
- Secondary command UART, separate from the USB programming port (UART0)
- TX: GPIO17, RX: GPIO16, 115200 8N1 (Kconfig: `CMD_UART2_*`)
- Both UARTs serve the command protocol simultaneously: responses return on
  the port the command arrived on, `EVENT|` lines broadcast to both, and
  UART2 keeps serving commands while UARTAUDIO streaming occupies UART0

**Rationale:**
- These pins avoid boot strapping pins (GPIO0, GPIO2, GPIO12)
- GPIO25/26 are commonly used for I2S and work well together
- GPIO16/17 provide a dedicated UART channel separate from the debug/programming port
- All selected pins are available on most ESP32 development boards
- This configuration leaves GPIO ports available for additional functionality if needed

**Notes:**
- If using an I2S codec chip or DAC/ADC, consult its datasheet for any specific requirements
- The standard programming/debug UART (UART0 on GPIO1/GPIO3) remains available

## Audio Format Requirements

### I2S Input Format for Bluetooth A2DP Transmission
- **Sample Rate**: 44.1kHz (standard for A2DP audio)
- **Channel Configuration**: Stereo (2 channels)
- **Bit Depth**: 16-bit per sample
- **Data Format**: Signed PCM (little-endian)
- **I2S Standard**: Standard Philips I2S protocol
  - BCLK: Bit clock (frequency = sample rate × bit depth × channels)
  - WCLK/LRCLK: Word/Left-Right clock (frequency = sample rate)
  - DATA: Serial data line (MSB first)

### A2DP Transmission Processing
- ESP32 I2S controller receives digital audio data
- ESP32 internal processing converts I2S data to Bluetooth A2DP packets
- SBC encoding is applied (default A2DP codec for ESP32)
- Data is transmitted via Bluetooth to connected A2DP sink devices

**Implementation Note**: The ESP-IDF A2DP source example includes a sample rate converter if needed, but direct 44.1kHz input simplifies processing and provides best audio quality.

## Serial Command Protocol

### Command Format
Commands follow a simple text-based protocol:
```
<COMMAND> [PARAMETERS]
```

Responses follow the format:
```
<STATUS>|<COMMAND>|<RESULT>[|<ADDITIONAL_DATA>]
```
- STATUS: OK, ERR, INFO, EVENT
- COMMAND: The command being responded to
- RESULT: Result of the command
- ADDITIONAL_DATA: Optional additional data

### Command Set

#### Bluetooth Connection Management
- **SCAN** - Start scanning for Bluetooth devices
  - Response: `INFO|SCAN|DEVICE_FOUND|<MAC>,<NAME>` (multiple responses possible)
  - Final response: `OK|SCAN|COMPLETE|<COUNT>`
- **CONNECT <MAC>** - Connect to specific device by MAC address
  - Response: `OK|CONNECT|CONNECTED|<MAC>` or `ERROR|CONNECT|FAILED|<REASON>`
- **CONNECT_NAME <NAME>** - Connect to device by name
  - Response: Same as CONNECT
- **DISCONNECT** - Disconnect current Bluetooth connection
  - Response: `OK|DISCONNECT|SUCCESS`
- **PAIRED** - List paired devices
  - Response: `INFO|PAIRED|DEVICE|<MAC>,<NAME>` (multiple responses possible)
  - Final response: `OK|PAIRED|COMPLETE|<COUNT>`
- **SET_NAME <NAME>** - Set Bluetooth device name
  - Response: `OK|SET_NAME|SUCCESS|<NAME>`

#### Audio Control
- **START** - Start audio transmission
  - Response: `OK|START|SUCCESS` or `ERROR|START|FAILED|<REASON>`
- **STOP** - Stop audio transmission
  - Response: `OK|STOP|SUCCESS`
- **VOLUME <0-100>** - Set volume level
  - Response: `OK|VOLUME|SET|<LEVEL>`
- **MUTE** - Mute audio output
  - Response: `OK|MUTE|SUCCESS`
- **UNMUTE** - Unmute audio output
  - Response: `OK|UNMUTE|SUCCESS`

#### Status and System Commands
- **STATUS** - Get current system status
  - Response: `OK|STATUS|<BT_STATUS>,<AUDIO_STATUS>,<VOLUME>`
- **VERSION** - Get firmware version
  - Response: `OK|VERSION|<VERSION_STRING>`
- **RESET** - Reset the ESP32
  - Response: `OK|RESET|REBOOTING` (followed by boot messages)
- **DEBUG <ON|OFF>** - Enable/disable debug messages
  - Response: `OK|DEBUG|<STATE>`

#### Audio Configuration
- **SAMPLE_RATE <RATE>** - Configure I2S sample rate
  - Response: `OK|SAMPLE_RATE|SET|<RATE>`
- **I2S_CONFIG <BCLK>,<WCLK>,<DOUT>,<DIN>** - Configure I2S pins
  - Response: `OK|I2S_CONFIG|SUCCESS`

### Example Communication Flow
```
> SCAN
< INFO|SCAN|DEVICE_FOUND|11:22:33:44:55:66,Kitchen Speaker
< INFO|SCAN|DEVICE_FOUND|AA:BB:CC:DD:EE:FF,Living Room Speaker
< OK|SCAN|COMPLETE|2

> CONNECT AA:BB:CC:DD:EE:FF
< OK|CONNECT|CONNECTED|AA:BB:CC:DD:EE:FF

> START
< OK|START|SUCCESS

> STOP
< OK|STOP|SUCCESS

> STATUS
< OK|STATUS|CONNECTED,STOPPED,75
```

## Bluetooth Pairing Process

### Pairing Methods
The ESP32 Bluetooth A2DP source supports different pairing methods depending on the requirements of the sink device:

1. **"Just Works" Pairing** - No PIN required
   - Most common for speakers and headphones
   - Automatically handled without user intervention
   
2. **PIN Code Authentication** - Numeric PIN required
   - Used by some older Bluetooth devices
   - ESP32 can use a fixed PIN (default "0000" or "1234") or custom PIN

3. **Secure Simple Pairing (SSP)** - Modern security method
   - Confirmation of numeric code displayed on both devices
   - Passkey entry on one device
   
### Additional Pairing Commands

- **PAIR <MAC>** - Initiate pairing with device (if not already paired)
  - Response: `OK|PAIR|STARTED|<MAC>` or `ERROR|PAIR|FAILED|<REASON>`
  
- **CONFIRM_PIN <YES/NO>** - Confirm a PIN match during SSP
  - Response: `OK|CONFIRM_PIN|<RESULT>`
  
- **ENTER_PIN <PIN>** - Enter a PIN when requested
  - Response: `OK|ENTER_PIN|ACCEPTED` or `ERROR|ENTER_PIN|REJECTED`
  
- **SET_DEFAULT_PIN <PIN>** - Set the default PIN to use (stored in NVS)
  - Response: `OK|SET_DEFAULT_PIN|SUCCESS|<PIN>`
  
- **UNPAIR <MAC>** - Remove a paired device
  - Response: `OK|UNPAIR|SUCCESS|<MAC>` or `ERROR|UNPAIR|FAILED|<REASON>`
  
- **UNPAIR_ALL** - Remove all paired devices
  - Response: `OK|UNPAIR_ALL|SUCCESS|<COUNT>`

### Pairing Events
During pairing operations, the ESP32 may send unsolicited messages:

- `EVENT|PAIR|PIN_REQUEST|<MAC>` - A PIN code is required
- `EVENT|PAIR|CONFIRM|<PIN>` - Confirm the PIN matches on both devices
- `EVENT|PAIR|SUCCESS|<MAC>` - Pairing was successful 
- `EVENT|PAIR|FAILED|<REASON>` - Pairing failed

### Example Pairing Flow with PIN

```
> SCAN
< INFO|SCAN|DEVICE_FOUND|AA:BB:CC:DD:EE:FF,Car Stereo
< OK|SCAN|COMPLETE|1

> PAIR AA:BB:CC:DD:EE:FF
< OK|PAIR|STARTED|AA:BB:CC:DD:EE:FF
< EVENT|PAIR|PIN_REQUEST|AA:BB:CC:DD:EE:FF

> ENTER_PIN 0000
< OK|ENTER_PIN|ACCEPTED
< EVENT|PAIR|SUCCESS|AA:BB:CC:DD:EE:FF

> CONNECT AA:BB:CC:DD:EE:FF
< OK|CONNECT|CONNECTED|AA:BB:CC:DD:EE:FF
```

**Note**: Most A2DP audio sink devices (speakers, headphones) use "Just Works" pairing and don't require PIN codes. PIN-based pairing commands are typically only needed for automotive systems and some older devices.

## WiFi Controller ESP32

### Function
The WiFi Controller ESP32 serves as:
- I2S audio source (generating audio data)
- Web interface provider via WiFi
- Command interface to the Bluetooth ESP32
- Internet connectivity for streaming services

### GPIO Assignments (ESP32 WROOM32)

**I2S Output Interface:**
- BCLK (Bit Clock): GPIO26 (must match Bluetooth ESP32)
- WCLK/LRCLK (Word/LR Clock): GPIO25 (must match Bluetooth ESP32)
- DATA OUT: GPIO21 (connects to DATA IN on Bluetooth ESP32)

**Serial Communication to Bluetooth ESP32:**
- TX: GPIO17 (connects to RX GPIO16 on Bluetooth ESP32)
- RX: GPIO16 (connects to TX GPIO17 on Bluetooth ESP32)

**SD Card Interface (Optional - for local audio files):**
- MOSI: GPIO23
- MISO: GPIO19
- CLK: GPIO18
- CS: GPIO5

**User Interface Elements:**
- Status LED: GPIO2 (built-in LED on most dev boards)
- User button: GPIO0 (built-in button on most dev boards)

**Notes:**
- The I2S pins are selected to directly connect to the Bluetooth ESP32
- This configuration leaves SPI bus 2 available for the SD card
- GPIO pins 1 and 3 remain available for the debug serial port
- WiFi functionality uses the ESP32's integrated WiFi and doesn't require additional GPIO pins

### Connection Diagram
```
WiFi ESP32                  Bluetooth ESP32
-----------                 --------------
GPIO26 (BCLK) ------------- GPIO26 (BCLK)
GPIO25 (WCLK) ------------- GPIO25 (WCLK)
GPIO21 (DATA) ------------- GPIO22 (DATA IN)
GPIO17 (TX)   ------------- GPIO16 (RX)
GPIO16 (RX)   ------------- GPIO17 (TX)
GND           ------------- GND
```

### Power Requirements

**RECOMMENDED: Power Both ESP32s Separately**

Each ESP32 WROOM32 should be powered independently via USB:

```
USB Hub (powered) or separate USB power supplies
    ├──> WiFi ESP32 (esp_i2s_source) via USB
    └──> Bluetooth ESP32 (esp_bt_audio_source) via USB
    
Common GND established via I2S/UART wiring
```

**Why separate power is recommended:**
- Each ESP32 draws 80-260 mA during active Wi-Fi/BT operation
- Separate power ensures each device gets dedicated 500+ mA from its USB port
- Eliminates brownout risks when both devices use Wi-Fi/BT simultaneously
- Easier debugging - can power-cycle one device independently
- No risk of voltage drops affecting audio quality or causing resets
- Safer for development boards

**Alternative (NOT RECOMMENDED): Powering one ESP32 from another**

If you must power one ESP32 from the other:
- Use the **5V rail** (NOT 3.3V) to feed the second ESP32's voltage regulator
- Requires ESP32 #1 powered by a **2A+ USB supply** (not a computer USB port)
- Add a 470µF-1000µF electrolytic capacitor across VIN/GND near ESP32 #2
- Monitor for brownouts/resets during Wi-Fi/BT activity
- Risk: May exceed USB current limits during peak activity

**Wiring if using shared power (use at your own risk):**
```
ESP32 #1 (source)           ESP32 #2 (load)
VIN or 5V ─────────────────> VIN (NOT 3V3)
GND       ─────────────────> GND
          [470µF+ cap across VIN/GND near ESP32 #2]
```

**Current Limitations:**
- ESP32 active current: ~80-260 mA (varies with Wi-Fi/BT activity)
- USB 2.0 limit: 500 mA per port
- Two ESP32s can exceed 500 mA during peak operation
- ESP32 3.3V regulator output: ~600 mA max (shared with onboard components)

## Implementation Guide

### ESP32 WiFi Controller Project Setup

**Recommended Base Templates:**

1. **HTTP Server Options:**
   - **`protocols/http_server/simple`**: Basic HTTP server with minimal features
   - **`protocols/http_server/restful_server`**: More advanced with RESTful API structure
      * Good for structured API endpoints
      * Includes JSON parsing
      * Better organization for complex web interfaces

2. **WebSocket Option:**
   - **`protocols/http_server/ws_echo_server`**: WebSocket server implementation
      * Ideal for real-time communication
      * Allows push notifications from ESP32 to browser
      * Better for displaying live status updates from Bluetooth ESP32
      * More responsive user experience for status monitoring

3. **Persistent Sockets Option:**
   - **`protocols/http_server/persistent_sockets`**: Maintains long-lived TCP connections
      * Lower-level socket implementation
      * Good for custom protocols or binary data streaming

**Final Recommendation:** The **`protocols/http_server/ws_echo_server`** would be the best choice for your project because:
- WebSockets provide the ideal communication channel for real-time Bluetooth status updates
- It allows both server-to-client push notifications and client-to-server commands
- It includes basic HTTP functionality for serving the web interface
- The bidirectional communication model matches your needs for sending commands and receiving status updates

**Implementation steps:**
1. Start with the ws_echo_server template
2. Add I2S output functionality for audio generation
3. Implement UART communication to the Bluetooth ESP32
4. Add SD card support if needed
5. Develop the web interface with WebSocket support for real-time updates

*More implementation details to be added...*

## System Diagram

```
┌───────────────────┐       ┌────────────────────┐      ┌──────────────┐
│                   │ I2S   │                    │  BT  │              │
│  WiFi Controller  ├───────►  Bluetooth Source  ├──────►  BT Speaker  │
│  ESP32            │       │  ESP32             │      │              │
│                   │       │                    │      │              │
└───┬───────────────┘       └────────────────────┘      └──────────────┘
    │                                 ▲
    │         Serial UART            │
    └─────────────────────────────────┘

     ▲
     │ WebSocket/HTTP
     │
┌────▼─────────┐
│              │
│  Web Client  │
│              │
└──────────────┘
```

## Troubleshooting Guide

### Common Issues

#### No Sound Output
- Check I2S connections between ESP32s
- Verify Bluetooth connection status
- Ensure volume is not set to zero
- Try different speakers
- Check the sample rate configuration

#### ESP32s Not Communicating
- Verify UART connections
- Check TX/RX are correctly crossed (TX to RX, RX to TX)
- Try a lower baud rate
- Check ground connection between ESP32s

#### Bluetooth Not Connecting
- Ensure the target device is in pairing mode
- Try resetting the Bluetooth stack with the RESET command
- Check MAC address is entered correctly
- Try removing paired devices with UNPAIR_ALL

#### WiFi Connection Issues
- Check SSID and password
- Ensure router is broadcasting 2.4GHz network (ESP32 doesn't support 5GHz)
- Try moving closer to the router
- Restart the ESP32

#### WebSocket Connection Failing
- Check ESP32's IP address in serial output
- Verify browser supports WebSockets
- Try a different browser or WebSocket client
- Check your network allows WebSocket connections

#### Audio Quality Issues
- Verify 44.1kHz sample rate
- Check for I2S clock jitter
- Try shorter I2S cables
- Ensure both ESP32s are using the same I2S clock

## Developer tools

A small helper script is available to post-process pairing serial logs and resolve ELF addresses to symbols:

- Script: `tools/symbolize_pairing/symbolize_pairing.py` — extracts addresses from `build/pairing_e2_logs/serial.log` and uses `addr2line` with the built ELF to produce either a symbolized timeline or an aggregated CSV (address,count,symbol).

Example (sorted top 20):
```bash
python3 tools/symbolize_pairing/symbolize_pairing.py \
  --log esp_bt_audio_source/build/pairing_e2_logs/serial.log \
  --elf esp_bt_audio_source/build/esp_bt_audio_source.elf \
  --csv /tmp/pairing_addrs_sorted.csv --sort --top 20
```

Set `ADDR2LINE` env var if your toolchain's `addr2line` is not on PATH.

Tip: use `--no-resolve` to skip addr2line lookups when you only need address counts quickly; the symbol column will contain `<no-resolve>`.

## Running host Unity tests (Linux)

Host-side Unity tests live under `esp_bt_audio_source/test/host_test`. They build a native Linux runner with mocks so you can exercise the business logic without flashing hardware. Use the flow below whenever you need a clean run:

1. Install prerequisites (if you have not already):
  ```bash
  sudo apt-get update
  sudo apt-get install -y build-essential cmake pkg-config
  ```
2. Configure a fresh build directory:
  ```bash
  cd esp_bt_audio_source/test/host_test
  rm -rf build_host_tests
  mkdir build_host_tests && cd build_host_tests
  cmake ..
  ```
3. Build every host-test binary so they register with CTest:
  ```bash
  cmake --build . -j"$(nproc)"
  ```
4. Run the complete Unity suite and show detailed output on failure (tests are deduplicated by CTest):
  ```bash
  ctest --output-on-failure | tee test_results.log
  ```
5. To confirm coverage, list the discovered tests and compare against the Unity registrations in `test_*.c`:
  ```bash
  ctest -N
  grep -R "RUN_TEST" .. -n
  ```
6. Capture the pass/fail tally from the saved log (the final summary line from CTest is sufficient for reports):
   ```bash
   grep -E "tests (passed|failed)" test_results.log
   ```

Additional tips, including Valgrind usage and mock locations, are documented in `esp_bt_audio_source/test/host_test/README.md`.
