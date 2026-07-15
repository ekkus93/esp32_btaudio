# ESP32 Bluetooth Audio Source

ESP32 firmware that streams I2S audio to Bluetooth speakers/headphones via A2DP. Part of a dual-ESP32 audio system: the WiFi controller sends audio via I2S, this device receives it and streams over Bluetooth Classic.

## Prerequisites

- ESP-IDF v5.5.1 installed
- Python tooling: the repo root has a uv-managed `.venv` for all Python tooling (test runners, scripts). To set it up:

```bash
# From the repository root, create the virtual environment and install deps
uv venv
uv pip install -r requirements.txt   # or uv pip install -e .
```

Then source both environments:

```bash
. $HOME/esp/v5.5.1/esp-idf/export.sh   # ESP-IDF toolchain and idf.py
. .venv/bin/activate                  # Python tooling for tests
```

## Quick Start

```bash
cd esp_bt_audio_source
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Requires an ESP32 WROOM32.

## Serial Command Interface

Commands are sent over UART (USB console or GPIO16/17 secondary port). Each command ends with `\n`.

| Command | Syntax | Description | Sample Output |
|---------|--------|-------------|---------------|
| `HELP` | `HELP` | Show list of all commands | `OK\|HELP\|DONE` |
| `STATUS` | `STATUS` | Get system status | `OK\|STATUS\|CURRENT\|MUTE=0,SAMPLE_RATE=44100,PAIRED_COUNT=2` |
| `VERSION` | `VERSION` | Get firmware version | `OK\|VERSION\|1.0.0` |
| `SCAN` | `SCAN` | Start Bluetooth scan | `OK\|SCAN\|STARTED` |
| `CONNECT` | `CONNECT <MAC>` | Connect by MAC address | `OK\|CONNECT\|INITIATED` |
| `CONNECT_NAME` | `CONNECT_NAME <NAME>` | Connect by device name | `OK\|CONNECT_NAME\|INITIATED` |
| `DISCONNECT` | `DISCONNECT` | Disconnect current connection | `OK\|DISCONNECT\|DONE` |
| `PAIR` | `PAIR <MAC\|NAME>` | Initiate pairing | `OK\|PAIR\|INITIATED` |
| `CONFIRM_PIN` | `CONFIRM_PIN [MAC] <ACCEPT\|REJECT>` | Respond to SSP confirm | `OK\|CONFIRM_PIN\|ACCEPTED\|MAC` |
| `ENTER_PIN` | `ENTER_PIN [MAC] <PIN>` | Submit legacy PIN code | `OK\|ENTER_PIN\|SENT\|MAC` |
| `SET_DEFAULT_PIN` | `SET_DEFAULT_PIN <PIN>` | Persist default PIN | `OK\|SET_DEFAULT_PIN\|SUCCESS\|PIN` |
| `PAIRED` | `PAIRED` | List paired devices | `INFO\|PAIRED\|ITEM\|MAC,Name` then `OK\|PAIRED\|COUNT\|N` |
| `UNPAIR` | `UNPAIR <MAC>` | Remove paired device | `OK\|UNPAIR\|REMOVED\|MAC` |
| `UNPAIR_ALL` | `UNPAIR_ALL` | Erase all paired devices | `OK\|UNPAIR_ALL\|SUCCESS` |
| `PARTS` | `PARTS` | List partitions | `INFO\|PARTS\|ITEM\|name,type=N,sub=N,off=0xNNNNNN,size=0xNNNNNN` |
| `SET_NAME` | `SET_NAME <NAME>` | Set Bluetooth device name | `OK\|SET_NAME\|SUCCESS\|NAME` |
| `START` | `START` | Start A2DP audio streaming | `OK\|START\|STARTED` |
| `STOP` | `STOP` | Stop A2DP audio streaming | `OK\|STOP\|STOPPED` |
| `VOLUME` | `VOLUME <0-100>` | Set playback volume | `OK\|VOLUME\|SET\|LEVEL` |
| `MUTE` | `MUTE` | Mute audio output | `OK\|MUTE\|SET` |
| `UNMUTE` | `UNMUTE` | Unmute audio output | `OK\|UNMUTE\|CLEARED` |
| `SAMPLE_RATE` | `SAMPLE_RATE <Hz>` | Apply I2S sample rate | `OK\|SAMPLE_RATE\|APPLIED\|RATE` |
| `SYNTH` | `SYNTH ON\|OFF` | Force synthetic audio | `OK\|SYNTH\|ENABLED` |
| `BEEP` | `BEEP` | Play 10s middle-C tone | `OK\|BEEP\|SENT` |
| `DIAG` | `DIAG` | Report connection/state | `OK\|DIAG\|STATE\|CONN=1,STREAM=1,MGR=1,I2S=1,BEEP=0` |
| `AUDIO_STATUS` | `AUDIO_STATUS` | Report audio engine stats | `OK\|AUDIO_STATUS\|CURRENT\|RING_CAP=N,RING_USED=N,SOURCE=I2S,BEEP=no,...` |
| `LAST_MAC` | `LAST_MAC get\|clear` | Get/clear last connected MAC | `OK\|LAST_MAC\|MAC` or `OK\|LAST_MAC\|CLEARED` |
| `SPANLOG` | `SPANLOG [N]` | Dump span log entries | `INFO\|SPANLOG\|ENTRY\|seq,ts,bytes,ring_used_kb,source,flags` |
| `MEM` | `MEM` | Show free memory | `OK\|MEM\|STATS\|DRAM=N,INTERNAL=N,8BIT=N,PSRAM=N` |
| `RESET` | `RESET` | Reboot device | `OK\|RESET\|REBOOTING` |
| `AUDIO_AUTOSTART` | `AUDIO_AUTOSTART on\|off\|get` | Enable/disable audio autostart | `OK\|AUDIO_AUTOSTART\|ENABLED` or `OK\|AUDIO_AUTOSTART\|DISABLED` |
| `I2S_CONFIG` | `I2S_CONFIG BCLK,WS,DIN,DOUT [RATE] [BIT] [CH]` | Configure I2S pins | `OK\|I2S_CONFIG\|APPLIED\|PINS=18,19,22,...` |
| `I2S_PROBE` | `I2S_PROBE [BCLK] [WS]` | Count clock edges (diag) | `EVENT\|I2SPROBE\|RESULT\|bclk_highs=N,ws_highs=N` |
| `I2S_RXTEST` | `I2S_RXTEST [TIMEOUT_MS]` | One blocking I2S read | `EVENT\|I2SRXTEST\|RESULT\|ret=0,read_bytes=N` |
| `I2S_CLKGEN` | `I2S_CLKGEN [MS]` | Bit-bang clock square wave | `EVENT\|I2SCLKGEN\|DONE\|bclk=18,ws=19,ms=4000` |
| `UARTAUDIO` | `UARTAUDIO START\|STATUS\|STOP` | High-speed serial audio streaming | `OK\|UARTAUDIO\|STARTING\|baud=921600` |

**DEBUG Subcommands:**
| Command | Syntax | Description | Sample Output |
|---------|--------|-------------|---------------|
| `DEBUG MOCK_ON` | `DEBUG MOCK_ON` | Enable mock mode | `OK\|DEBUG\|MOCK_ON` |
| `DEBUG MOCK_ADD` | `DEBUG MOCK_ADD <MAC>` | Add mock pairing | `OK\|DEBUG\|MOCK_ADD\|MAC` |
| `DEBUG MOCK_PAIR` | `DEBUG MOCK_PAIR <MAC>` | Start mock pairing | `OK\|DEBUG\|MOCK_PAIR_STARTED\|MAC` |
| `DEBUG BEEP_DIAG` | `DEBUG BEEP_DIAG` | Arm beep diagnostic | `OK\|DEBUG\|BEEP_DIAG_ARMED` |
| `DEBUG WORKER_DIAG` | `DEBUG WORKER_DIAG` | Emit worker diagnostic | `OK\|DEBUG\|WORKER_DIAG_EMITTED` |
| `DEBUG AUDIO_DIAG` | `DEBUG AUDIO_DIAG ON\|OFF` | Enable/disable audio diag | `OK\|DEBUG\|AUDIO_DIAG_ON` |
| `DEBUG AUDIO_DIAG_SUMMARY` | `DEBUG AUDIO_DIAG_SUMMARY` | Emit audio diag summary | `OK\|DEBUG\|AUDIO_DIAG_SUMMARY` |
| `DEBUG AUDIO_DIAG_PROBE` | `DEBUG AUDIO_DIAG_PROBE ARM\|DUMP` | Audio diag probe | `OK\|DEBUG\|AUDIO_DIAG_PROBE_ARMED` |
| `DEBUG LOG` | `DEBUG LOG <TAG> <LEVEL>` | Set log level | `OK\|DEBUG\|LOG_SET\|TAG:LEVEL` |
| `DEBUG FORCE_BEEP` | `DEBUG FORCE_BEEP` | Force beep diagnostic | `OK\|DEBUG\|FORCE_BEEP_SENT` |
| `DEBUG DRAIN_QUEUE` | `DEBUG DRAIN_QUEUE` | Drain ring buffer | `OK\|DEBUG\|DRAIN_QUEUE_DONE` |
| `DEBUG DRAM` | `DEBUG DRAM ON\|OFF` | Force DRAM allocation | `OK\|DEBUG\|DRAM_ON` |

**Output Format:** All responses use pipe-delimited format: `STATUS\|COMMAND\|RESULT\|DATA`

| Status | Meaning |
|--------|---------|
| `OK` | Command succeeded |
| `ERR` | Command failed or invalid |
| `INFO` | Informational data (multi-line responses) |
| `EVENT` | Async event notification |

### UARTAUDIO: High-speed Serial Audio Streaming

The `UARTAUDIO` command provides high-speed audio streaming over the USB console UART. A host PC sends stereo 22050 Hz s16le PCM audio data, which the ESP32 feeds into the A2DP audio engine for Bluetooth playback.

**How It Works:**
1. **Text Mode** (115200 baud): Normal command interface operates over the USB console UART.
2. **`UARTAUDIO START [baud]`**: Initiates streaming mode. The ESP32 switches the UART baud rate to the specified baud (default 921600, supports 230400/460800/921600). The device sends `OK|UARTAUDIO|STARTING` at 115200 baud before switching.
3. **Handshake**: The ESP32 sends `UA|READY` beacons at the new baud rate until the host sends the first data frame.
4. **Streaming**: The host sends framed binary audio data at the streaming baud. The ESP32 receives frames, validates CRC, and feeds audio samples to the A2DP ring buffer. Periodic `UA|FILL` feedback lines report fill levels.
5. **Stop**: The host sends an in-band `STOP` frame. The ESP32 responds with `UA|BYE`, restores 115200 baud, and emits `EVENT|UARTAUDIO|STOPPED` with counters (frames, bytes, CRC errors).
6. **Recovery**: If the host disconnects or fails to send data, the ESP32 auto-recovers to text mode after ~2s inactivity. If the host sends `UARTAUDIO START` but never sends data, the ESP32 aborts after ~5s and restores text mode.

**Commands:**
| Subcommand | Syntax | Description | Sample Output |
|------------|--------|-------------|---------------|
| `UARTAUDIO START` | `UARTAUDIO START [baud]` | Start streaming (baud: 230400, 460800, 921600) | `OK\|UARTAUDIO\|STARTING\|baud=921600,frame=N,ring=N,a2dp=1` |
| `UARTAUDIO STATUS` | `UARTAUDIO STATUS` | Query streaming status | `OK\|UARTAUDIO\|STATUS\|streaming=0,state=INACTIVE,used=0,cap=4096` |
| `UARTAUDIO STOP` | `UARTAUDIO STOP` | Stop (only valid during streaming via in-band frame) | `ERR\|UARTAUDIO\|NOT_STREAMING\|stop is the in-band STOP frame during streaming` |

**Python Test Tool:** `tools/stream_audio_uart.py` provides frame building and CRC utilities. The laptop BT test suite (`test/laptop_bt_tests/test_uart_streaming.py`) imports these and runs end-to-end tests that stream synthetic tones from a PC to the ESP32 over UART, which then plays over A2DP to the laptop's own Bluetooth adapter.

## Hardware

### GPIO Pin Assignments

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO18 | I2S BCLK | Bit clock (output) |
| GPIO19 | I2S WS | Word select / LRCLK (output) |
| GPIO22 | I2S DIN | Digital audio input |
| GPIO16 | UART2 RX | Secondary command port (115200 baud) |
| GPIO17 | UART2 TX | Secondary command port (115200 baud) |

All I2S pins are configurable via NVS or the `I2S_CONFIG` command. The secondary UART2 on GPIO16/17 serves commands while UARTAUDIO streaming owns the USB console port.

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

## Project Status

Stable. 725/725 host tests + 99/99 device tests passing. UARTAUDIO validated bit-faithful. See memory.md for recent changes.
