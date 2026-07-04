# REDO1 — esp_i2s_source from-scratch rewrite (ESP32-S3)

Task list for the rewrite specified in [SPEC.md](SPEC.md). One TDD-style
commit per task group where practical (`feat(scope): REDO1 <ID> — ...`).

Prerequisites / environment (confirmed 2026-07-04):
- ESP-IDF v5.5.1 at `$HOME/esp/esp-idf` (same env as esp_bt_audio_source).
- Companion WROOM32 runs firmware ≥ v0.2.0-317 (UART2 command port live,
  I2S slave-RX on BCLK=26/WS=25/DIN=22).
- Wiring per SPEC.md §2.2; both boards on separate USB power, common GND.
- Python tooling via `conda run -n python310` (never create new envs).
- The old `main/ws_echo_server.c` scaffold is replaced wholesale; the old
  `docs/PRD.md`/`docs/FS.md` are superseded by SPEC.md where they conflict.

---

## INFRA-1 — Project scaffold on esp32s3

**Status:** `[ ]` Not started

### Background
Current tree is the unmodified IDF ws_echo_server example targeting plain
esp32. Start clean: new main, target esp32s3, component layout mirroring
esp_bt_audio_source conventions (components/, test/host_test/, tools/).

### Tasks
- [ ] **INFRA-1a** Re-init project: `idf.py set-target esp32s3`, remove
      ws_echo_server.c + its pytest/sdkconfig.ci; minimal `app_main` that
      boots, inits NVS, prints a `DIAG|BOOT|READY` marker.
- [ ] **INFRA-1b** Component skeleton: `components/{i2s_out,signal_gen,bt_link}`
      registered and building empty; `test/host_test` CMake harness cloned
      from esp_bt_audio_source pattern (CTest + Unity FetchContent + mocks/).
- [ ] **INFRA-1c** `idf.py build` green; flash to the S3 and confirm boot
      banner on its USB console (confirm before flashing).

Acceptance: clean build from a fresh checkout; boot marker visible.

## SIG-1 — Signal generator + I2S master TX (Phase-1 audio source)

**Status:** `[ ]` Not started

### Background
Prove the I2S link with zero external dependencies. Slot format MUST match
the WROOM32 slave exactly (SPEC.md §2.3: Philips, 16-bit data in 32-bit
slots, stereo, 44.1 kHz, MCLK unused). Pins: BCLK=GPIO5, WS=GPIO6, DOUT=GPIO7.

### Tasks
- [ ] **SIG-1a** `signal_gen`: 44.1 kHz stereo s16 sine/sweep/silence
      producers, pure functions, host-tested (sample math asserted exactly —
      reuse the tone-generation approach validated in the UARTAUDIO test
      tooling).
- [ ] **SIG-1b** `i2s_out`: SPSC PCM ring (port `audio_ringbuffer.c` pattern)
      + I2S master-TX channel at the §2.3 contract; writer task pulls from
      ring, zero-fills + counts underruns; start/stop/stats API host-tested
      with an I2S mock.
- [ ] **SIG-1c** On-hardware smoke: S3 outputs a 440 Hz tone continuously;
      scope-free check = WROOM32 `AUDIO_STATUS` shows `SOURCE=I2S` and
      `I2S_BYTES` growing after `START` (drive the WROOM32 manually over USB
      for this task).
- [ ] **SIG-1d** Listen test: tone audible in earbuds via the full chain;
      WROOM32 `READ_BPS`≈176400 and no underrun growth on either side.

Acceptance: M2 definition of done in SPEC.md §6.

## LINK-1 — UART command client (bt_link)

**Status:** `[ ]` Not started

### Background
C implementation of the command-client behavior proven by
`esp32_serial.py`: send `CMD\r\n`, await the single `OK|`/`ERR|` terminal
line for that command, queue `EVENT|` lines separately. S3 UART1
(TX=GPIO17/RX=GPIO18) ↔ WROOM32 UART2. This task is ALSO the first physical
exercise of the WROOM32's UART2 port.

### Tasks
- [ ] **LINK-1a** Response/event line parser (pure, host-tested): splits
      `STATUS|COMMAND|RESULT|DATA`, classifies OK/ERR/INFO/EVENT, correlates
      responses to the pending command token, tolerates interleaved EVENT
      lines and partial reads.
