# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this component.

## Build

Standard ESP-IDF CMake + `idf.py`, no PlatformIO. Target ESP-IDF v5.5.1.

```bash
. $HOME/esp/v5.1/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Python Environment

The repo root has a uv-managed `.venv` for Python tooling:

```bash
. .venv/bin/activate
python tools/run_unity.py ...
python tools/run_all_tests.py ...
```

## Architecture: main.c must stay a clean bootstrap

`main/main.c` may not call BT APIs directly (`esp_a2d_*`, `esp_avrc_*`, `esp_bt_gap_*`, `esp_bluedroid_*` — except `esp_bt_controller_mem_release`), may not read UART beyond driver install, and must call NVS init exactly once. All BT logic belongs in `components/bt_manager`. This is enforced by `tools/ci_check_main_layering.sh main/main.c`.

Components: `audio_processor`, `bt_manager`, `bt_stack_stub`, `command_interface`, `nvs_storage`, `platform_shim`, `util_safe`.

### Audio Sources (priority order)
1. **Beep** — WAV/tone overlay (highest priority)
2. **UARTAUDIO** — stereo 22.05 kHz PCM from PC over USB serial at 921600 baud
3. **Synth** — synthesized tones (piano voice, arpeggios)
4. **I2S** — external I2S input
5. **Silence** — default fallback

### UART2 (Secondary Command Port)
GPIO16 (RX) / GPIO17 (TX) at 115200 8N1. Serves the text command protocol alongside USB (UART0).
