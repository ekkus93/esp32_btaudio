# REDO1 — esp_i2s_source from-scratch rewrite (ESP32-S3 N16R8)

Task list for the rewrite specified in [SPEC.md](SPEC.md). One TDD-style
commit per task group where practical (`feat(scope): REDO1 <ID> — ...`).

Prerequisites / environment (confirmed 2026-07-04):
- Board: AYWHP ESP32-S3-WROOM-1 **N16R8** (16 MB flash, 8 MB octal PSRAM).
- ESP-IDF v5.5.1 at `$HOME/esp/esp-idf` (same env as esp_bt_audio_source).
- Companion WROOM32 runs firmware ≥ v0.2.0-317 (UART2 command port live,
  I2S slave-RX on BCLK=26/WS=25/DIN=22).
- Wiring per SPEC.md §3.2; both boards on separate USB power, common GND.
- Python tooling via `conda run -n python310` (never create new envs).
- The old `main/ws_echo_server.c` scaffold is replaced wholesale; old
  `docs/PRD.md`/`docs/FS.md` superseded by SPEC.md where they conflict.

---

## INFRA-1 — Project scaffold on esp32s3

**Status:** `[ ]` Not started

### Tasks
- [ ] **INFRA-1a** Re-init project: `idf.py set-target esp32s3`; remove
      ws_echo_server.c + its pytest/sdkconfig.ci; minimal `app_main` that
      boots, inits NVS, prints `DIAG|BOOT|READY`.
- [ ] **INFRA-1b** Enable octal PSRAM (`CONFIG_SPIRAM`, octal mode) and
      verify 8 MB visible in the boot heap report.
- [ ] **INFRA-1c** Component skeleton: `components/{i2s_out,signal_gen,
      bt_link,wifi_mgr,web_ui,radio}` registered and building empty;
      `test/host_test` harness cloned from the esp_bt_audio_source pattern
      (CTest + Unity FetchContent + mocks/).
- [ ] **INFRA-1d** `idf.py build` green; flash the S3 (confirm before
      flashing) and verify boot banner + PSRAM size on its USB console.

## SIG-1 — Signal generator + I2S master TX (first audio)

**Status:** `[ ]` Not started

### Background
Prove the I2S link with zero external dependencies. Slot format MUST match
the WROOM32 slave exactly (SPEC §3.3: Philips, 16-bit data in 32-bit slots,
stereo, 44.1 kHz, MCLK unused). Pins: BCLK=GPIO5, WS=GPIO6, DOUT=GPIO7.

### Tasks
- [ ] **SIG-1a** `signal_gen`: 44.1 kHz stereo s16 sine/sweep/silence
      producers, pure functions, host-tested (exact sample math).
- [ ] **SIG-1b** `i2s_out`: SPSC PCM ring (port `audio_ringbuffer.c`
      pattern; PSRAM-backed, ≥256 KB) + I2S master-TX channel per §3.3;
      writer task zero-fills + counts underruns; start/stop/stats API
      host-tested with an I2S mock.
- [ ] **SIG-1c** On-hardware smoke: 440 Hz tone out; WROOM32 (driven
      manually over USB) shows `SOURCE=I2S`, `I2S_BYTES` growing after
      `START`.
- [ ] **SIG-1d** Listen test: tone audible in earbuds; WROOM32
      `READ_BPS`≈176400; no underrun growth either side. (M2)

## LINK-1 — UART command client (bt_link)

**Status:** `[ ]` Not started

### Background
C implementation of the command-client behavior proven by
`esp32_serial.py`: send `CMD\r\n`, await the single `OK|`/`ERR|` terminal
line, queue `EVENT|` lines for subscribers. S3 UART1 (TX=17/RX=18) ↔
WROOM32 UART2. Also the first physical exercise of the WROOM32's UART2.

### Tasks
- [ ] **LINK-1a** Line/response parser (pure, host-tested): splits
      `STATUS|COMMAND|RESULT|DATA`, classifies OK/ERR/INFO/EVENT,
      correlates responses to the pending command, tolerates interleaved
      events and partial reads.
- [ ] **LINK-1b** `bt_link` task: UART1 driver, one-in-flight command with
      timeout/retry, EVENT fan-out (multiple subscribers); host-tested
      against a scripted UART mock.
- [ ] **LINK-1c** Hardware validation: `VERSION`/`STATUS`/`VOLUME 40` over
      the real wires; WROOM32 USB console verified still fully usable in
      parallel (dual-UART contract). Record results here. (M3)

## WIFI-1 — WiFi manager + provisioning

**Status:** `[ ]` Not started

### Tasks
- [ ] **WIFI-1a** `wifi_mgr` state machine (host-tested logic where pure):
      creds in NVS → STA connect w/ retries → fallback to AP mode
      (`ESP32-S3-Audio`, WPA2, default password printed on console) when
      no creds or repeated failure.
- [ ] **WIFI-1b** mDNS `esp-i2s-source.local` in STA mode.
- [ ] **WIFI-1c** S3 console fallback commands: `WIFI <ssid> <pass>`,
      `WIFI STATUS`, `WIFI RESET` (clear creds → AP mode).

## WEB-1 — Web server + UI shell + terminal

**Status:** `[ ]` Not started

### Background
esp_http_server with an embedded single-page UI (EMBED_FILES — no
filesystem partition). One WebSocket multiplexes terminal I/O, the EVENT
feed, and status pushes as JSON `{type:...}` frames (SPEC §5.2).

