# Changelog

All notable changes to this project are documented here, generated from the
project's annotated git tags (the release-notes convention this repo has used
since `v0.1.0`). For the exact commit and full original message behind any
entry: `git show <tag>`. Reverse-chronological order.

## [0.3.0] - 2026-07-27

Auth removal, HTTPS-hang fix, I2S static fix, coverage push, docs overhaul.

Firmware version strings match this tag: `esp_bt_audio_source`'s
`CONFIG_APP_PROJECT_VER` is derived automatically from `git describe` (reads
`v0.3.0` exactly at this commit); `esp_i2s_source`'s `PROJECT_VER` and the web
UI's `package.json`/`package-lock.json` were bumped to `0.3.0` explicitly.

**esp_i2s_source web UI**
- Device-token/bearer-token HTTP auth removed entirely (frontend + backend),
  by explicit user decision — the web server now has no authentication.
- Root-caused and fixed the web UI hanging while an HTTPS radio station
  streamed: mbedTLS pinned ~16 KB of internal RAM per HTTPS connection,
  starving WiFi's dynamic TX buffers under concurrent load. Fixed via
  `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`.
- New Bluetooth tab (between Terminal and Settings); station list
  export/import with merge+dedup; internal/DMA heap telemetry in
  `/api/status`; stations addressed by stable id instead of array index;
  specific station add/edit failure reasons; radio auto-recovers from a
  faulted session; max WiFi TX power on STA connect.
- `wifi_mgr.c` and `radio.c` split (NVS persistence extracted to
  `wifi_mgr_nvs.c` / `radio_prebuffer.c`) to stay under the ~800-line policy.
- TLS crash fixed (radio task stacks sized for TLS; `https://` no longer
  crashes the device).

**esp_bt_audio_source**
- Root-caused and fixed intermittent Bluetooth audio static: the S3↔WROOM32
  I2S link's payload-phase detector re-decided every audio block with no
  memory, thrashing between valid/invalid locks (113 re-locks in 31 s
  measured). Fixed with phase-lock hysteresis (`i2s_frame_phase_hold()`).
- Fixed `SCAN` failing with an invalid inquiry length; unblocked
  `bt_stack_stub` so all 3 device Unity suites build and pass.
- Recovered `tools/stream_audio_uart.py` (the UARTAUDIO host streamer),
  accidentally truncated to a 136-byte stub by an earlier docs commit.
- Removed 2 dead-code stubs left over from earlier feature removals (WAV
  playback, legacy tag queue) with no real callers anywhere.

**Test coverage** (`docs/UNIT_TESTS2_TODO.md`, full Ralph loop, all P0/P1/P2)
- `esp_bt_audio_source`: 79.4% → 84.2% line coverage (946 host cases, was 891).
- `esp_i2s_source`: 55.1% → 61.7% line coverage (27 suites, was 25).
- Closed a real security-relevant gap: `radio_stream.c`'s SSRF/redirect-policy
  re-check had zero test evidence it worked; now has 33 cases including a
  confirmed-passing assertion that a redirect to a private IP is rejected.
- Found and fixed 3 pre-existing missing header declarations and one
  masked-lock-failure bug in test infrastructure along the way.

**Docs**
- Root `README.md` overhauled: dropped the archived RPi/BeagleBone mention
  and the stale "Project Status" section; added a top-level "Connecting the
  ESP32-S3 to the ESP32-WROOM32" section with the real I2S/UART wiring table
  and photos, plus an explicit warning against bridging the two boards'
  3.3V rails while both are on USB power.
- `docs/ACTIVE_TODOS_SPEC.md` + `docs/ACTIVE_TODOS_TODO.md` added: design and
  task checklist for the two remaining open items (longer-duration UARTAUDIO
  throughput-regression pytest; physical UART2 verification, blocked on a
  second USB-serial adapter).
- Both projects' READMEs and `SPEC.md` brought current; several
  stale/inaccurate claims fixed (host test counts, API route tables,
  audio-source priority order, I2S master/slave roles).

Both `idf.py build`s clean; full host suites green at every step.

## [0.2.0] - 2026-07-22

`esp_i2s_source` safety/integrity hardening across 12 phases:
- Wi-Fi manager credential validation and lifecycle.
- I2S output lifecycle, PSRAM-required ring, DMA-starvation fix.
- `bt_link` worker join/cancellation safety.
- Station persistence CRC-32 validation + non-destructive recovery.
- Stream URL/SSRF policy (literals, DNS, redirects, reconnects).
- Radio session lifecycle, reconnect backoff, decoder hardening.
- `ctrl` config synchronization, truthful scan/resume.
- Degraded-boot capability boundaries (`runtime_capabilities`).
- Frontend authenticated mutation flow.

Two production bugs found and fixed during final verification:
- I2S write-timeout unit double-conversion starving the DMA (audible
  static/choppiness over Bluetooth A2DP).
- Corrupt stations NVS blob with no recovery path short of full erase.

2-hour endurance test exceeded (3.84 h continuous, 0 reconnects/decode
errors, flat heap). Full host suite (strict+ASan+UBSan+npm) and a clean
ESP-IDF build pass. Full implementation summary:
`esp_i2s_source/docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SUMMARY_2026-07-22.md`.

### Milestone: v0.1.0-pre-hardening (2026-07-13)

Docs checkpoint (CLAUDE.md files, coverage badge, archive references updated)
taken just before the safety/integrity hardening pass that became v0.2.0.

### Milestone: v0.2.0-mainc-stable (2026-02-01)

`main.c` architecture cleanup and productization pass (CODE_REVIEW2),
tagged separately from the v0.2.0 line above:
- Audio autostart configuration (NVS + `AUDIO_AUTOSTART` command); 3-level
  configuration system (NVS → Kconfig → fallback); 4 new Kconfig
  compile-time defaults (sample rate, volume, bit depth, autostart).
- Fixed invalid preprocessor guards (P0), NVS ownership ambiguity (P1),
  UART driver lifecycle (P1), init order contradictions (P1), portability
  issues (P3).
- Zero compiler/clang-tidy warnings; 385/385 tests passing (190 host + 195
  device); ~1,877 lines of documentation added; CI layering-check tooling.
- Backward compatible; 13 items of technical debt eliminated, 0 introduced.

## [0.1.0] - 2025-02-01

Added `CONNECT`, `DISCONNECT`, and `UNPAIR` commands.
