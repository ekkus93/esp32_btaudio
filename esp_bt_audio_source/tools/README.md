# Tools — esp_bt_audio_source

This folder contains helper scripts used for flashing, running and capturing on-device tests and simple CI/host smoke checks for the `esp_bt_audio_source` project.

Helper of interest
------------------

- `flash_and_verify_spiffs.py` — automates a reproducible flash + SPIFFS write + verification sequence for `esp_bt_audio_source`.

Purpose
-------

The `flash_and_verify_spiffs.py` helper performs the following steps (when called from the repository root):

1. Runs `idf.py -C <project-dir> flash` to flash the app and partition table.
2. Uses `esptool` to write the SPIFFS image to the configured SPIFFS partition offset.
3. Opens the serial port, sends `PARTS` and `FILES` commands, captures the responses and asserts the presence of expected `OK|...|SUMMARY` lines.

Usage
-----

Basic invocation (from repo root):

```bash
python3 esp_bt_audio_source/tools/flash_and_verify_spiffs.py \
  --port /dev/ttyUSB0 \
  --spiffs-image esp_bt_audio_source/main/assets/spiffs/spiffs.bin \
  --spiffs-offset 0x1C0000 \
  --project-dir esp_bt_audio_source \
  --baud 115200 \
  --monitor-wait 4
```

Options (summary)
- `--port` (required): serial device (for example `/dev/ttyUSB0`).
- `--spiffs-image` (required): path to the SPIFFS binary to write.
- `--spiffs-offset` (required): flash offset where the SPIFFS partition is located (hex, e.g. `0x1C0000`).
- `--project-dir` (optional, default `esp_bt_audio_source`): project path passed to `idf.py -C` for flashing.
- `--baud` (optional, default 115200): serial monitor baud for the test commands.
- `--monitor-wait` (optional, default 4): seconds to wait after flashing for the target to boot before opening serial.

Exit codes
----------

The helper uses the following exit codes to make it CI-friendly:

- `0` — success: flash completed, SPIFFS write succeeded, and both `PARTS` and `FILES` emitted expected `OK|...|SUMMARY` lines.
- `1` — verification failure: either `PARTS` or `FILES` did not report the expected summary lines within the configured timeout.
- `2` — fatal error: I/O or environment error (for example, cannot open serial, esptool not found, or `idf.py` failed to run).

CI example
----------

This snippet shows a minimal CI job step (assumes the runner has serial access and ESP-IDF is sourced):

```bash
. $HOME/esp/v5.5.1/esp-idf/export.sh
python3 esp_bt_audio_source/tools/flash_and_verify_spiffs.py \
  --port ${PORT:-/dev/ttyUSB0} \
  --spiffs-image esp_bt_audio_source/main/assets/spiffs/spiffs.bin \
  --spiffs-offset 0x1C0000 \
  --project-dir esp_bt_audio_source \
  --baud 115200 \
  --monitor-wait 6
if [ $? -ne 0 ]; then
  echo "SPIFFS flash or verification failed" >&2
  exit 1
fi
```

Notes & troubleshooting
-----------------------

- If `make_spiffs.py` is used to build the image it may fall back to `spiffsgen.py` when `mkspiffs` is not available — that is expected in some toolchains.
- If the `PARTS` output does not list a `spiffs` partition, verify the flashed partition table contains a `spiffs` label (the `esp_bt_audio_source` project builds `build/partition_table/partition-table.bin`).
- If the `FILES` output shows no entries, confirm the SPIFFS image actually contains files under `main/assets/spiffs/`.

stream_audio_uart.py — UARTAUDIO host streamer
===============================================

Streams PCM audio from this machine to the ESP32 over the USB serial cable
for immediate playback on the connected Bluetooth speaker/headset —
`UARTAUDIO`'s host-side counterpart (see the root README and
`components/command_interface/uart_audio.c`). Bypasses I2S entirely, so it
doubles as a diagnostic bypass for isolating I2S-link problems from the
BT/A2DP output path (used to confirm the A2DP/BT chain was healthy during
the 2026-07-25 I2S static investigation).

Wire format: stereo 22050 Hz s16le PCM in CRC-16/CCITT-FALSE-framed chunks
(magic `A5 5A`, see `components/command_interface/uart_audio_frame.c`). The
device 2x-upsamples to 44.1 kHz and plays it through the normal A2DP path.