### Tasks
- [ ] **WEB-1a** httpd + `GET /` embedded UI + `GET /api/status`
      (aggregated S3 + last-known WROOM32 state).
- [ ] **WEB-1b** `POST /api/wifi` provisioning endpoint + AP-mode flow:
      connect to S3 AP → open UI → enter home WiFi → S3 switches to STA;
      UI shows the new address. End-to-end provisioning test. (M4 gate)
- [ ] **WEB-1c** WebSocket: terminal pane (raw command → bt_link →
      response echo) + live scrolling EVENT feed. Both share `/ws`.
- [ ] **WEB-1d** Tone controls in the UI (`/api/tone`): frequency select,
      on/off — first browser-driven audio.

## BTUI-1 — Bluetooth management UI

**Status:** `[ ]` Not started

### Tasks
- [ ] **BTUI-1a** Scan: button → `SCAN` via bt_link → parse
      `INFO|SCAN|ITEM|` results → device list in UI.
- [ ] **BTUI-1b** Pair/connect/disconnect buttons per device; volume
      slider (`VOLUME <n>`); paired-devices list (`PAIRED`) with unpair.
- [ ] **BTUI-1c** Pairing prompts: `EVENT|PAIR|CONFIRM`/`PIN_REQUEST`
      surfaced as UI dialogs → `CONFIRM_PIN`/`ENTER_PIN` replies.
- [ ] **BTUI-1d** Hardware E2E with the Echo Buds: full pair→connect→
      volume flow from the browser only. (M5)

## RADIO-1 — Stream client + parsers

**Status:** `[ ]` Not started

### Background
HTTP/HTTPS stream fetch with playlist resolution and ICY metadata.
Compressed-frame ring in PSRAM decouples network jitter from decode.

### Tasks
- [ ] **RADIO-1a** Playlist resolution (`.m3u`/`.pls` → stream URL) and
      ICY metadata block parser — pure functions, host-tested.
- [ ] **RADIO-1b** Stream task: esp_http_client (+esp-tls), redirects,
      `Icy-MetaData:1`, content-type → codec selection, PSRAM ring fill,
      reconnect with backoff; telemetry (buffer level, drops, reconnects).
- [ ] **RADIO-1c** Station store: NVS-backed preset CRUD (host-tested
      logic) + `/api/stations` endpoints + UI list/add/edit/delete +
      custom-URL field. Seed defaults from internet-radio.com's Popular
      Stations list (SPEC §5.4 — 5 `.pls` presets); all web-editable.

## RADIO-2 — Decode, resample, play

**Status:** `[ ]` Not started

### Tasks
- [ ] **RADIO-2a** Decoder task: `esp_audio_codec` MP3 + AAC-LC + HE-AAC;
      handles mid-stream format changes; error containment (bad frame →
      resync, not crash).
- [ ] **RADIO-2b** Resampler stage to 44.1 kHz stereo s16 (decoder output
      may be 22.05/24/32/44.1/48 kHz, mono or stereo) — math host-tested
      against known-exact conversions.
- [ ] **RADIO-2c** Source arbitration: radio ⇄ tone (explicit user action
      wins); silence when idle; `/api/radio` play/stop; ICY title pushed
      to UI over WS.
- [ ] **RADIO-2d** Hardware E2E: MP3 station and AAC station each play to
      earbuds ≥30 min without dropout; WROOM32 counters clean; document
      buffer telemetry. MP3 station from the SPEC §5.4 seed list; AAC test
      via Dance UK Radio (AAC+, `uk2.internet-radio.com:8024`) and Hirschmilch
      Electronic (§5.4 — verify its codec/port on hardware). (M6)

## CTRL-1 — Orchestrated boot + polish

**Status:** `[ ]` Not started

### Tasks
- [ ] **CTRL-1a** NVS: target sink MAC, autostart flag, last source/station.
- [ ] **CTRL-1b** Orchestrator: boot → WiFi up → bt_link STATUS →
      CONNECT <mac> → START → resume last source (radio station or idle);
      EVENT-driven reconnect. Host-tested with scripted bt_link mock.
- [ ] **CTRL-1c** Power-on-to-music with zero human interaction. (M7 gate)

## DOC-1 — Documentation + regression

**Status:** `[ ]` Not started

### Tasks
- [ ] **DOC-1a** esp_i2s_source README rewritten (supersede README_orig.md);
      root README wiring/system diagrams updated to SPEC §3.2 (they still
      show the old GPIO21 plan).
- [ ] **DOC-1b** Host tests wired into a `run_all_tests`-style entry;
      counts recorded here.
- [ ] **DOC-1c** SPEC.md changelog with contract deviations found on
      hardware.

---

## Implementation order

INFRA-1 → SIG-1 (M2 listen test!) → LINK-1 (M3, validates UART2) →
WIFI-1 → WEB-1 (M4) → BTUI-1 (M5) → RADIO-1 → RADIO-2 (M6) → CTRL-1 (M7)
→ DOC-1.

Rationale: audio path first with the fewest moving parts, then the control
link, then the network/UI stack that everything user-facing hangs off,
then radio (the deepest dependency chain: WiFi + web + audio + control all
in place), orchestration and docs last. A PC→WiFi raw-PCM streaming mode
is parked as optional future work (see SPEC §8 note).
