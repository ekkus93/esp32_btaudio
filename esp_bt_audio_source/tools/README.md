Host Pairing Test Harness
=========================

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
