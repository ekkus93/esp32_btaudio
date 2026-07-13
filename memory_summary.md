# Memory summary

_Generated: 2026-07-13T22:20:16Z — derived from memory.md; do not edit by hand._

## Current state

The project is a Bluetooth Classic A2DP audio source running on two ESP32 boards (WROOM32 on `/dev/ttyUSB0` running `esp_bt_audio_source`, and ESP32-S3 on `/dev/ttyACM0` running `esp_i2s_source`). Both were last built and flashed on 2026-07-12.

**Latest completed work:** Full host coverage sweep via `UNIT_TESTS1_TODO.md` raised host line coverage from 68.0% to 78.1% (func 70.4%→84.7%); all 70 CTest suites pass. SPLIT_AND_REFRACT work split large files (`bt_source_mock.c` 1937→4 files, `bt_source_stubs.c` 1763→4 files) into <700-line domain files. Dead-code sweep removed 13 orphaned test files (~2100 lines). Both boards flashed and verified with latest code.

**What's next:** `audio_processor_test.c` (709 lines) is the only remaining >700-line file but is dead code → deferred to the low-priority dead-code sweep. The scan-broadcast fix for WROOM32 is flashed and verified. Piano voice (SG synthesizer) is flashed. Adjustable prebuffer runtime control is verified.

## Timeline

### July 2026 (latest)

- **2026-07-12 (Opus 4.8)** — Build + flash both boards (WROOM32 → `/dev/ttyUSB0`, S3 → `/dev/ttyACM0`) post refactoring + dead-code sweep. Hash verified, both reset.
- **2026-07-12 (Opus 4.8)** — Dead-code sweep: removed 13 unbuilt test scaffolding files (~2100 lines). Commit 3241ef59.
- **2026-07-12 (Opus 4.8)** — SPLIT_AND_REFRACT #6: split `bt_source_stubs.c` (1763) into 4 domain files + stubs_internal.h. Commit a4efad74.
- **2026-07-12 (Opus 4.8)** — SPLIT_AND_REFRACT #5: split `bt_source_mock.c` (1937) into 4 domain files + internal.h. Commit 3050c291.
- **2026-07-12 (Opus 4.8)** — UNIT_TESTS1_TODO.md: 10/10 tasks done. Host coverage 68.0%→78.1% line, 70.4%→84.7% func. All 70 CTest suites green.
- **2026-07-12 (Opus 4.8)** — Host coverage analysis: 68.0% overall (4750 instrumented lines), 66 host suites green, 725/725 host cases + 99/99 device Unity pass.
- **2026-07-12 (Opus 4.8)** — Piano voice flashed (SG additive harmonics + fast attack + per-harmonic exponential decay).
- **2026-07-12 (Opus 4.8)** — Adjustable prebuffer flashed: runtime-adjustable via `/api/prebuffer`; 9000 clamps to 5000; NVS persistence confirmed.
- **2026-07-12 (Opus 4.8)** — Scan fix (broadcast `cmd_send_response_all()`) flashed + verified: `/api/scan` now populates discovered devices. WROOM32 PAIRED_COUNT=1 (Echo Buds only; laptop adapter unpaired).
- **2026-07-12 (Opus 4.8)** — Root-caused empty scan list: WROOM32 `bt_scan_emit_results()` misrouted INFO|SCAN|RESULT lines to USB primary instead of `bt_link` to S3. Fix: new `cmd_send_response_all()` broadcast.
- **2026-07-12 (Opus 4.8)** — Piano note length WiFi race fixed: browser fired setTone/toneOff fire-and-forget; short note's DELETE could beat POST. Fix: `await setTone` before scheduling note-off.
- **2026-07-12 (Opus 4.8)** — Verified note length over real BT link via BlueZ `parec` capture (laptop A2DP sink). Note duration controls duration end-to-end (monotonic).
- **2026-07-12 (Opus 4.8)** — Piano card flashed + verified on S3 (press-to-play via `/api/tone`).
- **2026-07-12 (Opus 4.8)** — Radio/Volume UI batch: volume card full width, "now playing" moved to top, inline/accordion station edit, station reorder (swap-with-neighbor via `station_store_move()`). 13/13 host tests, UI 9/9.
- **2026-07-12 (Opus 4.8)** — Fixed stale connected=true: WROOM32 keeps RUN=1 after disconnect. Fix: derive `connected` from CONN_MAC (non-empty = real A2DP peer).
- **2026-07-12 (Opus 4.8)** — Connected-sink feature E2E verified: STATUS reports CONN_MAC, UI shows "Connected to ArIsu BT Headset" + badge. Unpaired laptop adapter to reconnect to Echo Buds.
- **2026-07-04** — UART2 hardened to 14 host tests; next: redo `esp_i2s_source`.
- **2026-07-04** — Full sweep green: 837 tests, 0 failures. test_bluetooth device suite fixed (46/46).
- **2026-07-04** — test_bluetooth device suite FIXED: new `bt_manager_api_mock.c` wraps the BT manager API as pure delegates. 46/46 pass.
- **2026-07-04** — Echo Buds E2E test: PERFECT 24 s stream to real headset (48:78:5E:D9:35:A3) — zero defects.
- **2026-07-04** — FIFO_OVF convicted as UARTAUDIO tapping root cause; threshold fix (RX full threshold 32) → zero-defect streams.
- **2026-07-04** — UARTAUDIO static SOLVED: engine throughput bug (1 chunk/wake → 8 chunks/wake) + UART FIFO overrun fix.
- **2026-07-04** — UARTAUDIO implementation COMPLETE (9 steps committed). Wire format: stereo 22050 Hz s16le at 921600 baud.
- **2026-07-04** — Secondary UART2 command port added (RX=GPIO16 TX=GPIO17) — commands served alongside USB.
- **2026-07-04** — UARTAUDIO validated on hardware: 6/6 laptop-BT tests green.