Usage
-----

Input is a WAV file (must already be 22050 Hz / stereo / 16-bit) or `-` for
raw s16le on stdin. Speaker must already be connected (e.g. via the
`CONNECT` console command):

```bash
python3 esp_bt_audio_source/tools/stream_audio_uart.py --port /dev/ttyUSB0 song.wav
```

To play arbitrary audio, transcode with ffmpeg into stdin:

```bash
ffmpeg -i song.mp3 -ar 22050 -ac 2 -f s16le - | \
    python3 esp_bt_audio_source/tools/stream_audio_uart.py --port /dev/ttyUSB0 -
```

Options
- `audio` (positional): WAV file path, or `-` for raw s16le stdin.
- `--port` (default `/dev/ttyUSB0`): serial device.
- `--baud` (default `921600`): streaming baud rate (only `921600` carries
  stereo 22.05 kHz PCM in real time — see `uart_audio.c`'s `baud_is_supported()`).
- `-v` / `--verbose`: print device feedback lines (`UA|FILL|...` progress).

Ctrl-C stops cleanly: a STOP frame is sent, the device drains its buffer,
answers `UA|BYE`, and both sides drop back to 115200 text mode.

Host Pairing Test Harness
==========================

This folder contains a small test harness to exercise the device's deterministic
mock pairing flow over a serial console.

File
----
- `host_pairing_driver.py` — a Python 3 script that drives a deterministic pairing
  flow on the device and emits a single JSON result. It also writes a timestamped
  human-readable log to a log directory.

Quick usage
-----------
Run the harness against a connected device (example):

```bash
python3 esp_bt_audio_source/tools/host_pairing_driver.py \
  --port /dev/ttyUSB0 \
  --mac AA:BB:CC:DD:EE:FF \
  --timeout 30
```

Common options
--------------
- `--port` / `-p`: Serial device path (default `/dev/ttyUSB0`).
- `--baud` / `-b`: Baud rate (default `115200`).
- `--mac` / `-m`: MAC address to use for the deterministic mock pairing (default `AA:BB:CC:DD:EE:FF`).
- `--timeout` / `-t`: Timeout in seconds waiting for pairing (default `30`).
- `--log-dir` / `-l`: Directory to write the detailed log (default `build/pairing_e2e_manual_logs`).

Output
------
1) Human-readable log file: a timestamped file in `--log-dir`, e.g.
   `build/pairing_e2e_manual_logs/pairing_e2e_manual_20251011-024123.log`.
   This file contains timestamped RX/TX lines and helpful debugging text.

2) JSON result printed to stdout (single line). The JSON object includes:

- `start_ts`: ISO UTC start timestamp
- `end_ts`: ISO UTC end timestamp (when available)
- `port`: serial port used
- `baud`: baud rate used
- `mac`: MAC used for mock pairing
- `logfile`: path to the human-readable log
- `status`: one of `success`, `failed`, `timeout`, or `error`
- `reason`: optional text describing failure or error
- `events`: array of `{ ts: <ISO>, line: <text> }` entries captured during the run

Exit codes
----------
- `0` — pairing succeeded (`status == "success"`).
- `2` — pairing timed out or device reported failure (`status == "timeout"` or `status == "failed"`).
- `1` — internal error (failed to open serial port, exception, etc.).

CI integration notes
--------------------
- The harness prints a single JSON object to stdout. A CI step can run the
  harness and parse the JSON to decide pass/fail, or test the exit code.
- Example CI check (bash):

```bash
out=$(python3 esp_bt_audio_source/tools/host_pairing_driver.py --port /dev/ttyUSB0 --mac $MAC --timeout 30)
status=$(echo "$out" | jq -r .status)
if [ "$status" != "success" ]; then
  echo "Pairing test failed: $out"
  exit 1
fi
```

Troubleshooting
---------------
- If the script times out without seeing pairing events, ensure the device's
  command interface is exposed on the same USB console device (often `/dev/ttyUSB0`).
- Check that the baud is correct (115200 by default) and that the device is
  running firmware with the debug/mock pairing command handlers.

License
-------
Same license as the repository.
