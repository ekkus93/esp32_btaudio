---
name: lint-n-test
description: Lint the changed files (clang-tidy, flake8, main.c layering check) and run the full test sweep for esp_bt_audio_source. User-triggered via /lint-n-test.
disable-model-invocation: true
model: haiku
---

Run everything from the repo root. All steps target `esp_bt_audio_source/` (the main project). Report results per step; do not stop on lint findings — collect them all, then summarize.

## Step 1 — Lint C files (clang-tidy)

Lint the C files that changed (working tree vs HEAD; if none, the files in the last commit):

```bash
cd esp_bt_audio_source
FILES=$(git diff --name-only HEAD -- '*.c' | grep -v test/ || true)
[ -z "$FILES" ] && FILES=$(git show --name-only --pretty=format: HEAD -- '*.c' | grep -v test/ || true)
for f in $FILES; do
    clang-tidy -p build "$f" 2>/dev/null | grep -v "warnings generated" || true
done
```

Requires `build/compile_commands.json` (from a prior `idf.py build`). If it is missing, say so and skip this step — do NOT run a device build just for lint.

The `.clang-tidy` config is intentionally near-default: report findings, never suggest disabling checks (fix the code instead, per esp_bt_audio_source/CLAUDE.md).

## Step 2 — main.c layering check

Only if `main/main.c` changed (same FILES logic):

```bash
esp_bt_audio_source/tools/ci_check_main_layering.sh esp_bt_audio_source/main/main.c
```

## Step 3 — Lint Python (flake8, same scope as CI)

```bash
conda run -n python310 python -m flake8 tools esp_bt_audio_source/tools --max-line-length=120
```

CI treats flake8 as advisory (`|| true`); report findings but count them separately from hard failures.

## Step 4 — Run all tests (full sweep, tier 3)

The full sweep flashes test images to the ESP32 on /dev/ttyUSB0. **Ask the user to confirm before running it** (root CLAUDE.md: never flash without confirmation). Offer two options:

- Full sweep (needs confirmation, device on /dev/ttyUSB0):
  ```bash
  cd esp_bt_audio_source
  conda run -n python310 python tools/run_all_tests.py --port /dev/ttyUSB0 --timeout 300
  ```
- Host-only (no hardware, no confirmation needed) — use this automatically if the user declines or /dev/ttyUSB0 is absent:
  ```bash
  cd esp_bt_audio_source
  conda run -n python310 python tools/run_all_tests.py --no-device --no-standalone
  ```

Authoritative result: `esp_bt_audio_source/tmp/run_all_tests_summary.json`.

## Report

End with a compact summary:
- clang-tidy: N findings (list file:line + check name; "clean" if none)
- layering check: PASS/FAIL/skipped
- flake8: N findings (advisory)
- tests: pass/fail counts from the summary JSON, naming any failing tests

If any test failed, quote the failing test output — never report just "tests ran".
