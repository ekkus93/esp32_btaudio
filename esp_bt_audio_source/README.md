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
## Project status — October 30, 2025

- Latest firmware commit `85ea4d74` (2025-10-29) disables BLE features to reclaim flash headroom, documents the harmless `ESP_EVENT_ANY_ID` warning, and updates the internal runbook (`memory.md`).
- Full regression sweep completed on 2025-10-30 after fixing the failing host test:
   - Host `ctest` bundle: 17/17 tests passing (`test/host_test/build/Testing/Temporary/LastTest.log`).
   - `test_app`: 37 tests, 0 failures, 0 ignored (`test_app/build/one_run_unity.log`).
   - `test_app2`: 45 tests, 0 failures, 0 ignored (`test_app2/build/one_run_unity.log`).
   - `test_app_audio`: 26 tests, 0 failures, 0 ignored (`test_app_audio/build/one_run_unity.log`).

- Key recent completions:
   - Host-based unit tests: all host tests pass and the host-test harness updated to support sequence-number diagnostics.
   - Pairing event stream hardening: sequence numbers added to `EVENT|PAIR|...` and tests updated to validate ordering/monotonicity.
   - Device-side pairing test added: `test_app/main/test_pairing_seq_hardening_device.c` (device-only Unity test that checks CONFIRM+SUCCESS sequencing on-device).
   - CI: Added a compile-only device build workflow (`.github/workflows/ci-device-build.yml`) that runs `idf.py build` on `ubuntu-latest` to detect device compile regressions. This ensures the main app continues to compile on PRs/pushes even without hardware.

