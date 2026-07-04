---
name: laptop-bt-tests
description: Run the real-hardware laptop Bluetooth integration suite (esp_bt_audio_source/test/laptop_bt_tests) against the physical ESP32 and the laptop's own BT adapter. User-triggered only — never invoke automatically.
disable-model-invocation: true
---

Runs `esp_bt_audio_source/test/laptop_bt_tests/`, which drives the real over-the-air A2DP lifecycle (scan, pair, connect, stream, disconnect) between the laptop's Bluetooth adapter (via BlueZ D-Bus / `pydbus`) and a physical ESP32 flashed with **production firmware** on `/dev/ttyUSB0`. No mocks. Never run this in CI — CI only runs a software-mocked pairing harness.

Hardcoded hardware identifiers (see `test/laptop_bt_tests/conftest.py`):
- Laptop adapter MAC: `E8:FB:1C:25:E4:C2`
- ESP32 Classic BT MAC: `A0:B7:65:2B:E6:5E`
- Serial port: `/dev/ttyUSB0`

Before running:
1. Confirm with the user that the ESP32 is connected on `/dev/ttyUSB0` and flashed with current production firmware (rebuild/reflash first if the user wants to test recent changes — confirm before flashing, per the root CLAUDE.md).
2. Confirm the laptop's Bluetooth adapter is present and not mid-use by another pairing.
3. Note the run takes up to ~15 minutes for the full suite.

Run the full suite:
```bash
cd esp_bt_audio_source/test/laptop_bt_tests
conda run -n python310 python -m pytest \
    test_connection.py test_autoconnect.py test_streaming.py \
    test_control.py test_e2e.py test_uart_streaming.py \
    -v --timeout=120
```

Or via the wrapper (runs everything marked `laptop_bt`, including discovery/pairing):
```bash
conda run -n python310 esp_bt_audio_source/tools/run_laptop_bt_tests.sh
```

To run a single test file, pass it directly instead of the full list, e.g. `conda run -n python310 python -m pytest test_streaming.py -v --timeout=120`.

If no hardware is attached, `conftest.py`'s hardware-presence guard skips the suite cleanly — report the skip rather than treating it as a failure.

After running, report pass/fail counts and any failing test names — don't just say "tests ran."
