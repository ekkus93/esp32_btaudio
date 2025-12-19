## 1. Overview

`esp_bt_audio_source` is the Bluetooth A2DP source half of a two-ESP32 audio system. It receives audio (I2S, WAV-from-SPIFFS, or synthesized waveforms), manages Bluetooth pairing/connection, and streams audio reliably to consumer speakers/headsets. This PRD retroactively captures the current product intent so engineering, QA, and documentation stay aligned while the firmware continues evolving.

### 1.1 Problem Statement
- Developers and integrators need a reference Bluetooth audio-source firmware that can pair with common sinks and stream audio with predictable latency/resiliency, without hand-modifying ESP-IDF boilerplate each time.
- The project lacked an authoritative requirements artifact; decisions were scattered across README, memory logs, and code comments. This PRD centralizes the "source of truth" for scope, requirements, and constraints.

### 1.2 Product Vision
Deliver a developer-friendly ESP32-based Bluetooth audio source that can be driven over serial commands, seamlessly switch among audio sources (live I2S feed, WAV-on-flash, synthesized beeps), and expose diagnostics/tests robust enough for CI and hardware labs.

## 2. Goals & Success Criteria

| Priority | Goal | Success Measurement |
| --- | --- | --- |
| P0 | Stream clean audio from at least one source (I2S, WAV, synth) to Bluetooth headsets and speakers | Manual validation + automated Unity suites show zero audio underrun/failure; paired device hears continuous playback for ≥5 minutes |
| P0 | Maintain deterministic command protocol over UART for scan/pair/connect/audio control | Host tests cover command parsing; device logs show `OK|...` responses under load |
| P0 | Provide a repeatable “run all tests” workflow (host CTest + 3 Unity suites) | `tools/run_all_tests.py` completes with 0 failures on reference hardware; artifacts captured to `tmp/*.json` and per-suite logs |
| P1 | SPIFFS WAV playback coexists with synthesized beep + realtime status tagging | WAV pipeline consumes metadata tags without drift (<1 tag mismatch per test suite) |
| P1 | Pairing persistence survives reboot and UNPAIR/UNPAIR_ALL flows | Manual pairing script log + Unity coverage confirm preserved/removed entries |
| P2 | Diagnostics (trace parser, timeline stats) available for field triage | `tools/parse_traces.py` + `trace_stats.py` produce CSV/JSON for any captured run |

Out-of-scope (for now): mobile app UX, BLE sink support, OTA update flows.

## 3. Personas & Use Cases

1. **Firmware Engineer (primary)**
	- Needs fast host tests during development and deterministic device sweeps before pushing to `master`.
	- Requires clear diagnostics (ringbuffer, SPIFFS, pairing) when regressions occur.

2. **Field Technician / Integrator**
	- Flashes reference firmware, uses UART commands (SCAN, CONNECT, PLAY) during installations.
	- Needs reliable SPIFFS asset flashes and help text with command catalog.

3. **CI / Lab Runner**
	- Executes nightly sweeps via `tools/run_all_tests.py`, stores artifacts, alerts on failure.
	- Needs instructions not to reuse stale logs and to respect flashing permissions.

Representative Use Cases:
1. Pair with a consumer headset and stream WAV file from SPIFFS after device boot.
2. Execute scripted SCAN + CONNECT_NAME for a known sink and start I2S streaming.
3. Run `run_all_tests.py` to collect host + Unity pass/fail counts for a release candidate.
4. Instrument audio ringbuffer diagnostics and parse them via `tools/parse_traces.py` to verify no overruns on DRAM-only boards.

### 3.1 Field Operation Expectations
- **Power-on to READY**: BT controller init + command interface available within 5 seconds of reset so technicians can issue SCAN immediately.
- **SCAN duration**: must complete within 10 seconds, emitting `INFO|SCAN|DEVICE_FOUND|...` lines and a terminal `OK|SCAN|COMPLETE|<count>` summary; CONNECT/PLAY remain invalid until this summary appears.
- **CONNECT timing**: succeed or fail within 8 seconds. On failure, firmware leaves the device in a recoverable IDLE state and reports the reason via `ERR|CONNECT|...`.
- **Auto-reconnect policy**: when sinks drop unexpectedly, firmware attempts one immediate reconnect; repeated attempts require explicit CONNECT to avoid runaway loops.
- **Command gating**: START/PLAY/BEEP while not connected must return `ERR|<cmd>|NOT_CONNECTED`; SCAN issued while streaming returns `ERR|SCAN|BUSY`.
- **UART interleaving**: asynchronous `EVENT|...` lines may appear between the command request and its `OK|...`/`ERR|...` response; responses must still be a single line tagged with the original command so scripts can correlate them.

