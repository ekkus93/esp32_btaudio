# ESP32 Bluetooth Audio Source

ESP32 firmware that streams I2S audio to Bluetooth speakers/headphones via A2DP. Part of a dual-ESP32 audio system: the WiFi controller sends audio via I2S, this device receives it and streams over Bluetooth Classic.

## Prerequisites

- ESP-IDF v5.5.1 installed
- uv-managed `.venv` at repository root for Python tooling (test runners, scripts)

```bash
. $HOME/esp/v5.1/esp-idf/export.sh
. .venv/bin/activate
```

## Quick Start

```bash
cd esp_bt_audio_source
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Requires an ESP32 WROOM32.

## Architecture

**Components:**
- `bt_manager` — Bluetooth lifecycle, connection management, pairing
- `audio_processor` — I2S slave receiver, ring buffer, audio sources
- `command_interface` — UART serial command protocol
- `nvs_storage` — Configuration persistence
- `platform_shim` — Host/ESP32 abstraction layer for testing

**Audio sources (priority order):**
1. Beep overlay (mixes over active source)
2. UARTAUDIO (development: PC → USB serial → A2DP)
3. Synth (test tones)
4. I2S (primary audio input)
5. Silence (fallback)

**Serial commands:** Send text commands over UART (USB console or GPIO16/17). See `components/command_interface/` for command reference. Key commands: `SCAN`, `CONNECT`, `START`, `STOP`, `VOLUME`, `STATUS`, `VERSION`.

## Testing

```bash
# Host tests (fast, no hardware)
cd test/host_test/build_host_tests
ctest --output-on-failure

# All tests (host + device, requires ESP32)
. .venv/bin/activate
python tools/run_all_tests.py --port /dev/ttyUSB0 --timeout 300
```

## Hardware

ESP32 WROOM32. I2S pins (BCLK=GPIO18, WS=GPIO19, DIN=GPIO22) configurable via NVS or `I2S_CONFIG` command. Secondary UART2 on GPIO16/17 for commands during UARTAUDIO streaming.

## Project Status

Stable. 725/725 host tests + 99/99 device tests passing. UARTAUDIO validated bit-faithful. See memory.md for recent changes.
