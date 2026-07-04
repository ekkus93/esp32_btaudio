# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this component.

## Build

Standard ESP-IDF CMake + `idf.py`, no PlatformIO. The repo references multiple ESP-IDF versions in different places (CI clones v5.5.1, local `.vscode/settings.json` points at v5.4.1, README says "v4.4+") — **target v5.5.1**, since that's what CI actually builds and gates on.

```bash
. $HOME/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # confirm with the user first — see root CLAUDE.md
```

Firmware version is set via `set(PROJECT_VER "X.Y.Z")` **before** `project(esp_bt_audio_source)` in `CMakeLists.txt`.

## Architecture: main.c must stay a clean bootstrap

`main/main.c` may not call BT APIs directly (`esp_a2d_*`, `esp_avrc_*`, `esp_bt_gap_*`, `esp_bluedroid_*` — except `esp_bt_controller_mem_release`), may not read UART beyond driver install, and must call NVS init exactly once. All BT logic belongs in `components/bt_manager`. This is enforced by `tools/ci_check_main_layering.sh main/main.c`, which runs automatically after edits to `main/main.c` (see `.claude/settings.json`) — fix any violation it reports rather than working around it.

Components: `audio_processor`, `bt_manager`, `bt_stack_stub`, `command_interface`, `nvs_storage`, `platform_shim` (host/ESP32 split for memory/storage/sync/timing — this is what makes host testing possible), `util_safe`.

Known unresolved issue: per `code_review/BT_STATE_ACCESS_CONTRACT.md`, the global `bt_ctx` struct is written only from `BtAppTask` via a work-dispatch queue but read from `cmd_proc` without synchronization (documented, not fixed). Be aware of this when touching `bt_manager` or `command_interface` status/diagnostics code.

## Testing — four tiers, know which one applies

**"Run the tests" with no tier specified defaults to the full sweep (tier 3 below).**

1. **Host unit tests** (no hardware) — plain CTest, not Unity:
   ```bash
   cd test/host_test && mkdir -p build_host_tests && cd build_host_tests
   cmake .. && cmake --build . -- -j"$(nproc)"
   ctest --output-on-failure
   ```
   Optional: `-DENABLE_COVERAGE=ON`, `-DENABLE_ASAN=ON`.

2. **On-device Unity suites** (real ESP32 required, flashes a test image — confirm before flashing) — `test/test_bluetooth`, `test/test_app_audio`, `test/test_manager`. **Ignore the top-level README's references to `test_app`/`test_app2` — those are stale; `test/test_app/` on disk is an empty leftover.**
   ```bash
   conda activate python310
   python tools/run_unity.py -p /dev/ttyUSB0 -r test/test_bluetooth
   ```

3. **Full regression sweep** — host CTest + all Unity suites + log aggregation (confirm before flashing):
   ```bash
   conda run -n python310 python tools/run_all_tests.py --port /dev/ttyUSB0 --timeout 300
   ```
   Flags: `--no-host`, `--no-device`, `--no-standalone`, `--coverage`, `--asan`, `--valgrind`. Authoritative result: `tmp/run_all_tests_summary.json`. Coverage HTML: `python3 tools/run_all_tests.py --no-device --coverage --no-standalone` then open `tmp/coverage_html/index.html`.

4. **Laptop BT hardware integration tests** (`test/laptop_bt_tests/`, real over-the-air A2DP against the laptop's own BT adapter via BlueZ D-Bus, no mocks — confirm before running, use the `/laptop-bt-tests` skill):
   ```bash
   cd test/laptop_bt_tests
   conda run -n python310 python -m pytest test_connection.py test_autoconnect.py test_streaming.py test_control.py test_e2e.py -v --timeout=120
   ```
   Hardcoded hardware: laptop adapter `E8:FB:1C:25:E4:C2`, ESP32 `A0:B7:65:2B:E6:5E`, port `/dev/ttyUSB0`. Never run on CI — CI only runs a software-mocked pairing harness (`pairing-harness.yml`), never real hardware.

Use `conda run -n python310` / `conda activate python310` for all Python-based test tooling — don't create a new env.

## Code style and workflow

- Follow TDD (red → green → refactor): write the failing test first, minimum code to pass, refactor only once green.
- Keep structural changes (refactoring/tidying) and behavioral changes (new behavior) in separate commits.
- `.clang-tidy` is deliberately close to full-default; the only disabled checks are `readability-function-cognitive-complexity` and `readability-suspicious-call-argument` (false positives against ESP-IDF macros/APIs). Don't disable others to silence a warning — fix the code instead.
- Inline comments like `/* CODE_REVIEW8 P2: ... */` are intentional traceability links back to `code_review/*.md` — don't strip them during cleanup.
- Before large refactors to `bt_manager` or concurrency-sensitive code, check `code_review/` (dated `CODE_REVIEW{N}.md` + paired `_TODO.md` files) for prior analysis.

## Git conventions

Conventional-commit prefixes (`feat(scope): …`, `fix(scope): …`, `docs: …`, `refactor(scope): …`, `test: …`). Commit subjects reference the task IDs from the relevant `*_TODO.md` (e.g. `BUG-1..9`, `CTRL-1`, `E2E-1`) and often report pass counts (e.g. "38/38 laptop BT suite"). Match this style.