## 4. Functional Requirements

### 4.1 Command Interface
- UART transport runs at 115200 baud, 8 data bits, no parity, 1 stop bit (8N1) over the reference board’s USB-to-serial bridge exposed as `/dev/ttyUSB0`.
- UART protocol parses uppercase tokens terminated by `\n`; tokens are case-sensitive and must reject mixed case.
- Supported commands: `SCAN`, `CONNECT`, `CONNECT_NAME`, `DISCONNECT`, `PAIR`, `PAIRED`, `UNPAIR`, `UNPAIR_ALL`, `START`, `STOP`, `PLAY`, `BEEP`, `VOLUME`, `MUTE`, `UNMUTE`, `FILES`, `PARTS`, `STATUS`, `VERSION`, `HELP`, `SET_NAME`, `SET_DEFAULT_PIN`, `SAMPLE_RATE`, `I2S_CONFIG`.
- Configuration persistence: `SET_NAME` and `SET_DEFAULT_PIN` must store their values in NVS immediately, while `SAMPLE_RATE`, `I2S_CONFIG`, and other tuning commands are runtime-only unless explicitly documented otherwise.
- HELP command prints the canonical list (with short descriptions) and must be updated whenever behavior changes.
- Each request must emit exactly one `OK|<COMMAND>|...` or `ERR|<COMMAND>|<CODE>|...` response line even if asynchronous `EVENT|...` logs interleave; scripts assume single-line completion.
- Error codes use the taxonomy `{BAD_SYNTAX,BAD_PARAM,NOT_CONNECTED,BUSY,NOT_FOUND,FAILED}` with optional subsystem suffixes (e.g., `FAILED|BT`).
- Error responses follow the canonical token format `ERR|<COMMAND>|<CODE>[|<SUBSYS>][|<HUMAN_MESSAGE>]` so host tooling can parse rigidly; `<SUBSYS>` remains optional but, when present, is a short token such as `BT`, `AUDIO`, or `FS`.
- State expectations:

| State | Entry Condition | Allowed Commands | Notes |
| --- | --- | --- | --- |
| IDLE | Controller initialized, no scan/connect/stream pending | `SCAN`, `PAIR`, `PAIRED`, `CONNECT`, configuration commands (`FILES`, `PARTS`, `STATUS`, etc.) | START/PLAY/BEEP must reject with `ERR|...|NOT_CONNECTED`. |
| SCANNING | Scan active | `SCAN` (idempotent), `STOP` (cancels scan), `STATUS`, `HELP` | CONNECT/PAIR requests fail fast with `ERR|...|BUSY`. |
| CONNECTED | ACL + A2DP up, not streaming | `START`, `STOP`, `PLAY`, `BEEP`, `DISCONNECT`, `UNPAIR`, `PAIRED`, diagnostic commands | `SCAN` returns `ERR|SCAN|BUSY` until streaming stops. |
| STREAMING | Audio pipeline feeding A2DP | `STOP`, `PLAY` (switch source), `DISCONNECT`, `STATUS`, diagnostics | SCAN/PAIR/BEEP requests reject with `ERR|...|BUSY`. |

- Command handlers must explicitly note whether they queue (rare) or reject when invoked in incompatible states.