- Recent bt_manager fixes (2025-10-29) implemented proper state validation and ESP-IDF API calls for START/STOP audio streaming commands, ensuring correct A2DP media control flow and error handling.
- Pairing diagnostics under `build/pairing_e2_logs/` remain under analysis; allocator timeline correlation still needs capture and documentation (see [Remaining work](#remaining-work-short-list)).
- Known warning: ESP-IDF builds currently print duplicate-definition notices for `ESP_EVENT_ANY_ID` because our legacy Bluetooth shim header (`components/components/bt/include/esp32/include/esp_event_base.h`) still defines the macro; plan is to guard/remove that old definition in a future cleanup.
- **Build warnings present (2025-10-30)**: 
   - **Crystal frequency**: Detected 41.01MHz crystal frequency differs from normalized 40MHz (unsupported crystal in use)

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

- [x] Initial A2DP source implementation — Done
- [x] I2S driver configuration for receiving audio — Done
- [x] Serial command protocol — Done
- [~] Pairing management — In progress (host tests passing; on-device E2E pending)
   - Host-side: command handlers and event streaming for pairing (PIN request / SSP confirm) are implemented and covered by host unit tests (CONFIRM_PIN / ENTER_PIN and `nvs_storage` tests).
   - On-device: `PAIR` / `CONFIRM_PIN` / `ENTER_PIN` now route through `bt_pairing_confirm()` / `bt_pairing_submit_pin()`; persistent pairing across reboot requires manual verification.
   - Acceptance criteria: pairing → reboot → paired list persists; host-driven confirmation flows succeed on-device.
- [x] Volume/mute control — Done
- [~] Device scanning and connection management — Partial (APIs present; event-streaming & robustness need on-device validation)
   - [x] Persistent settings storage in NVS — Done

Notes on progress:
- I2S driver configuration (modern standard-mode API) and an audio processing task are implemented. The code exposes runtime setters to change I2S pins and sample rate.
- The serial command protocol has been implemented in `components/command_interface` and supports commands such as `SCAN`, `CONNECT`, `DISCONNECT`, `START`, `STOP`, `VOLUME`, `I2S_CONFIG`, `PAIR` and `UNPAIR`. Handlers are implemented with ESP guards so host-based unit tests remain functional.
- Volume and mute controls are implemented in the audio processor (`audio_processor_set_volume`, `audio_processor_set_mute`) and are wired into the command handlers.
- Device scanning and connection management APIs are available in `components/bt_manager` (scan/connect/disconnect functions). The scanning result/event streaming and full pairing flows (PIN/SSP confirmation and persistent pairing state) remain to be completed and tested on device.

Notes on recent progress:
- A small NVS persistence component (`components/nvs_storage`) was added and registered with CMake. Configuration keys for volume, I2S pins, local device name and a default PIN are persisted and retrieved via that component.
- The audio processor and command handlers now persist changes (volume and I2S pin updates) to NVS. The command `SET_NAME` and `SET_DEFAULT_PIN` persist values as well.
- Bluetooth initialization was updated to read the persisted local device name from NVS at boot and apply it (GAP API with guarded deprecated fallback), so persisted device name now takes effect on startup.

Current test status (2025-10-30)
--------------------------------
- Host-based tests under `test/host_test` rebuilt on 2025-10-30 and executed via `ctest`; 17 tests, 0 failures, 0 ignored (`test/host_test/build/Testing/Temporary/LastTest.log`).
- On-device Unity suites were rerun on hardware on 2025-10-30 using `tools/flash_and_watch.py`:
   - `test_app`: 37 tests, 0 failures, 0 ignored (`test_app/build/one_run_unity.log`).
   - `test_app2`: 45 tests, 0 failures, 0 ignored (`test_app2/build/one_run_unity.log`).
   - `test_app_audio`: 26 tests, 0 failures, 0 ignored (`test_app_audio/build/one_run_unity.log`).

Remaining work (short list)
---------------------------
- Test coverage gaps: Address false-positive tests in audio_processor where unit tests pass on stubs but don't verify observable behavior, risking undetected regressions. Specific areas: beep functionality (tests check command response but not audio generation), volume control (tests may not verify actual volume application), read buffer filling (tests may not check if buffers are actually filled), and audio streaming (tests might not verify real streaming behavior).
- On-device end-to-end verification: run real-device scenarios to validate pairing persistence across reboot and interoperability with common phones/speakers (**~2–3 days**).
- Pairing event stream hardening: ensure `EVENT|PAIR|...` emissions remain ordered/noise-free under stress and that host-driven confirmation flows succeed on hardware (**~1–1.5 days**).
- Mock fault-injection coverage: extend host mocks to simulate connection drops/timeouts and assert recovery logic (**~1–2 days**).
- Timeline analysis: parse `build/pairing_e2_logs/serial.log`, map allocator traces to source lines, and summarize findings for a targeted fix (**~1 day**).
- CI automation: add a job that runs host tests on every PR and preserves Unity logs/ELFs as artifacts; evaluate hardware-backed runners as a follow-up (**~0.5–1 day** to bootstrap).
- Documentation polish: add a quick pairing-log triage guide and refresh command help output once pairing verification completes (**~0.5 day**).

Prioritized next steps (actionable)
----------------------------------
1. On-device E2E pairing verification (High, **~2–3 days**)
   - Task: Run pairing scenarios with representative sinks (phone, speaker, car stereo).
   - Acceptance: pairing → reboot → verify paired list persists and device connects as expected.

2. Pairing event stream hardening (High, **~1–1.5 days**)
   - Task: Stress `EVENT|PAIR|...` emissions, confirm ordering, and validate that host `CONFIRM_PIN` / `ENTER_PIN` commands succeed on hardware.
   - Acceptance: Host-driven pairing flows complete successfully with stable event sequencing.

3. Host fault-injection coverage (Medium, **~1–2 days**)
   - Task: Extend mocks to simulate connection drops/timeouts and assert recovery logic through unit tests.
   - Acceptance: New tests fail without the current safeguards and pass with them in place.

4. Timeline analysis (Medium, **~1 day**)
   - Task: Parse `build/pairing_e2_logs/serial.log`, symbolise addresses with the latest ELF, and document suspected ownership issues.
   - Acceptance: A concise report outlining findings and proposed fixes is published alongside logs.

5. CI & documentation uplift (Lower, **~0.5–1 day**)
   - Task: Add a PR-host-test job that archives Unity logs/ELFs and draft a short pairing-log triage guide.
   - Acceptance: CI surfaces host regressions automatically and the guide ships with the repo.

Recent work (pairing & events):
- Pairing event streaming: GAP pairing events (PIN requests, SSP numeric confirmation, auth complete) are forwarded to the serial command interface as `EVENT|PAIR|...` messages so a host can drive the pairing flow.
- Command replies for pairing: `CONFIRM_PIN` and `ENTER_PIN` command handlers now delegate to `bt_pairing_confirm()` / `bt_pairing_submit_pin()` via the Bluetooth manager layer, falling back to a stored default PIN from NVS when available. These handlers remain guarded by `#ifdef ESP_PLATFORM` for host-test compatibility.

Recent changes (host-test and pairing work)
- Host unit-test harness under `test/host_test` updated with additional mocks and tests to validate command handlers without device hardware.
- Header and mock files for host tests were reorganized under `test/host_test/mocks/include/` so production and test builds share consistent interfaces.
- Added minimal host-side mocks for Bluetooth GAP responses and NVS (`test/host_test/mocks/mock_gap.c`, `mocks/nvs_storage_mock.c`, `mocks/esp_bt.h`, `mocks/esp_err.h`) so `CONFIRM_PIN` and `ENTER_PIN` command handlers can be exercised by unit tests.
- `components/command_interface/commands.c` has a small host-path branch that parses MAC and calls the GAP reply mocks so host tests can assert the expected behavior.
 - New `nvs_storage` host tests were added and expanded (capacity and invalid-MAC cases); all host `nvs_storage` tests pass locally (6 tests, 0 failures).

Recent on-device pairing diagnostics and status

- On-device E2E pairing runs have been executed and serial monitor output was persisted to `build/pairing_e2_logs/serial.log` for post-run analysis. This log contains allocator diagnostic dumps (the allocator prints `osi_mem_dbg_clean not-found` with a `recent-free-history` buffer), temporary BTM lifecycle traces and additional WARN/ERRORs inserted for timeline correlation.
- Unity regression sweeps on 2025-10-29 refreshed the canonical per-suite logs: `test_app/build/one_run_unity.log` (37 tests), `test_app_audio/build/one_run_unity.log` (26 tests), and `test_app2/build/one_run_unity.log` (45 tests) all show clean passes alongside the earlier `device_test_monitor.log` capture from 2025-10-11.
- Minimal defensive fixes applied to aid stability and debug:
   - `BTM_SetPowerMode` now uses a zero-initialized local copy of the caller-provided `tBTM_PM_PWR_MD` structure before using it internally and before sending it to the power manager helper. This reduces reliance on caller memory lifetime.
   - `bta_dm_pm_sniff` contains an ACL-existence guard that skips requesting a power-mode change when no ACL is present; it logs the skip and returns a non-success status. This avoids invoking power-mode transitions during disconnect transient windows.
- Current status: serial capture step is complete; parsing and correlating allocator traces against BTM lifecycle events is in-progress to decide whether a deeper ownership/double-free fix is required or whether conservative guards + tests suffice.

Next steps

- Complete timestamped parsing of `build/pairing_e2_logs/serial.log`, map runtime addresses to source file:line using the built ELF, and build an event timeline that correlates allocator frees with BTM events and ACL lifecycle.
- Based on the correlation, either (A) keep the conservative guards and add host/unit tests that reproduce the timing/race, or (B) implement a minimal ownership fix in the implicated module(s), add tests, and revalidate on-device.
- Produce a concise PR-ready report that includes the findings, diffs, test results, and instructions to reproduce the on-device capture.

<a id="developer-tools-diagnostics"></a>
Developer tools / Diagnostics
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

### Commands still needing on-device implementation / hardening

The majority of commands are implemented and exercised by host unit tests. However a few command flows still need additional on-device work (end-to-end testing, event-stream reliability or small API wiring) before we consider them "complete" on hardware:

- PAIR — Status: partially implemented. Host-side pairing flows (mocked PIN/SSP handling) are covered by unit tests, but on-device end-to-end pairing (PIN/SSP request → host reply → successful persistent pair entry across reboot) needs manual verification and hardening.
- CONFIRM_PIN / ENTER_PIN — Status: implemented for host tests and now routed through `bt_pairing_confirm()` / `bt_pairing_submit_pin()` on-device; event-streaming (reliable `EVENT|PAIR|...` messages) and recovery from transient errors should be validated on hardware.
- CONNECT_NAME — Status: implemented end-to-end (the `bt_connect_by_name()` helper now lives in `bt_manager`). Remaining work is validating that scan and pairing caches are populated on hardware so name lookups succeed without relying on host mocks.
- SCAN / device-found streaming — Status: scanning APIs exist but the serial event stream for discovered devices and the `OK|SCAN|COMPLETE` termination need reliability testing on-device (throttling and noise mitigation may be needed under heavy Bluetooth traffic).
- PAIRED / UNPAIR / UNPAIR_ALL — Status: commands exist and host tests validate NVS helpers; on-device persistence verification (pairing list persists across reboot and unpair edge cases) still requires full E2E testing and any required retry/rollback logic.
- HELP — Status: command is parsed but currently returns a placeholder message. Flesh out the structured help output so users can enumerate supported commands over UART.

Next steps to finish these flows on-device:

1. Run on-device pairing scenarios (phone/speaker) and capture serial logs to verify the `EVENT|PAIR|...` sequence and persistence across reboots.
2. Validate scan → connect-by-name flow on hardware to ensure discovery caches are populated and `bt_connect_by_name()` finds devices without host assistance.
3. Harden event emission (rate-limit noisy events, ensure ordering) and add targeted host tests that mimic high-noise serial output so the command interface remains usable during heavy BT activity.


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

Note: use the repository helper `tools/run_unity.py` instead when possible — it watches the serial output for a deterministic marker (`TEST_RUN_COMPLETE: <total> <fail> <ignored>`) and will exit automatically when the tests finish, saving the canonical capture to `build/one_run_unity.log`. If you have an older copy of the helper and it doesn't stop automatically, update `tools/run_unity.py` from the repository.
```

Locked-down reproducible runner (recommended)
---------------------------------------------

Before you run the runner, ensure the ESP‑IDF environment is active in your shell so `idf.py` and the toolchain tools are on `PATH`.

Quick pre-check and activation (example):

```bash
# verify idf.py is visible
which idf.py || echo "idf.py not found"

# if not found, source your ESP-IDF export (adjust path as needed)
. $HOME/esp/esp-idf/export.sh

# verify again
which idf.py || (echo "ERROR: idf.py still not found; fix your ESP-IDF export" && exit 1)
```

We provide `tools/run_unity.py` to make flashing, monitoring, and extracting Unity test results reproducible and scriptable.

What it does:
- Runs `idf.py -p <PORT> flash monitor` in the project you point it at.
- Saves the full monitor output to `build/one_run_unity.log`.
- Watches the serial output for Unity summary markers and exits with a status code:
   - 0 = tests passed
   - 1 = tests failed
   - 2 = no Unity summary found (timeout)
   - 3 = error/interrupt

📌 **Important:** Unity lives in the `test_app/` project. If you run the helper from the production app root, it will happily flash `esp_bt_audio_source` and you will *not* see any Unity output. Always point the runner at `test_app/` (either by changing directories or using `--project-root`).

Unity quickstart (host + on-device)
-----------------------------------
1. **Host Unity/CTests (no hardware required)**
   ```bash
   cd esp_bt_audio_source/test/host_test
   cmake -S . -B build_host_tests
   cmake --build build_host_tests -- -j"$(nproc)"
   cd build_host_tests
   ctest --output-on-failure
   ```
   The binaries land in `build_host_tests/` (for example `test_audio_processor`). JUnit/CTest logs remain in the same directory for CI artifacts.

2. **Bluetooth Unity firmware (`test_app`)**
   ```bash
   cd esp_bt_audio_source/test_app
   idf.py build
   python3 ../tools/run_unity.py --port /dev/ttyUSB0 --timeout 600
   ```
   The runner flashes `build/esp_bt_audio_source_test.bin`, streams Unity output, and saves the canonical capture to `test_app/build/one_run_unity.log`.

3. **Integration Unity firmware (`test_app2`)**
   ```bash
   cd esp_bt_audio_source/test_app2
   idf.py build
   python3 ../tools/run_unity.py --project-root test_app2 --port /dev/ttyUSB0 --timeout 900
   ```
   Notes:
   - The runner flashes `build/esp_bt_audio_source_test2.bin`, streams Unity output, and writes the capture to `test_app2/build/one_run_unity.log`.
   - `test_app2` now prints the canonical `"<tests> Tests <failures> Failures <ignored> Ignored"` line, so the helper exits with `0` on success and `1` if any tests fail.
   - Bump `--timeout` as the suite grows (900 seconds comfortably covers the current run). Run `idf.py fullclean` if you add new sources and see stale behaviour.
   - To inspect the tail of the latest run: `tail -n 30 test_app2/build/one_run_unity.log`.

4. **Audio Unity firmware (`test_app_audio`)**
   ```bash
   cd esp_bt_audio_source/test_app_audio
   idf.py build
   python3 ../tools/run_unity.py --project-root test_app_audio --port /dev/ttyUSB0 --timeout 600
   ```
   This image (`build/esp_bt_audio_source_audio_test.bin`) exercises the audio/I²S suites. The log is written to `test_app_audio/build/one_run_unity.log`.

Canonical sequence:

```bash
# 1. Build the Unity firmware once (incremental rebuilds are cheap)
cd esp_bt_audio_source/test_app
idf.py build

# 2. Flash + monitor using the locked-down runner
python3 ../tools/run_unity.py --port /dev/ttyUSB0 --timeout 600
```

If you prefer to stay at the repository root, pass the test app directory explicitly:

```bash
cd esp_bt_audio_source
python3 tools/run_unity.py --project-root test_app --port /dev/ttyUSB0 --timeout 600
```

The script writes the canonical serial capture to `build/one_run_unity.log` inside the selected project. In CI, run the script and fail the job when the exit code is non-zero; upload `build/one_run_unity.log` as an artifact for triage.

### Gathering Unity summaries (canonical workflow)

When you want a single, reproducible summary of all Unity test runs recorded in this repository (host + on-device runs and recorded idf.py monitor captures), follow this canonical workflow. It restricts scanning to known artifact locations (runner captures and CTest LastTest logs) to avoid accidental matches in README files or other non-test outputs.

1) Host tests (ctest)

- Build and run host tests as documented above. The host CTest summary is written to the LastTest.log files under each host-test build dir, for example:

```bash
cd esp_bt_audio_source/test/host_test
cmake -S . -B build_host_tests
cmake --build build_host_tests -- -j$(nproc)
cd build_host_tests
ctest --output-on-failure |& tee ctest_full_output.log
echo "Host LastTest: $(pwd)/Testing/Temporary/LastTest.log"
```

2) On-device Unity tests

- Use the locked-down runner to build, flash, and capture the device Unity output. The runner writes the canonical serial capture into each test app's `build/one_run_unity.log`. Example:

```bash
# from repository root
python3 tools/run_unity.py --project-root esp_bt_audio_source/test_app --port /dev/ttyUSB0 --timeout 600
echo "Device test_app log: esp_bt_audio_source/test_app/build/one_run_unity.log"

python3 tools/run_unity.py --project-root esp_bt_audio_source/test_app_audio --port /dev/ttyUSB0 --timeout 600
echo "Device test_app_audio log: esp_bt_audio_source/test_app_audio/build/one_run_unity.log"
```

3) idf.py stdout captures (optional)

- CI runs or scripted idf.py invocations sometimes leave `idf_py_stdout_output_*` captures under `esp_bt_audio_source/test_app/build/log/`. Include these only if you use the runner or CI that produces them.

4) Canonical aggregation (recommended)

- Quick grep (one-liner) to spot Unity summary lines in canonical locations:

```bash
grep -E "[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored" \
  esp_bt_audio_source/test_app/build/one_run_unity.log \
  esp_bt_audio_source/test_app_audio/build/one_run_unity.log \
  esp_bt_audio_source/device_test_monitor.log \\
  esp_bt_audio_source/test_app/build/log/idf_py_stdout_output_* \
  test/host_test/**/Testing/Temporary/LastTest.log || true
```

- Recommended: run the small aggregator below. It scans only the canonical artifact locations and writes a JSON summary to `tmp/canonical_unity_summary.json` in the repository root. The JSON contains the aggregated totals and a per-file breakdown.

```bash
python3 - <<'PY'
import os,re,json
root=os.path.abspath('.')
pattern=re.compile(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored")
paths=[]
# canonical device logs
paths.append(os.path.join(root,'esp_bt_audio_source','test_app','build','one_run_unity.log'))
paths.append(os.path.join(root,'esp_bt_audio_source','test_app_audio','build','one_run_unity.log'))
paths.append(os.path.join(root,'esp_bt_audio_source','device_test_monitor.log'))
# idf.py captured logs (optional)
logdir=os.path.join(root,'esp_bt_audio_source','test_app','build','log')
if os.path.isdir(logdir):
   for fn in os.listdir(logdir):
      if fn.startswith('idf_py_stdout_output_'):
         paths.append(os.path.join(logdir,fn))
# host LastTest logs - common build dirs used in this repo
paths.append(os.path.join(root,'esp_bt_audio_source','test','host_test','build_host_tests','Testing','Temporary','LastTest.log'))
paths.append(os.path.join(root,'esp_bt_audio_source','test','host_test','build-host','Testing','Temporary','LastTest.log'))
paths.append(os.path.join(root,'test','host_test','build_host_tests','Testing','Temporary','LastTest.log'))
# CI artifact test-results.xml (optional)
artdir=os.path.join(root,'.github','artifacts')
if os.path.isdir(artdir):
   for sub in os.listdir(artdir):
      p=os.path.join(artdir,sub,'test-results.xml')
      if os.path.isfile(p):
         paths.append(p)

summary={'total':{'tests':0,'failures':0,'ignored':0},'byfile':{}}
for p in paths:
   if not os.path.isfile(p):
      continue
   with open(p,'r',encoding='utf-8',errors='ignore') as f:
      txt=f.read()
   occ=0;ts=fs=ig=0
   for m in pattern.finditer(txt):
      t=int(m.group(1));f=int(m.group(2));i=int(m.group(3))
      ts+=t; fs+=f; ig+=i; occ+=1
   if occ>0:
      summary['byfile'][p]={'tests':ts,'failures':fs,'ignored':ig,'occurrences':occ}
      summary['total']['tests']+=ts
      summary['total']['failures']+=fs
      summary['total']['ignored']+=ig

outdir=os.path.join(root,'tmp')
os.makedirs(outdir,exist_ok=True)
outpath=os.path.join(outdir,'canonical_unity_summary.json')
with open(outpath,'w') as outf:
   json.dump(summary,outf,indent=2)
print('WROTE',outpath)
PY
```

Quick automated aggregator
-------------------------

To avoid accidental double-counting and to make CI and local runs reproducible, this repository includes a tiny helper script that scans only the canonical artifact locations and writes a JSON summary.

Location: `tools/aggregate_unity.py`

Usage examples:

```bash
# write aggregated totals (sums all matched summary lines in canonical files)
python3 tools/aggregate_unity.py --output tmp/canonical_unity_summary.json

# write aggregated totals but keep only the last summary per file ("latest-run only")
python3 tools/aggregate_unity.py --latest-only --output tmp/canonical_unity_summary.json
```

The script prints the files it found and writes a JSON file with the following shape:

{
   "generated_at": "...",
   "total": { "tests": N, "failures": M, "ignored": K },
   "by_file": { "path": { "entries": [ {"tests":.., "failures":.. } ], "count": X }, ... }
}

Notes:
- The script intentionally scans only canonical artifact locations (build/one_run_unity.log and host LastTest.log locations) to avoid README or other stray matches.
- In CI you can run this script after the locked-down runner and upload `tmp/canonical_unity_summary.json` as an artifact.


Notes:
- The aggregator intentionally scans only canonical artifact locations to avoid README/script/.git false-positives. Add more paths to the `paths` list if your CI uses different artifact locations.
- If you want a "latest-run only" snapshot instead of summing repeated occurrences, modify the script to keep only the last matched summary per file (or sort `idf_py_stdout_output_*` by mtime and pick the newest).

With this documented workflow you (or CI) can reproduce the canonical aggregation used by the project and avoid accidental double-counting from unrelated files.

Run & debug on-device Unity tests
-----------------------------------------------------
Use the exact sequence below to run Unity tests reliably and capture the canonical log. This avoids interactive monitor confusion and ensures instrumentation (for example `DIAG:` lines) is compiled into the flashed image.

1) Build (recommended: clean when iterating on instrumentation)
```bash
source $HOME/esp/esp-idf/export.sh
cd test_app
idf.py fullclean   # optional but useful when changing instrumented files
idf.py build
```

2) Flash + capture using the project's locked-down runner (preferred)
```bash
# from repository root
source $HOME/esp/esp-idf/export.sh
python3 tools/run_unity.py --project-root test_app --port /dev/ttyUSB0 --timeout 600
```
The runner flashes the image, captures serial to `test_app/build/one_run_unity.log`, watches for the Unity summary, and exits automatically (exit codes: 0=pass, 1=fail, 2=timeout, 3=error/interrupt).

- Quick checks after a run
- Search for Unity summary markers:
```bash
grep -n "--- SUMMARY ---\|Tests:\|FAIL" test_app/build/one_run_unity.log || true
```
- Search for instrumentation (example `DIAG:`):
```bash
grep -n "DIAG:" test_app/build/one_run_unity.log || true
```

Common pitfalls & tips
- Wrong project: running the runner from the production app root will flash the wrong image and you won't see Unity output. Always point `--project-root` at `test_app` (or `cd test_app` first).
- Stale builds: if your instrumentation doesn't appear in the serial log, run `idf.py fullclean` before `idf.py build` to force recompilation of edited files.
- Serial port: verify the correct device (e.g., `/dev/ttyUSB0` vs `/dev/ttyACM0`) using `dmesg` or `ls /dev/ttyUSB*`.
- Monitor behavior: `idf.py monitor` is interactive; prefer the runner or `timeout` for unattended runs.


Manual fallback (if you prefer raw commands)
-----------------------------------------

If you want to run the steps manually, here's the canonical sequence:

```bash
# build the test_app
cd test_app
idf.py build
# flash and capture monitor to the canonical file
idf.py -p /dev/ttyUSB0 flash monitor |& tee ../build/one_run_unity.log
```

Then search the canonical log for Unity summary markers:

```bash
grep -n "--- SUMMARY ---\|Tests:\|FAILED\|OK" -i build/one_run_unity.log || true
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
