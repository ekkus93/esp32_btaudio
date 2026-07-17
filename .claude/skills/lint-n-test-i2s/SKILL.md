---
name: lint-n-test-i2s
description: Lint esp_i2s_source (clang-tidy, flake8) and run BOTH its host tests and on-device gate. User-triggered via /lint-n-test-i2s.
disable-model-invocation: true
model: haiku
---

Run everything from the repo root. All steps target `esp_i2s_source/` only — this skill does not touch `esp_bt_audio_source` or the repo-root `tools/`. Report results per step; do not stop on lint findings — collect them all, then summarize.

## Step 1 — Lint C files (clang-tidy)

Lint the C files that changed (working tree vs HEAD; if none, the files in the last commit):

```bash
cd esp_i2s_source
FILES=$(git diff --name-only HEAD -- '*.c' | grep -v test/ || true)
[ -z "$FILES" ] && FILES=$(git show --name-only --pretty=format: HEAD -- '*.c' | grep -v test/ || true)
for f in $FILES; do
    clang-tidy -p build "$f" 2>/dev/null | grep -v "warnings generated" || true
done
```

Requires `build/compile_commands.json` (from a prior `idf.py build`). If it is missing, say so and skip this step — do NOT run a device build just for lint. This project has its own `.clang-tidy` config (copied from `esp_bt_audio_source/.clang-tidy`: disables `readability-function-cognitive-complexity` and `readability-suspicious-call-argument`, both ESP-IDF logging-macro false positives).

## Step 2 — Lint Python (flake8)

```bash
. .venv/bin/activate
python -m flake8 esp_i2s_source/tools --max-line-length=120
```

Advisory only — report findings but count them separately from hard failures.

## Step 3 — Run host tests

`esp_i2s_source/tools/run_host_tests.sh` builds and runs the host CTest suite (19 suites: `test_sanity`, `test_signal_gen`, `test_pcm_ring`, `test_i2s_out_pump`, `test_i2s_out_gain`, `test_bt_link_parser`, `test_bt_link_session`, `test_wifi_sm`, `test_radio_parse`, `test_station_store`, `test_radio_resampler`, `test_ctrl_cfg`, `test_ctrl_sm`, `test_radio_lifecycle`, `test_pack_s16_msb`, `test_bt_link_lifecycle`, `test_ctrl_init`, `test_main_boot`). No hardware needed.

```bash
cd esp_i2s_source
./tools/run_host_tests.sh --strict
```

## Step 4 — Run device tests (S3 hardware gate)

`esp_i2s_source/tools/s3_device_gate.sh` builds, flashes the ESP32-S3 (auto-detects `/dev/ttyACM*`, no `--port` flag — it does not accept one despite what the README implies), boots the app, captures the console, and asserts the DIAG boot markers. This flashes hardware — **ask the user to confirm before running it** (root CLAUDE.md: never flash without confirmation).

- Confirmed (flash + boot + assert):
  ```bash
  cd esp_i2s_source
  ./tools/s3_device_gate.sh
  ```
- If the user only wants to re-assert against firmware already on the board (no reflash):
  ```bash
  cd esp_i2s_source
  ./tools/s3_device_gate.sh --no-flash
  ```
- If the user declines entirely, or no `/dev/ttyACM*` device is present, skip this step and clearly report it as skipped (not a failure).

By default the gate only warns on WiFi/I2S/BTLINK companion checks (S3-only boot is the hard requirement). Mention `--require-i2s` / `--require-link` (promote those to hard failures) and `--degraded` (relax companion/network requirements) as available options if the user wants a stricter or looser run, but don't add them unless asked.

## Report

End with a compact summary:
- clang-tidy: N findings (list file:line + check name; "clean" if none)
- flake8: N findings (advisory)
- host tests: pass/fail counts, naming any failing suites
- device gate: PASS/FAIL/skipped, with the gate's verdict output

If any test failed, quote the failing test output — never report just "tests ran".