### 4.2 Bluetooth & Pairing
- A2DP Classic BT source profile must be active (dual-mode controller); BLE roles remain disabled but may be re-enabled later if needed.
- Pairing persistence: store at least four bonded devices (current implementation supports eight) in NVS with LRU eviction when capacity is exceeded.
- PIN policy: default PIN `0000` unless overridden via `SET_DEFAULT_PIN`. SSP numeric comparison flows are surfaced as `EVENT|PAIR|CONFIRM|PIN=<xxxx>` and require host confirmation.
- Pair initiation: UART `PAIR <MAC>` or `PAIR <alias>` arms the controller for incoming pairing; remote-initiated pairing is ignored unless a pending request exists to reduce surprise bonds.
- Event logging: every PIN request, confirmation, success, and failure must emit `EVENT|PAIR|...|SEQ=<n>,TS=<ms>` with monotonic sequence numbers and millisecond timestamps for host verification.
- `PAIRED` command enumerates stored devices in deterministic order (`INFO|PAIRED|ITEM|<mac>,<name>` lines) and ends with `OK|PAIRED|SUMMARY|COUNT=<n>`.
- UNPAIR removes a specific device’s controller bond + NVS record; UNPAIR_ALL drops all entries and reports how many records were deleted.
- Disconnect policy: when sinks drop unexpectedly, perform one automatic reconnect attempt; subsequent retries require explicit CONNECT to avoid busy-looping.

### 4.3 Audio Pipeline
- Support three audio sources: I2S live capture, WAV playback from SPIFFS, synthesized beep fallback.
- PSRAM boards: audio ringbuffer capacity must be ≥128 KiB and logged at boot; watchdog resets caused by WAV or I2S enqueues are unacceptable.
- DRAM-only boards: guarantee ≥32 KiB audio ringbuffer and document the reduced capacity in boot logs. WAV playback must continue to pass Unity suites via chunk-throttling/pacing so “degrade gracefully” means “smaller buffer but still uninterrupted playback.”
- WAV playback must bypass beep synth while active, resume synth only when WAV drains or aborts.
- Metadata tag ringbuffer must stay in lock-step with audio ringbuffer (push/drop/reset) for both enqueue and discard paths.
- Ringbuffer diagnostics (free space before/after operations, chunk sizes) must be emitted via DIAG lines so tooling can detect regressions.

### 4.4 Reference Audio Configuration & Measurement
- Reference PCM format: 44.1 kHz, 16-bit, stereo, little-endian. WAV assets shipped in SPIFFS must match this exactly; I2S capture defaults to the same format. Support for other rates/widths is best-effort and must be documented separately.
- Codec expectation: ESP-IDF A2DP source uses SBC, bitpool 32 (≈328 kbps). Changes to codec/bitpool require explicit documentation and revalidation of interoperability + jitter.
- Latency/jitter measurement: instrumentation stamps timestamps around `audio_processor_read()`, `xRingbufferSend`, and A2DP callbacks. `tools/parse_traces.py` + `tools/trace_stats.py` compute inter-arrival deltas; jitter must remain <50 ms (95th percentile) for WAV and I2S runs with diagnostics disabled, and <60 ms with diagnostics enabled.
- Measurement scope: tests run under nominal load (no extra high-priority tasks), with PLAY and START commands executed after stable pairing. Logs providing evidence must be archived under `tmp/` for each release candidate.

### 4.5 Storage & Assets
- Only use `esp_bt_audio_source/main/assets/spiffs/spiffs.bin` as the canonical SPIFFS image.
- Provide helper to flash SPIFFS image at `0x1C0000` and verify via `PARTS`/`FILES` commands.
- Prevent command handlers from assuming SPIFFS is mounted; mount on-demand before FILES/PLAY.

### 4.6 Testing & Tooling
- `tools/run_all_tests.py` cleans prior artifacts, runs host CTest, then flashes and runs `test_app`, `test_app2`, and `test_app_audio` via runners.
- Per-suite logs stored at `esp_bt_audio_source/test/test_app*/build/one_run_unity.log`.
- Aggregated JSON/CSV recorded to `tmp/run_all_tests_summary.{json,csv}`; canonical summary to `tmp/canonical_unity_summary.json`.
- Trace parser and stats tools live under `tools/` and accept captured logs from host/device runs.

## 5. Non-Functional Requirements

