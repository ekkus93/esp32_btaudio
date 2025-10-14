# flash_and_watch helper

This small helper automates flashing the built firmware with `idf.py` and
running the serial monitor until the on-device Unity test summary is observed.

Usage:

```bash
# from the repo root
./tools/flash_and_watch.py --port /dev/ttyUSB0
```

Behavior:
- Sources `$HOME/esp/esp-idf/export.sh` automatically if `idf.py` is not in PATH and that file exists.
- Runs `idf.py -p PORT flash monitor` in the `esp_bt_audio_source` directory.
- Writes the complete monitor output to `esp_bt_audio_source/build/one_run_unity.log`.
- Stops the monitor automatically when a Unity summary line is detected and exits with:
  - 0 when summary is found and no obvious failures are detected in the log
  - 1 when summary found but `FAIL`/`Failures` are present in output
  - 3 when no summary detected (monitor left running)

Notes:
- If you prefer idf.py to remain unsourced in your shell, ensure it is already on PATH.
- The script is intentionally conservative and will not force-kill a running monitor if no summary is found; you can stop it manually.
