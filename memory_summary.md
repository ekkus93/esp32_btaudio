# Memory summary

_Generated: 2026-07-14T23:00:00Z — derived from memory.md; do not edit by hand._

## Current state

The project is a Bluetooth Classic A2DP audio source running on two ESP32 boards:
- **WROOM32** (`/dev/ttyUSB0`) — `esp_bt_audio_source` (BT A2DP source, audio processor)
- **ESP32-S3** (`/dev/ttyACM0`) — `esp_i2s_source` (I2S controller, web UI, radio)

Reliability hardening is in progress (esp_i2s_source S3 tasks + esp_bt_audio_source WR tasks).

**Latest completed work:**
- All S3 hardening tasks complete (S3-01 through S3-21): UART request ownership, radio generation isolation, resampler input consumption, decoder tail, signed sample packing, atomic SPSC ring, I2S writer stoppable, command worker, controller init, URI registration, stats snapshot, status coherence, bt_link rollback, radio_init leaks, NVS errors, transactional station mutations, persistence errors, http_client checks, codec support
- WR-01 (BT context mutex) completed via platform_shim mutex primitive
- WR-02 (audio stop timeout ownership) completed
- CLAUDE.md updated with build/test instructions using `$HOME` for paths

**What's next:**
- RH-WR-03: Audio engine startup acknowledgement (not started)
- RH-WR-04: Audio processor partial-init cleanup (not started)
- RH-WR-05: Bluetooth initialization rollback (not started)
- Phase 10 validation (ASan, firmware build, hardware regression, soak test) — not started
- Documentation updates (RH-DOC-01/02, RH-SEC-01) — not started

## Timeline

### July 2026 (latest)

- **2026-07-14 (Fable 5)** — RH-WR-02 audio stop timeout ownership
- **2026-07-14 (Fable 5)** — RH-S3-18/19 commit
- **2026-07-14 (Fable 5)** — RH-S3-18: Propagate controller and Wi-Fi persistence errors
- **2026-07-14 (Fable 5)** — RH-S3-08: Make I2S writer stoppable
- **2026-07-14 (Fable 5)** — RH-S3-07 atomic ring fix
- **2026-07-14 (Fable 5)** — RH-S3-05: Preserve compressed decoder tail
- **2026-07-14 (Fable 5)** — RH-S3-01 fix bt_link_send() request lifetime
- **2026-07-14 (Fable 5)** — RH-S3-02 verification + cleanup
- **2026-07-12 (Opus 4.8)** — Build + flash both boards, dead-code sweep, SPLIT_AND_REFRACT work
- **2026-07-12 (Opus 4.8)** — Host coverage 68.0%→78.1%, all 70 CTest suites green
- **2026-07-12 (Opus 4.8)** — Piano voice, adjustable prebuffer, scan fix, connected-sink reporting
- **2026-07-12 (Opus 4.8)** — UNIT_TESTS1_TODO.md 10/10 done
- **2026-07-04** — UART2 hardened (14 host tests), full sweep 837 tests green
- **2026-07-04** — Echo Buds E2E: PERFECT 24s stream to real headset
- **2026-07-04** — UARTAUDIO static SOLVED: engine throughput bug + UART FIFO overrun
- **2026-07-03** — UARTAUDIO plan approved, implementation started

### February 2026 (earlier)

- **2026-02-12** — All 7 test Phases COMPLETE (186 new tests)
- **2026-02-12** — AddressSanitizer CI caught 3 critical bugs
- **2026-02-12** — Full suite: 578 tests (479 host + 99 device), all passing

## Decisions & preferences

- **Flash policy:** Never flash without explicit user confirmation
- **Python:** Use conda `python3.10`; never create new venvs
- **Git:** No `Co-Authored-By:` trailers (commit-msg hook rejects them)
- **Test conventions:** Unity + CMake for host tests, ESP-IDF Unity for device tests
- **Models used:** Claude Opus 4.8, Claude Fable 5, various models
- **Board serial ports:** WROOM32 → `/dev/ttyUSB0`; ESP32-S3 → `/dev/ttyACM0`
- **UARTAUDIO wire format:** stereo 22050 Hz s16le, 921600 baud, upsample 2x to 44.1 kHz
- **Audio source priority:** beep > UART > synth > I2S > silence
- **ESP-IDF v5.5.1** at `$HOME/esp/v5.5.1/esp-idf`
- **CLAUDE.md:** Use `$HOME` in documentation, never hardcode local paths
- **memory.md:** Append-only journal (26K lines)

## Open items

- RH-WR-03 through WR-05: Not started
- Phase 10 validation (ASan, firmware build, hardware regression, soak test): Not started
- Documentation: RH-DOC-01 (stale I2S comments), RH-DOC-02 (BT ownership docs), RH-SEC-01 (credentials)
- `audio_processor_test.c` (709 lines) is dead code → deferred
- Physical UART2 verification pending (needs second USB-serial adapter)
- `esp_i2s_source` redo: planned as the device that talks to `esp_bt_audio_source` over UART2