- [ ] **LINK-1b** `bt_link` task: UART1 driver, TX queue with
      one-in-flight command + timeout/retry policy, EVENT callback
      registration; host-tested against a scripted UART mock.
- [ ] **LINK-1c** Hardware validation: S3 sends `VERSION`, `STATUS`,
      `VOLUME 40` to the WROOM32 over the real wires; responses parsed;
      confirm the WROOM32 USB console stays fully usable in parallel
      (dual-UART contract) — record results in this file.

Acceptance: LINK-1c green = M3 + physical UART2 verification done.

## CTRL-1 — Orchestrated boot (S3 as system brain)

**Status:** `[ ]` Not started

### Tasks
- [ ] **CTRL-1a** NVS config: target sink MAC, autostart flag, default tone.
- [ ] **CTRL-1b** Orchestrator state machine (host-tested with scripted
      bt_link mock): BOOT → QUERY (STATUS) → CONNECT <mac> → START → RUNNING;
      EVENT-driven reconnect (single retry then backoff, mirroring the
      WROOM32's own policy); error states surfaced on the S3 console.
- [ ] **CTRL-1c** S3 console commands (USB serial): `TONE <hz>|OFF`,
      `BT <raw command>` passthrough, `STATUS` (S3 + last known WROOM32 state).
- [ ] **CTRL-1d** Hardware E2E: power both boards → earbuds auto-connect →
      tone plays with zero human interaction (M4).

## NET-1 — WiFi bring-up

**Status:** `[ ]` Not started

### Tasks
- [ ] **NET-1a** STA mode with NVS-stored credentials; console command
      `WIFI <ssid> <pass>` to provision; connection state on console.
- [ ] **NET-1b** mDNS advertisement (`esp-i2s-source.local`) so the PC tool
      finds the box without hardcoded IPs.

## NET-2 — Network PCM streaming (Phase-2 audio source)

**Status:** `[ ]` Not started

### Background
TCP server (port 5005) receiving length-framed 44.1 kHz s16le stereo PCM
into the same `pcm_ring` the signal generator uses; fill-level feedback for
sender pacing (protocol per SPEC.md §4.2 — UARTAUDIO's proven design minus
CRC, since TCP is reliable).

### Tasks
- [ ] **NET-2a** Frame parser + source arbitration (net vs signal_gen —
      network wins while a client is connected), host-tested.
- [ ] **NET-2b** TCP server task with backpressure + feedback lines;
      graceful client disconnect → fall back to silence/tone.
- [ ] **NET-2c** PC sender `tools/stream_audio_net.py` (sibling of
      esp_bt_audio_source's `stream_audio_uart.py`: same WAV/stdin input,
      deadline pacing, feedback trim; TCP transport; ffmpeg hint at 44100).
- [ ] **NET-2d** Hardware E2E: PC WAV → WiFi → S3 → I2S → WROOM32 → earbuds,
      ≥3 min clean; counters zero on both ends (M5). Fidelity spot-check via
      the BT project's `compare_bt_capture.py` method (laptop as sink) with
      a 44.1 kHz reference (no upsampler step in the reference chain).

## DOC-1 — Documentation + regression

**Status:** `[ ]` Not started

### Tasks
- [ ] **DOC-1a** esp_i2s_source README rewritten for the real firmware
      (supersede README_orig.md); root README wiring/system diagrams updated
      to the SPEC.md §2.2 map (they currently show the old GPIO21 plan).
- [ ] **DOC-1b** Host tests wired into a `run_all_tests`-style entry point;
      counts recorded here.
- [ ] **DOC-1c** SPEC.md addendum/changelog with any contract deviations
      discovered on hardware.

---

## Implementation order

INFRA-1 → SIG-1 (M2 listen test!) → LINK-1 → CTRL-1 → NET-1 → NET-2 → DOC-1.

Rationale: SIG-1 before LINK-1 gets audio through the new wiring with the
fewest moving parts (WROOM32 driven manually over USB); LINK-1 then removes
the human from the loop; network audio lands last on an already-proven
I2S + control foundation.