| Area | Requirement |
| --- | --- |
| Reliability | No watchdog resets during normal WAV or I2S playback; recover gracefully from SPIFFS mount failures. |
| Performance | Audio pipeline keeps jitter < 50 ms (95th percentile) for WAV/I2S under nominal load as measured via `parse_traces.py` + `trace_stats.py`; < 60 ms with diagnostics enabled. |
| Usability | UART command responses remain legible even under diagnostic load; diagnostics can be gated via compile-time options. |
| Testability | Host tests execute in < 60 s on dev machines; full orchestrator sweep completes within 10 minutes. |
| Maintainability | No hidden configs (e.g., sdkconfig edits) without explicit approval; documentation (README, PRD) must mirror shipped behavior. |

## 6. Dependencies & Constraints

- **Platform**: ESP-IDF v5.4.x (ESP32-WROOM32 target). Must source `$HOME/esp/v5.4.1/esp-idf/export.sh` before running `idf.py`.
- **Hardware**: Reference board uses `/dev/ttyUSB0` for flashing/monitor. No BLE-only configs; controller must be dual-mode.
- **Storage**: NVS partition for settings, SPIFFS partition at offset `0x1C0000` (size `0x40000`).
- **Policy constraints** (per repo instructions):
  - Do not modify `sdkconfig`, partition tables, or introduce new components without explicit approval.
  - Flashing permission is standing once the user asks to “run all unit tests.”
  - Use `python310` conda env for Python tooling; do not create new venvs.

## 7. Metrics & Acceptance Criteria

1. **Test Coverage**: Host (22) + Unity (108) tests pass → `tmp/run_all_tests_summary.json` shows totals with zero failures. Acceptance requires two consecutive green orchestrator runs before release.
2. **Audio Continuity**: Manual WAV and I2S playback sessions show no underruns/DIAG warnings for ≥5 minutes (log evidence archived under `tmp/`).
3. **Pairing Persistence**: `PAIR` + reboot + `PAIRED` returns entry; `UNPAIR` removes entry on next boot. Evidence captured via UART logs.
4. **Diagnostics Availability**: Running `tools/parse_traces.py` on a latest `one_run_unity.log` produces >1000 records, consumed by `tools/trace_stats.py` without error.

## 8. Risks & Open Questions

| Risk/Question | Impact | Mitigation / Next Action |
| --- | --- | --- |
| SPIFFS image drift if contributors flash ad-hoc binaries | Playback failures, inconsistent tests | Enforce “only canonical spiffs.bin” rule; CI helper validates via PARTS/FILES |
| Audio metadata ringbuffer tasks incomplete | Diagnostics misalign, future features blocked | Finish tag init/deinit audit and add tests (tracked in memory.md) |
| Pairing event ordering not fully validated on hardware | Field regressions | Schedule pairing soak test, capture symbolized logs, and document pass/fail criteria in README + PRD. |
| PSRAM-dependent paths untested on PSRAM-equipped boards | Performance regressions | Acquire PSRAM board, run orchestrator with CONFIG_SPIRAM, record jitter + buffer metrics, update acceptance criteria accordingly. |

## 9. Milestones & Deliverables

| Milestone | Target / Date | Status (2025-12-04) | Description |
| --- | --- | --- | --- |
| M1 – Documentation Baseline | 2025-11-17 | ✅ Complete | PRD + README capture current behavior and testing workflow. |
| M2 – Metadata & Diagnostics Closure | +2 weeks from PRD (ETA 2025-12-01) | 🔄 In progress | Finish metadata ringbuffer audit, add tests, document in README. |
| M3 – Pairing Persistence Validation | +3 weeks (ETA 2025-12-08) | ⏳ Not started | Execute on-device pairing scripts, capture logs, update docs with results. |
| M4 – PSRAM Validation | Hardware availability dependent | ⏳ Blocked (needs PSRAM board) | Run orchestrator with CONFIG_SPIRAM hardware; record jitter + buffer metrics. |
| M5 – Release Candidate | After M3 + two green sweeps | ⏳ Pending | Two consecutive green orchestrator runs + updated docs + tagged release. |

## 10. Appendix / References
- `README.md` (esp_bt_audio_source root) — operational guide, command list, testing instructions.
- `memory.md` — rolling engineering log; specific TODOs and verification notes.
- `tools/run_all_tests.py`, `tools/run_unity.py`, `tools/flash_and_verify_spiffs.py` — automation and flashing helpers.
- Per-suite artifacts under `esp_bt_audio_source/test/test_app*/build/` and aggregated outputs under `tmp/`.
