# Memory summary

_Generated: 2026-07-21T20:30:57Z — condensed from `memory.md` (1.3 MB / ~26,085 lines) via full-file read. This file is fully regenerated each time `/summarize-memory` runs; do not hand-edit._

`memory.md` is an append-only journal but its ordering is inconsistent — long stretches are reverse-chronological (newest entries first), at least one large block (BBGW-era Nov 2025–Jan 2026 entries) is spliced in out of order relative to its neighbors, and the file ends (last entry) at **2026-07-17T15:46Z**. This summary is organized **by project/topic**, not strictly by file order, with date ranges noted per section.

## Current state (as of the last entry in memory.md, 2026-07-17)

Two-board system:
- **WROOM32** (`/dev/ttyUSB0`) runs `esp_bt_audio_source` — Bluetooth Classic A2DP **source** firmware (only board with BT radio).
- **ESP32-S3** (`/dev/ttyACM0`) runs `esp_i2s_source` — WiFi/web-UI/internet-radio/I2S controller (no BT).
- Signal chain: internet radio → S3 decodes (MP3/AAC via ESP-ADF/esp_audio_codec) → I2S → WROOM32 → A2DP → BT sink. S3's `ctrl` orchestrator drives WROOM32 over a UART "bt_link".

**Last thing in the log:** two `SPLIT_AND_REFRACT` structural splits on `esp_i2s_source` (mirroring the same technique already used on `esp_bt_audio_source`'s `bt_source_mock.c`/`bt_source_stubs.c`) — `radio.c` (1158→655 core + 3 domain files) and `test_radio_lifecycle.c` (904→560+402). Both verified (19/19 host suites green under `--strict/--asan/--ubsan`, device build clean) but **left uncommitted** at end of file.

Note: work described in this session's own conversation (further `SPLIT_AND_REFRACT` on `bt_source_mock.c`/`bt_source_stubs.c`, a dead-code sweep, flashing both boards, and a laptop-as-BT-headset playback test) happened **after** the last entry currently in `memory.md` and is not yet reflected here — it should appear once that session appends its own entry.

---

## Project: `esp_bt_audio_source` (WROOM32 — BT Classic A2DP source)

### Audio pipeline architecture (evolved through several review cycles, Feb 2026)

- **Original bug wave (CODE_REVIEW6, ~2026-02-04/05):** multi-producer FreeRTOS queue design had 5 real bugs — mono→stereo upmix heap-overflow risk, resampler over-consuming input at EOF, I2S captures silently truncated to 1KB of an 8KB read (87.5% of I2S audio discarded), lost backpressure, and cross-source races. Root-fixed by **migrating to a single-producer/single-consumer (SPSC) ring buffer** (`audio_ringbuffer.c`, 8–256KB configurable, optional PSRAM) driven by a dedicated `audio_engine_task` (2ms/500Hz tick). Each source became a non-blocking `*_source_fill()` API (WAV, I2S, synth; beep became a stateful in-place *mixer*, not a source). `audio_processor_read()` (the A2DP consumer) shrank to a single `audio_rb_read()` call with zero-fill on underrun. Watermark backpressure (pause/resume at 24KB/8KB). Span-log ring buffer + `AUDIO_STATUS` command added for postmortem diagnostics.
- **WAV/SPIFFS/PLAY entirely removed** ("REMOVE_PLAY" project, 2026-02-07/08): reclaimed ~1.01 MB flash (13KB binary + 1MB SPIFFS partition), ~1500 LOC deleted. Left a 3-source model (I2S / beep / synth). Root-caused and fixed a critical **task-watchdog bug**: `CONFIG_AUDIO_AUTOSTART_DEFAULT=y` started the audio engine at boot with no source configured, starving IDLE1 and tripping the watchdog every 5s — fixed by disabling autostart-by-default. Also fixed unknown commands being silently ignored instead of returning an error.
- **Cooperative shutdown** (CODE_REVIEW P0.1): replaced an external `vTaskDelete()` on the audio engine task (could kill it mid-lock, mid-stats-update, or leak a 512B buffer) with a flag + `EventGroupHandle_t` handshake (`ENGINE_RUNNING_BIT`/`ENGINE_STOPPED_BIT`), task self-deletes cleanly. Documented in `docs/COOPERATIVE_SHUTDOWN.md`.
- **BEEP priority mode** (F1, 2026-02-11): beep now *preempts* I2S/synth (stops them, plays as a pure tone, drains the ring buffer to avoid "late beep") and restores the exact prior source afterward, instead of trampling state.
- **Data races fixed** with `portMUX_TYPE` spinlocks: `bt_streaming_info_t` (ISR callback vs task-context writers) and `s_audio_stats`. BT context access converted to route through `BtAppTask` via request/queue rather than a mutex (avoids priority inversion).
- **bt_manager.c split** (CODE_REVIEW8, ~1852→1111 lines) into `bt_pairing_store`, `bt_scan`, `bt_connection`, `bt_events_{gap,a2dp,avrc}`.
- **Platform shims** added (`platform_storage`, `platform_malloc/calloc/free`, timing) to decouple app code from ESP-IDF headers — 27 fewer ESP-IDF includes in app code.
- **A2DP callback hardening**: `int32_t len` from the BT stack was unvalidated — negative values could cause huge buffer-overrun arithmetic via signed→size_t cast. Fixed by rejecting `len<0`/`len==0` at entry.
- **NVS wear fix**: volume changes wrote to NVS on every adjustment (~100-day flash life); added 500ms debounced commit (~27+ year projected life).
- **Hot-path logging**: gated 3 `ESP_LOGW` calls in the 344Hz A2DP callback behind `CONFIG_BT_VERBOSE_AUDIO_LOGGING` (they cost 1–5ms each — an underrun "death spiral" risk).
- **Streaming WAV resampler** (CODE_REVIEW5, before WAV removal): replaced block-local `floor()` resampling (cumulative rounding loss, "ends early" bug) with a Q16.16 fixed-point phase-accumulator streaming resampler. (Later became moot once WAV/PLAY was removed entirely.)
- **main.c architecture cleanup** (CODE_REVIEW2/3, Jan–Feb 2026): reduced main.c 1019→226→319 lines, removed ~757 lines of duplicated inline BT code. Fixed init order (was BT-before-CMD; now UART→NVS→CMD→BT→Audio, "control plane before data plane"). Established ownership rules still in force: **main.c installs the UART driver once at boot and never deletes it** (deleting it had broken esp-console/logging); NVS is initialized exactly once by main.c. Error-handling policy: platform services fail-fast (`ESP_ERROR_CHECK`), subsystems (BT/Audio/CMD) log and gracefully degrade. `tools/ci_check_main_layering.sh` enforces this.

### UARTAUDIO feature (2026-07-03 to 07-05) — replaces I2S wiring as the dev audio-test path

Streams real audio from the laptop over the existing USB-serial cable into the ESP32, played over A2DP.
- **Wire format** (CP2102 caps at 921600 baud): stereo 22050 Hz s16le (88.2 KB/s), 2× upsampled on-device to 44.1 kHz (exact-ratio linear interpolation). CRC-16/CCITT-FALSE framed, 1024B nominal payload.
- New `AUDIO_SOURCE_UART`; priority became **beep > UART > synth > I2S > silence**. 32KB staging ring, prebuffer→active at 50% fill. Baud runtime-switches 115200↔921600 during a stream; `cmd_process()` gates out while streaming.
- **Hardware bring-up found real bugs, not env noise:** (1) audio engine produced only 1 chunk/wake and `CONFIG_FREERTOS_HZ=100` clamped the tick to 10ms, capping throughput at 102.4 KB/s vs the needed 176.4 KB/s — A2DP zero-filled the ~40% gap on every underrun (flagged: the I2S capture path may have the same latent issue). Fixed by producing up to 8 chunks/wake. (2) UART hardware FIFO (128B) overran during BT flash-cache windows — fixed with `CONFIG_UART_ISR_IN_IRAM=y` + 16KB RX buffer + lowering the RX-full ISR threshold to 32 during streaming (default ~120 left only ~87µs ISR budget under BT interrupt masking, insufficient — 32 gives ~1ms).
- Result: **zero-defect streams** (und/crc/lost/ovf all 0) on repeated runs, and a real-headset (Echo Buds) 24s validation run held steady A2DP throughput at 176.6–176.8 kB/s.
- Added dual-UART: UART2 (RX=GPIO16/TX=GPIO17, 115200) as a secondary command port so commands still work mid-stream. Physical hardware verification of UART2 was still pending at file end (needs a second USB-serial adapter).
- Along the way, fixed the `test_bluetooth` device suite which had been **broken for months** (mock had drifted ~25 symbols behind the real `bt_manager` API) via a new `bt_manager_api_mock.c` wrapper.

### Test infrastructure evolution

- ASan added as a CMake option (`--asan`), caught 3 real production bugs early on (an `i2s_manager.c` queue leak on an error path, a test buffer overflow, a minor test-only leak).
- CI gained ASan (every push), Valgrind (master-only), and coverage jobs; baseline coverage measured at 62.9%, later 68→78.1%.
- `components/components/` — a 300MB, 16,750-file full ESP-IDF mirror kept just for 2 host tests — replaced with a 156KB `test/host_test/esp_idf_stubs/` (99.95% size reduction).
- Suite consolidation: `test_beep_manager+test_i2s_manager+test_synth_manager` → `test_manager`; `test_app+test_app2+test_app3` → `test_app_all` (later renamed `test_bluetooth`). Cut suite count 7→3, flash time ~60-67%.
- Coverage push (Feb 2026, `UNIT_TEST_TODO.md`) added ~186 tests across 7 phases (command handlers, NVS error injection, BT state machines, audio processor, integration/concurrency). Deleted the vestigial `audio_processor_wav.c` (142 lines) as dead code after WAV removal.
- Known gap: `tools/run_all_tests.py` silently counts build-failed suites as 0 failures (never fixed as of file end).
- **Recent (2026-07-12, per this session's own memory, not yet reflected further):** dead-code sweep, `SPLIT_AND_REFRACT` splits of `bt_source_mock.c`/`bt_source_stubs.c`, both boards flashed, and full-sweep verification (820 host + 99 device tests green) — see the live conversation rather than this file for the very latest state.

---

## Project: `esp_i2s_source` (ESP32-S3 — WiFi/I2S controller, no Bluetooth)

### PRD design phase (2026-02-05/06)

Key locked decisions, all still authoritative:
- Target hardware changed from WROOM32 to **ESP32-S3** (512KB SRAM headroom for WiFi+web+decode); `esp_bt_audio_source` stays on WROOM32.
- Default sample rate 48kHz (matches `esp_bt_audio_source`'s new default too).
- UART protocol between the boards inherits `esp_bt_audio_source`'s exactly: 115200 8N1, `COMMAND ARGS\n` / `OK|ERR|EVENT` replies.
- Audio framework: **ESP-ADF** chosen over libhelix-mp3 for multi-codec (MP3/AAC/FLAC/OGG) support, at ~200KB flash/~60KB heap cost.
- Web UI: `esp_http_server`, forced password change on first login, single-user access, STA→AP auto-revert after 30s join failure.
- Stream resilience: 4-phase auto-reconnect (mute → exponential backoff 1/2/4s ×3 → resume or user-intervention UI).
- Testing strategy: 30+ test matrix, cross-device harness (two ESP32s + logic analyzer for I2S timing).

### I2S hardware bring-up saga (2026-07-11) — the hardest bug of this project

- Diagnosed classic ESP32 (WROOM32) **I2S slave-RX as non-functional** (silicon limitation, RX FIFO never fills). Tried flipping roles (WROOM32 master-RX / S3 slave-TX): master RX worked but wouldn't emit BCLK/WS on any tested pins — a long dead-end (DAC-pin theory, disproven).
- **Actual root cause** (fresh-eyes pass): stale NVS-saved pins were silently overriding `main.c` defaults, so days of "clock not emitted" testing were on unwired pins. Underlying real bug: classic I2S derives WS width from *data* width (16-bit) and silently ignores `slot_bit_width=32`, giving a 32:1 BCLK:WS ratio instead of the S3's expected 64:1 — fixed by running classic I2S at bit_depth=32. Also fixed a 32-bit right-alignment capture shim and a bug that discarded most captured audio on any partial read (only bail on 0 bytes, not on `ESP_ERR_TIMEOUT`).
- **Final working topology:** WROOM32 I2S1 master-RX (GPIO18 BCLK/19 WS/22 DIN, 32-bit) ↔ S3 I2S0 slave-TX (GPIO15 BCLK/16 WS/7 DOUT). Verified via laptop A2DP FFT capture: 440.00 Hz tone, 100% purity, 0% dropouts.
- Post-fix cleanup found 3 of 4 earlier "mystery fixes" (glitch filters, port choice, APLL clock) were red herrings from the wrong root-cause era.

### Feature buildout (2026-07-11/12) — REDO1 roadmap marked fully complete 2026-07-12

- **LINK-1c**: dual-UART control link ("bt_link") between the boards — two hard-won bugs (wrong wiring pins hung the whole board; a first-command race fixed with flush+CRLF).
- **WIFI-1**: STA/AP fallback, mDNS, console.
- **WEB-1**: embedded React SPA + REST API; WebSocket terminal was tried first but **replaced by REST polling** after the WS server exhausted `CONFIG_LWIP_MAX_SOCKETS=10` (terminal/BT died while music played) — bumped the socket limit to 16.
- **BTUI-1**: browser-based BT scan/pair/connect/volume UI.
- **RADIO-1/2**: playlist/ICY parsing, HTTP streaming with a PSRAM ring buffer, MP3/AAC decode — internet radio finally plays over Bluetooth end-to-end.
- Bugs found along the way: `apply_volume()` was dead code (a no-op), then later corrupted audio via an int16/int32 mismatch on the 16-bit A2DP path (fixed with `apply_volume_s16()`); WiFi needed `WIFI_PS_NONE` to sustain 128kbps streams; a ~12KB `stations_blob_t` was originally stack-allocated (boot crash) — moved to static; `esp_http_server` needed `max_uri_handlers=20` (silently dropped routes above 8); WROOM32 scan results were misrouted to USB console instead of back over `bt_link` (fixed with a broadcast response helper).
- CTRL-1 (orchestrated cold-boot-to-music) and a 30-min MP3+AAC endurance test both passed.

### Reliability hardening (2026-07-14 onward, RH-* task list)

Fixed radio session lifecycle races, a stack-pointer-in-queue anti-pattern in `bt_link_send()`, made the ring buffer atomic, made the I2S writer stoppable, propagated persistence errors, replaced an unsafe BT status queue with a mutex snapshot, added an audio-engine lifecycle state machine (STOPPED→STARTING→RUNNING→STOPPING→FAULTED) with timeout safety, added BT-init rollback, removed the AP password from `/api/status` (security fix), and used ASan to find/fix a use-after-free in `radio_stop_sync`. A related large "esp_i2s_source repair" pass (via `/ralph-loop`, ~80 tasks) fixed a duplicate-init boot crash, discovered I2S audio output had **never actually been wired in** (dead code) at one point, and fixed a real watchdog-panic/reboot-loop caused by `writer_task()` holding a spinlock across a blocking I2S write with no clock present. Phase 9 (2026-07-15) added stable station IDs, versioned NVS persistence (STN2 + CRC-32 with legacy migration), and SSRF blocking on radio station URLs.

---

## Archived projects

- **`archive/bbgw_i2s_source/`** (BeagleBone Green Wireless I2S source, 2026-02-06/07): full port of `rpi_i2s_source` to BBGW, ~30.2 hours across 7 phases (device-tree overlays for McASP0/UART4, code adaptation, testing, ~7900 lines of docs, performance tuning, deployment tooling). Declared **COMPLETE**; per current `CLAUDE.md` this has since been superseded by `esp_i2s_source`.
- **`archive/rpi_i2s_source/`**: pytest-based Flask web server on Raspberry Pi; built out integration (7 hardware tests) and performance (9 tests) frameworks, all hardware-gated (`--run-hardware`), auto-skip otherwise.

---

## Standing rules & conventions established across the log

- Never flash without explicit user confirmation for risky/destructive actions (an earlier era had blanket standing permission for `/dev/ttyUSB0` test-flashing; current `CLAUDE.md` overrides this — always confirm).
- Use conda env `python3.10` for Python tooling; never create new envs.
- No `Co-Authored-By:` trailer in commits (a commit-msg hook rejects it in some eras of this repo).
- A "CRITICAL: zero tests ran" result must always be treated as a failure requiring action, never ignored.
- Test-result reporting must give exact numeric counts (host/device/standalone), never vague "all passed."
- `main.c` owns UART-driver-install-once and single NVS init; components must not redo either.
- ESP-IDF v5.5.1 lives at `$HOME/esp/v5.5.1/esp-idf`; `. export.sh` before building.
- Project's core purpose (stated repeatedly): play audio to a Bluetooth sink from I2S/synth (and now UART-streamed) sources — WAV/SPIFFS playback was deliberately removed as a supported source in Feb 2026.