### February 2026 (earlier)

- **2026-02-12** — BBGW I2S Source CI Test Timing Fix: race condition in `test_mixed_responses_and_events`; sleep 0.3→0.5s. Commit a2a65474.
- **2026-02-12** — AddressSanitizer CI caught 3 critical bugs: production queue leak in `i2s_manager.c`, buffer overflow in `test_cmd_handlers_files.c`, test leak in `test_list_ownership.c`. Commit 8c87ee1e.
- **2026-02-12** — CI Integration: Added 3 new GitHub Actions jobs (ASan, Valgrind, Coverage) to `.github/workflows/pairing-harness.yml`.
- **2026-02-12** — AddressSanitizer implementation: `--asan` flag in `run_all_tests.py`, CMake `ENABLE_ASAN` option. 450/450 tests pass with ASan.
- **2026-02-12** — All 7 test Phases COMPLETE (186 new tests). Full suite: 578 tests (479 host + 99 device), all passing.
- **2026-02-12** — Phase 7 (Integration Tests): 11 tests (8 integration/recovery + 3 concurrency).
- **2026-02-12** — Phase 6 (Audio Processor): 21 tests covering core logic, read path, ringbuffer, diagnostics.
- **2026-02-12** — Phase 5 (BT Manager): 47 tests across BT connection, streaming, scan, pairing edge cases.
- **2026-02-12** — Phase 4 (I2S Manager): 31 tests covering config errors, runtime errors, cleanup, mock queue.
- **2026-02-12** — Phase 3 (Beep Manager): 25 tests across beep edge cases and manager subsystem.
- **2026-02-11** — Phase 5.1: BT Manager profile init edge cases (6 tests). Phase 5.2: Connection manager edge cases (7 tests). Phase 5.3: Streaming manager (7 tests). Phase 5.4: bt_scan.c (13 tests). Phase 5.5: Connection/pairing/events (14 tests).

## Decisions & preferences

- **Flash policy:** Never flash without explicit user confirmation (despite `README.md` note claiming standing permission). User has confirmed flashing both boards multiple times.
- **Python:** Use conda `python3.10`; never create new venvs.
- **Git:** Do NOT add `Co-Authored-By:` trailers to commits (global `commit-msg` hook rejects them).
- **Test conventions:** Host tests use Unity + CMake; device tests use ESP-IDF Unity via `idf.py build`. All test phases follow TDD (Red-Green-Refactor).
- **Models used:** Claude Opus 4.8 (1M context) for recent work, various models for earlier work.
- **Board serial ports:** WROOM32 (esp_bt_audio_source) → `/dev/ttyUSB0`; ESP32-S3 (esp_i2s_source) → `/dev/ttyACM0`.
- **UARTAUDIO wire format:** stereo 22050 Hz s16le, CRC-framed 1024 B payloads at 921600 baud, upsamples 2× to 44.1 kHz on device.
- **Audio source priority:** beep > UART > synth > I2S > silence.

## Open items

- `audio_processor_test.c` (709 lines) is dead code → deferred to low-priority dead-code sweep (not a split target).
- Physical UART2 verification pending (needs second USB-serial adapter).
- `esp_i2s_source` redo: planned as the device that talks to `esp_bt_audio_source` over UART2 command port (requirements discussion not yet held).
