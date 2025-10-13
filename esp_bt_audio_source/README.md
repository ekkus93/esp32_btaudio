# ESP32 Bluetooth Audio Source

This project implements the Bluetooth A2DP audio source component of the ESP32 Audio Project. It receives I2S audio data from another ESP32 and streams it to Bluetooth speakers or headphones.

## Contents (quick links)

- [Features](#features)
- [Project status — October 2025](#project-status--october-2025)
- [Hardware Configuration](#hardware-configuration)
- [Implementation Tasks](#implementation-tasks)
- [Developer tools / Diagnostics](#developer-tools-diagnostics)
- [How to run host unit tests](#how-to-run-host-unit-tests)
- [Build and Installation Guide](#build-and-installation-guide)


<a id="features"></a>
## Features

- **A2DP Audio Source:** Implements the Bluetooth Advanced Audio Distribution Profile
- **I2S Audio Input:** Receives digital audio via I2S interface from the WiFi Controller ESP32
- **Serial Command Interface:** Accepts control commands over UART
- **Multiple Device Support:** Can scan for and connect to various Bluetooth audio sinks
- **Pairing Management:** Supports different pairing methods including "Just Works" and PIN-based pairing

<a id="project-status--october-2025"></a>
## Project status — October 2025

- Latest firmware commit `cb53afee` (2025-10-11) reorganizes host mocks under `test/host_test/mocks/include/` and checks in the device monitor capture `device_test_monitor.log`.
- Host unit tests in `test/host_test` pass locally after the mock cleanup, covering pairing command flows, NVS persistence, and serial command parsing.
- On-device Unity suite (`test_app`) was rebuilt and reflashed on 2025-10-11; the captured monitor log shows 11 pairing tests and 22 command tests looping without failures. The raw output lives at `device_test_monitor.log` alongside the test artifacts.
- Pairing diagnostics under `build/pairing_e2_logs/` remain under analysis; the event timeline and allocator correlation are still to be documented (see [Remaining work](#remaining-work-short-list)).

<a id="hardware-configuration"></a>
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

I2S and UART: practical defaults and recommendations
- I2S recommended default format: 16-bit PCM, stereo, 44.1 kHz (44100 Hz). This minimizes runtime processing on the Bluetooth ESP32 — send raw PCM frames from the decoder/producer ESP32 over I2S for the lowest CPU load on the Bluetooth device.
- I2S master/slave recommendation: make the audio producer (the ESP32 doing decoding/producing samples) the I2S master and this Bluetooth ESP32 the I2S slave. That way the producer supplies BCLK/WCLK and the BT device simply consumes the samples.
- UART defaults: 115200 baud, 8 data bits, no parity, 1 stop bit ("115200 8N1"). Commands are newline-terminated (\n).
- USB-serial adapter / TTL note: Use a 3.3V TTL USB-serial adapter (for example FTDI, CP2102, CH340 variants). Do not connect 5V-level UART adapters directly to the ESP32 pins. Cross RX/TX and always connect a common ground.

<a id="implementation-tasks"></a>
## Implementation Tasks

- [x] Initial A2DP source implementation
- [x] Add I2S driver configuration for receiving audio
- [x] Implement serial command protocol
- [~] Add pairing management functionality (in-progress)
   - Host-side: command handlers and event streaming for pairing (PIN request / SSP confirm) are implemented and covered by host unit tests (CONFIRM_PIN / ENTER_PIN and `nvs_storage` tests).
   - On-device: end-to-end pairing verification (pair → reboot → persisted list verification) remains manual/pending. Recent on-device instrumentation and defensive fixes were applied to aid debugging (see "Recent changes" below).
   - Host mocks and headers were deduplicated and centralized in `test/host_test/mocks/include/` so host and device builds share consistent interfaces.
- [x] Add volume/mute control
- [~] Implement device scanning and connection management (partial)
  - [x] Add persistent settings storage in NVS

Notes on progress:
- I2S driver configuration (modern standard-mode API) and an audio processing task are implemented. The code exposes runtime setters to change I2S pins and sample rate.
- The serial command protocol has been implemented in `components/command_interface` and supports commands such as `SCAN`, `CONNECT`, `DISCONNECT`, `START`, `STOP`, `VOLUME`, `I2S_CONFIG`, `PAIR` and `UNPAIR`. Handlers are implemented with ESP guards so host-based unit tests remain functional.
- Volume and mute controls are implemented in the audio processor (`audio_processor_set_volume`, `audio_processor_set_mute`) and are wired into the command handlers.
- Device scanning and connection management APIs are available in `components/bt_manager` (scan/connect/disconnect functions). The scanning result/event streaming and full pairing flows (PIN/SSP confirmation and persistent pairing state) remain to be completed and tested on device.

Notes on recent progress:
- A small NVS persistence component (`components/nvs_storage`) was added and registered with CMake. Configuration keys for volume, I2S pins, local device name and a default PIN are persisted and retrieved via that component.
- The audio processor and command handlers now persist changes (volume and I2S pin updates) to NVS. The command `SET_NAME` and `SET_DEFAULT_PIN` persist values as well.
- Bluetooth initialization was updated to read the persisted local device name from NVS at boot and apply it (GAP API with guarded deprecated fallback), so persisted device name now takes effect on startup.

Current test status (2025-10-11)
--------------------------------
- Host-based tests under `test/host_test` remain green after the mock reorganization, validating command handlers, NVS persistence helpers, and Bluetooth GAP reply paths.
- On-device Unity tests in `test_app` were rebuilt and reflashed on 2025-10-11. The captured monitor output (`device_test_monitor.log`) shows the full loop of 11 pairing tests and 22 command tests passing repeatedly without failures.

Remaining work (short list)
--------------------------
- On-device end-to-end verification: run a few real-device scenarios to validate pairing persistence across reboot and interoperability with common phones/speakers (**~2–3 days**).
- Expand timing and fault-injection tests: add simulated connection timeouts, drops, and auto-reconnect scenarios in the mock harness (**~1–2 days**).
- CI automation: add jobs to run host tests on every PR and consider a hardware-backed runner for selected on-device Unity tests (**~0.5–1 day** initially, hardware runner optional follow-up).
- Documentation: add a short how-to for capturing and interpreting on-device serial logs for pairing analysis (host test quick-start already lives in `test/host_test/README.md`) (**~0.5 day**).
- Static analysis & coverage: enable linting (clang-format/clang-tidy) and produce test coverage for host tests to identify gaps (**~1 day** for initial setup).

Prioritized next steps (actionable)
----------------------------------
1. On-device E2E pairing verification (High, **~2–3 days**)
   - Task: Run pairing scenarios with representative sinks (phone, speaker, car stereo).
   - Acceptance: pairing → reboot → verify paired list persists and device connects as expected.

2. Finalize pairing confirmation & event streaming (High, **~1–1.5 days**)
   - Task: Ensure `EVENT|PAIR|...` messages (PIN_REQUEST, CONFIRM, SUCCESS/FAILED) are reliably emitted and that `CONFIRM_PIN` / `ENTER_PIN` commands invoke the GAP reply APIs on-device.
   - Acceptance: Host-driven pairing flows complete successfully on hardware.

3. Add/extend host unit tests (Medium, **~1 day**)
   - Task: Add tests covering CONFIRM_PIN/ENTER_PIN flows, invalid-MAC cases, and NVS persistence edge cases.
   - Acceptance: New tests added to `test/host_test` and pass locally.

4. Expand timing/fault-injection tests in mocks (Medium, **~1–2 days**)
   - Task: Simulate timeouts, connection drops, and auto-reconnect in `test/host_test/mocks/` and assert recovery logic.
   - Acceptance: Tests reproduce and validate reconnect logic and timeouts.

5. Diagnostics & timeline analysis (Medium, **~1 day**)
   - Task: Parse `build/pairing_e2_logs/serial.log`, map runtime addresses to file:line using the built ELF, and correlate allocator frees with BTM events.
   - Acceptance: A short report describing root cause candidates and recommended fix (guard or ownership change).

6. CI & static checks (Lower, **~0.5–1 day**)
   - Task: Add a CI job to run host tests and a lint step (clang-format/clang-tidy). Optionally add coverage reporting for host tests.
   - Acceptance: CI runs host tests on PRs and flags formatting/lint issues.

7. Documentation (Quick wins, **~0.5 day**)
   - Task: Add `test/host_test/README.md` (done) and a short how-to for capturing and resolving on-device serial logs for pairing analysis.
   - Acceptance: New docs live in the repo and make it easy for contributors to run tests and collect logs.

Recent work (pairing & events):
- Pairing event streaming: GAP pairing events (PIN requests, SSP numeric confirmation, auth complete) are forwarded to the serial command interface as `EVENT|PAIR|...` messages so a host can drive the pairing flow.
- Command replies for pairing: `CONFIRM_PIN` and `ENTER_PIN` command handlers now call the appropriate GAP reply APIs on-device (`esp_bt_gap_ssp_confirm_reply()` and `esp_bt_gap_pin_reply()`), falling back to a stored default PIN from NVS when available. These handlers are guarded by `#ifdef ESP_PLATFORM` for host-test compatibility.

Recent changes (host-test and pairing work)
- Host unit-test harness under `test/host_test` updated with additional mocks and tests to validate command handlers without device hardware.
- Header and mock files for host tests were reorganized under `test/host_test/mocks/include/` so production and test builds share consistent interfaces.
- Added minimal host-side mocks for Bluetooth GAP responses and NVS (`test/host_test/mocks/mock_gap.c`, `mocks/nvs_storage_mock.c`, `mocks/esp_bt.h`, `mocks/esp_err.h`) so `CONFIRM_PIN` and `ENTER_PIN` command handlers can be exercised by unit tests.
- `components/command_interface/commands.c` has a small host-path branch that parses MAC and calls the GAP reply mocks so host tests can assert the expected behavior.
 - New `nvs_storage` host tests were added and expanded (capacity and invalid-MAC cases); all host `nvs_storage` tests pass locally (6 tests, 0 failures).

Recent on-device pairing diagnostics and status

- On-device E2E pairing runs have been executed and serial monitor output was persisted to `build/pairing_e2_logs/serial.log` for post-run analysis. This log contains allocator diagnostic dumps (the allocator prints `osi_mem_dbg_clean not-found` with a `recent-free-history` buffer), temporary BTM lifecycle traces and additional WARN/ERRORs inserted for timeline correlation.
- Unity test monitor output from 2025-10-11 is checked into `device_test_monitor.log` to document the passing baseline while pairing diagnostics continue.
- Minimal defensive fixes applied to aid stability and debug:
   - `BTM_SetPowerMode` now uses a zero-initialized local copy of the caller-provided `tBTM_PM_PWR_MD` structure before using it internally and before sending it to the power manager helper. This reduces reliance on caller memory lifetime.
   - `bta_dm_pm_sniff` contains an ACL-existence guard that skips requesting a power-mode change when no ACL is present; it logs the skip and returns a non-success status. This avoids invoking power-mode transitions during disconnect transient windows.
- Current status: serial capture step is complete; parsing and correlating allocator traces against BTM lifecycle events is in-progress to decide whether a deeper ownership/double-free fix is required or whether conservative guards + tests suffice.

Next steps

- Complete timestamped parsing of `build/pairing_e2_logs/serial.log`, map runtime addresses to source file:line using the built ELF, and build an event timeline that correlates allocator frees with BTM events and ACL lifecycle.
- Based on the correlation, either (A) keep the conservative guards and add host/unit tests that reproduce the timing/race, or (B) implement a minimal ownership fix in the implicated module(s), add tests, and revalidate on-device.
- Produce a concise PR-ready report that includes the findings, diffs, test results, and instructions to reproduce the on-device capture.

Next high-priority tasks:
- Implement pairing confirmation flows and streaming of scan/pairing events to the command interface (PIN requests, SSP confirmations, pairing results).
- Add/extend host unit tests to cover the command handlers and NVS-backed persistence logic.
- Finalize the pairing interaction loop and on-device verification (ensure host commands such as `CONFIRM_PIN` and `ENTER_PIN` trigger the expected GAP replies and that the full pairing flow succeeds on-device).
- Add/extend host unit tests to cover the command handlers and NVS-backed persistence logic.

<a id="developer-tools-diagnostics"></a>
Developer tools / Diagnostics
-----------------------------
- Host-test quick-start: see `test/host_test/README.md` for fast host-side test instructions and a map of the mocks used by the harness.
- Pairing log symbolizer: see `tools/symbolize_pairing/README.md`. Example usage:

```bash
python3 tools/symbolize_pairing/symbolize_pairing.py \
   --log esp_bt_audio_source/build/pairing_e2_logs/serial.log \
   --elf esp_bt_audio_source/build/esp_bt_audio_source.elf \
   --out esp_bt_audio_source/build/pairing_e2_logs/serial.symbolized.log
```

- If your toolchain's `addr2line` is not on PATH, set `ADDR2LINE` to the full path of the toolchain binary (for example `xtensa-esp32-elf-addr2line`) before running the symbolizer.

<a id="how-to-run-host-unit-tests"></a>
How to run host unit tests (fast, on your development machine)

1. Create and enter a build directory for host tests:

```bash
cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test/host_test
mkdir -p build_host_tests && cd build_host_tests
cmake ..
```

2. Build the test runner (example: `test_commands`):

```bash
cmake --build . --target test_commands -j$(nproc)
```

3. Run the test binary and capture output:

```bash
./test_commands |& tee test_commands.log
echo "exit code: $?"
```

Notes:
- The host-test harness compiles production code with `ESP_PLATFORM` undefined and links in test mock implementations from `test/host_test/mocks/` so tests run on your Linux machine.
- If a test fails, edit or extend the mocks under `test/host_test/mocks/` (for example `mock_gap.c` or `nvs_storage_mock.c`) to simulate the expected runtime behavior.

If you'd like, I can also add a short `test/host_test/README.md` with these commands and a quick map of where the mocks live.
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

Notes about commands and responses

- Commands are case-sensitive. Use uppercase command tokens (for example: `SCAN`, `CONNECT`, `START`).
- Line endings: commands must end with a newline character (`\n`). The parser expects the exact format and may reject extra whitespace.
- Error response format (recommended): `ERR|<COMMAND>|<CODE>|<MESSAGE>`
   - Example: `ERR|CONNECT|NOT_FOUND|Device not in range`
   - Suggested error codes: `BAD_SYNTAX`, `BAD_PARAM`, `NOT_FOUND`, `BUSY`, `FAILED`.
- IO convention used in this README: `>` denotes user/host input, `<` denotes device output (responses or events).

Persistent storage and small-file storage

- NVS is used for configuration and small structured data (paired device entries, config keys). The partition table includes an `nvs` partition by default.
- If you need to store small audio clips (e.g., .wav), add SPIFFS or LittleFS to the partition table and store files there. Keep file sizes small or use external flash for large media.

Testing suggestions

- Host-based unit tests: put parser and business logic behind an interface and mock ESP-IDF APIs. Use the `test/host_test` CMake host-test harness to run fast tests on your development machine.
- On-device: keep Unity-based tests in `test_app` for integration verification. Run fast host tests during development and run on-device tests before major merges.

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
   # Activate the Python environment used for tests and tools
   conda activate python310

   # Source the ESP-IDF environment so idf.py and toolchain are available
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

Note: On most Linux systems the common USB serial adapter shows up as `/dev/ttyUSB0` by default. If you don't have a different preference, you can use `/dev/ttyUSB0` in the command above (for example: `idf.py -p /dev/ttyUSB0 flash monitor`). If your adapter enumerates as `/dev/ttyACM0` or a different device node, replace the port accordingly.

## Unit Testing Framework

This project uses a dual testing approach for faster Test-Driven Development (TDD):

<a id="build-and-installation-guide"></a>
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

On-device Unity tests (detailed)
--------------------------------
These tests run on the target ESP32 and use ESP-IDF's Unity integration. The `test_app/` application is already configured to run the Unity tests on boot and print results to the serial console.

Quick commands

```bash
# from the project root
cd esp_bt_audio_source/test_app
# build the test firmware
idf.py build
# flash and open the serial monitor (replace PORT with your device, e.g. /dev/ttyUSB0)
idf.py -p PORT flash monitor
```

What to expect

- After flashing, the device boots the test application and the Unity runner executes the registered TEST_CASEs.
- Test output appears on the serial monitor. Look for lines like:

   "Running 3 tests..."
   "[ RUN ] Test Bluetooth connection"
   "[ PASS ] Test Bluetooth connection"
   "--- SUMMARY ---"
   "3 Tests 0 Failures 0 Ignored"

- On failure, Unity prints a failing assertion and the test name. Use the monitor output to trace back to the failing test and inspect logs produced by the firmware.

Running only the build (no flash)

If you want to build without flashing (for a faster compile-check):

```bash
cd esp_bt_audio_source/test_app
idf.py build
```

Capturing logs

- The serial monitor shows test output live. To capture it to a file, run:

```bash
idf.py -p PORT flash monitor |& tee esp32_unity_test.log
```

CI and automated test runners

- For CI runners that support serial access, flash the firmware and read the serial output capturing the Unity summary. Some CI setups use a hardware test harness (lab runner) that can power-cycle the device and collect logs automatically.

Troubleshooting

- No tests appear on the console: ensure you flashed the `test_app` binary (the main firmware won't run the Unity tests unless you run the test application).
- Build failures referencing missing ESP-IDF symbols: ensure your ESP-IDF environment is sourced (run `. $HOME/esp/esp-idf/export.sh`) and you're using a compatible IDF version.
- Serial monitor shows a crash early during boot: capture the logs, increase the monitor baud rate to match `CONFIG_ESP_CONSOLE_UART_BAUDRATE` in your `sdkconfig` (default 115200), and inspect stack traces. You can also use `idf.py monitor` with `--monitor` options to decode backtraces.


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

## CI improvements (future work)

The repository currently runs host-based unit tests and captures CTest output and JUnit XML as part of the `CI — host tests (optimized)` workflow. The following are suggested, prioritized enhancements you may want to add later to improve CI coverage, diagnostics, and developer experience.

High priority
- Add a dedicated PR job that runs the host tests and the Python symbolizer unit tests on every pull request. This gives faster feedback to contributors.
- Persist build artifacts (JUnit XML, ctest.log, pairing logs, and debug symbols) for failed runs so failures can be downloaded and inspected from the run UI.

Medium priority
- Annotator enhancements: extend the embedded JUnit annotator to resolve native addresses using `addr2line` and the built ELF when failures include addresses, producing file:line annotations.
- Add a job that runs static formatting and lint checks (clang-format, clang-tidy) to catch style/regression issues early.

Low priority
- Add coverage collection for host tests (gcov/lcov or pytest coverage for Python parts) and upload coverage artifacts; optionally fail CI on coverage regression.
- Add a scheduled workflow that performs longer integration tests (flaky/resilience scenarios) and preserves logs for triage.
- Investigate hardware-backed CI runners to run on-device Unity tests automatically (requires access to hardware and runner setup).

Notes
- Start with the High priority items first — they provide the largest immediate developer ROI with minimal infra cost.
- When adding addr2line resolution in workflows, ensure build artifacts (ELF, debug symbols) are preserved and accessible to the runner step performing resolution.
- For hardware-backed runners, create a small, well-documented runner playbook describing power cycling, serial capture, and artifact upload procedures.
