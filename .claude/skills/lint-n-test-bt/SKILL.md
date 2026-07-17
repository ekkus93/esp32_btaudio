---
name: lint-n-test-bt
description: Lint esp_bt_audio_source (clang-tidy, flake8, main.c layering check) and run BOTH its host tests and on-device Unity tests. User-triggered via /lint-n-test-bt.
disable-model-invocation: true
model: haiku
---

Run everything from the repo root. All steps target `esp_bt_audio_source/` only — this skill does not touch `esp_i2s_source` or the repo-root `tools/`. Report results per step; do not stop on lint findings — collect them all, then summarize.

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

## Step 3 — Lint Python (flake8, esp_bt_audio_source only)

```bash
. .venv/bin/activate
python -m flake8 esp_bt_audio_source/tools --max-line-length=120
```

CI treats flake8 as advisory (`|| true`); report findings but count them separately from hard failures.

## Step 4 — Run host tests + on-device Unity tests

`tools/run_all_tests.py` (repo root) is scoped entirely to `esp_bt_audio_source`: host CTest plus the three on-device Unity suites (`test_bluetooth`, `test_app_audio`, `test_manager`), with SPIFFS flashing and log aggregation. The device portion flashes `/dev/ttyUSB0` — **ask the user to confirm before running it** (root CLAUDE.md: never flash without confirmation).

- Confirmed (needs device on `/dev/ttyUSB0`) — runs host + device together:
  ```bash
  . .venv/bin/activate
  python tools/run_all_tests.py --port /dev/ttyUSB0 --timeout 300
  ```
- If the user declines, or `/dev/ttyUSB0` is absent, fall back to host-only (no confirmation needed) and clearly report that device tests were skipped:
  ```bash
  . .venv/bin/activate
  python tools/run_all_tests.py --no-device --no-standalone
  ```

Authoritative result: `esp_bt_audio_source/tmp/run_all_tests_summary.json`.

## Report

End with a compact summary:
- clang-tidy: N findings (list file:line + check name; "clean" if none)
- layering check: PASS/FAIL/skipped
- flake8: N findings (advisory)
- host tests: pass/fail counts from the summary JSON, naming any failing tests
- device tests: pass/fail counts per suite (test_bluetooth/test_app_audio/test_manager), or "skipped" with the reason

If any test failed, quote the failing test output — never report just "tests ran".
