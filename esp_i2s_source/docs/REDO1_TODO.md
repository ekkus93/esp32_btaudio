# REDO1 — esp_i2s_source from-scratch rewrite (ESP32-S3 N16R8)

Task list for the rewrite specified in [SPEC.md](SPEC.md). One TDD-style
commit per task group where practical (`feat(scope): REDO1 <ID> — ...`).

Prerequisites / environment (confirmed 2026-07-04):
- Board: AYWHP ESP32-S3-WROOM-1 **N16R8** (16 MB flash, 8 MB octal PSRAM).
- ESP-IDF v5.5.1 at `$HOME/esp/esp-idf` (same env as esp_bt_audio_source).
- Companion WROOM32 runs firmware ≥ v0.2.0-317 (UART2 command port live,
  I2S **master-RX** on BCLK=GPIO18/WS=GPIO19/DIN=GPIO22 — role inverted during
  SIG-1d bring-up; see SPEC §3.3).
- Wiring per SPEC.md §3.2; both boards on separate USB power, common GND.
- Python tooling via `conda run -n python310` (never create new envs).
- Web UI build (WEB-1 only) needs Node.js ≥ 20 + npm for the `web/` Vite
  project. The IDF/CI firmware build does NOT need Node — it embeds the
  committed gzipped bundle in `main/www/` (SPEC §5.5).
- The old `main/ws_echo_server.c` scaffold is replaced wholesale; old
  `docs/PRD.md`/`docs/FS.md` superseded by SPEC.md where they conflict.

---

## INFRA-1 — Project scaffold on esp32s3

**Status:** `[x]` DONE (2026-07-04) — hardware-verified on the S3.

### Tasks
- [x] **INFRA-1a** Re-init project: `idf.py set-target esp32s3`; removed the
      vendored ESP-IDF tree + ws_echo_server.c + pytest/sdkconfig.ci; minimal
      `app_main` boots, inits NVS, prints `DIAG|BOOT|READY`.
- [x] **INFRA-1b** Octal PSRAM enabled (`CONFIG_SPIRAM_MODE_OCT`); boot banner
      reports `psram_kb=8192` (8 MB) and ~8.7 MB free heap.
- [x] **INFRA-1c** Component skeletons `components/{i2s_out,signal_gen,
      bt_link,wifi_mgr,web_ui,radio}` register + build; `test/host_test`
      harness (CTest + Unity FetchContent + mocks/) with `test_sanity` green
      via `tools/run_host_tests.sh`.
- [x] **INFRA-1d** `idf.py build` green (219 KB app, 79% partition free);
      flashed + booted the app; console shows `DIAG|BOOT|READY|psram_kb=8192`.
      Chip: ESP32-S3 rev v0.2, 8 MB PSRAM, MAC 30:ed:a0:bd:44:e0.

### Hardware/flashing notes (USB-Serial-JTAG)
- Board enumerates as native USB-Serial-JTAG (`303a:1001`), console routed
  there (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Only `/dev/ttyACM*` present
  (no CP2102 UART port wired).
- **esptool's default RTS/DTR reset drops the S3 into DOWNLOAD mode** (DTR
  holds GPIO0 low) — the app won't run. Boot the app via
  `esptool --after watchdog_reset`. Encapsulated in `tools/s3_flash_run.sh`.
- The CDC port hops `/dev/ttyACM0` ↔ `/dev/ttyACM1` on re-enumeration after
  each reset — rescan `/dev/ttyACM*`, don't hardcode.
- `idf.py monitor` needs a real TTY (fails when piped) — use `script -qec` or
  the helper's pyserial capture.

## SIG-1 — Signal generator + I2S slave TX (first audio)

**Status:** `[x]` DONE — end-to-end I2S link verified via laptop A2DP capture
(440.00 Hz, 100% purity, both channels, 0% dropouts; commit `5a5f91e4`). Roles
were **inverted** from the original design: WROOM32 = master RX, S3 = slave TX.

### Background
Prove the I2S link with zero external dependencies. Slot format MUST match
the WROOM32 master exactly (SPEC §3.3: Philips, 32-bit slots, ws_width 32,
stereo, 44.1 kHz, MCLK unused; WROOM32 data width 32, S3 data width 16 placed
in the top half of the slot with a per-block phase-detect receiver). Pins
(final): S3 BCLK=GPIO15(in), WS=GPIO16(in), DOUT=GPIO7(out); WROOM32
BCLK=GPIO18(out), WS=GPIO19(out), DIN=GPIO22(in).

### Tasks
- [x] **SIG-1a** `signal_gen`: sine/sweep/silence producers, phase-continuous,
      7 host tests (commit 31917181).
- [x] **SIG-1b** `i2s_out`: lock-free SPSC `pcm_ring` (8 host tests) + pure
      `i2s_out_pump_once` (5 host tests, mock sink) + I2S std channel
      (16-in-32 slots per §3.3) + writer task (commits 1452d93a, 756f4e4f).
      Role later flipped to **slave-TX** during SIG-1d bring-up (commit
      `5a5f91e4`).
- [x] **SIG-1c** On-hardware smoke: S3 side PASS (commit a71eadc9); WROOM32
      confirmed `SOURCE=I2S` + `I2S_BYTES` growing after `START` during the
      SIG-1d bring-up.
- [x] **SIG-1d** Listen/verify test: **PASS** — laptop-as-A2DP-sink FFT capture
      confirms 440.00 Hz, 100.0% purity, both channels, peak bit-faithful, 0%
      dropouts (commit `5a5f91e4`). Five stacked root causes fixed: classic WS
      framing (bit_depth 32), stale NVS pins, per-session payload phase
      (phase-detect receiver), convert/resample mangling (direct copy), engine
      silence-stuffing chop (`audio_engine_hold_for_live_i2s`). Host tests added
      to lock the fixes: `test_i2s_frame_extract` (16) + audio_util identity &
      hold-policy probes (7).

## LINK-1 — UART command client (bt_link)

**Status:** `[x]` DONE — LINK-1a/1b/1c complete; hardware-validated dual-UART.

### Background
C implementation of the command-client behavior proven by
`esp32_serial.py`: send `CMD\r\n`, await the single `OK|`/`ERR|` terminal
line, queue `EVENT|` lines for subscribers. S3 UART1 (TX=17/RX=18) ↔
WROOM32 UART2. Also the first physical exercise of the WROOM32's UART2.

### Tasks
- [x] **LINK-1a** Line/response parser (pure, host-tested, commit 4fa649f3):
      splits `STATUS|COMMAND|RESULT|DATA` (DATA keeps embedded `|`), classifies
      OK/ERR/INFO/EVENT + terminal helper, line assembler tolerates partial
      reads / CRLF / empty lines / overflow recovery. 13 host cases.
      (Response↔command correlation moves to LINK-1b's session state machine.)
- [x] **LINK-1b** Session state machine (pure, 11 host tests, commit 04c4e4b8):
      one-in-flight command, verb correlation, terminal completion, timeout
      tick, EVENT fan-out to N subscribers incl. interleaving. Device UART1
      task w/ synchronous `bt_link_send()` (commit 646f4fcc, builds; runtime
      verified at LINK-1c).
- [x] **LINK-1c** Hardware validation **PASS** (S3 UART1 GPIO17/18 ↔ WROOM32
      UART2 GPIO16/17): a boot-time `link_selftest()` in `main.c` sent
      `VERSION`/`STATUS`/`VOLUME 40` over the real wires — **3/3 OK**, VERSION
      returned the live WROOM32 build string. Dual-UART contract confirmed: the
      WROOM32 USB console (UART0) stayed fully usable throughout, and
      `STATUS` read back `VOL=40` — proving the S3's UART2 command took effect
      and was observable on the independent console port. (M3)
      Two findings recorded:
      - **Wiring trap:** the jumpers must be on WROOM32 **GPIO16/17** (UART2),
        NOT GPIO1/3 (silk-labeled TX/RX = the UART0 console). A misplaced
        jumper let the self-test "pass" over UART0 while the S3 driving that
        line hung the whole WROOM32 (console + UART2 + BT all dead together).
        A raw wire-probe (WROOM32 logs on UART0 only, never UART2) is the
        definitive discriminator.
      - **First-command race fix (bt_link.c):** `uart_set_pin` glitches the TX
        line, leaving a partial garbage line in the peer's assembler that
        swallowed the first command. `bt_link_init` now flushes + sends a lone
        CRLF after pin setup → clean 3/3.

## WIFI-1 — WiFi manager + provisioning

**Status:** `[x]` DONE — a/b/c hardware-validated (STA join + mDNS + AP + console).

### Tasks
- [x] **WIFI-1a** Pure `wifi_sm` STA/AP fallback state machine (creds → STA with
      bounded retries → AP fallback; set/clear creds transitions). Host-tested,
      12 Unity cases incl. the disconnect-in-AP regression. No ESP-IDF deps.
- [x] **WIFI-1b** `wifi_mgr` device glue: esp_wifi/esp_netif/esp_event, NVS creds
      (namespace `wifi`), AP provisioning (`ESP32-S3-Audio`, WPA2, **MAC-derived**
      password `audio-XXXXXX` printed on console), STA connect, and **mDNS
      `esp-i2s-source.local`**. Hardware-verified: boot→AP (laptop sees SSID);
      provision→STA got IP 10.1.2.52; `esp-i2s-source.local` resolves + pings;
      creds persist across reboot (auto-STA). Regression found+fixed on hardware:
      a stray STA disconnect after WIFI RESET no longer bounces AP→STA.
- [x] **WIFI-1c** S3 console (`cmd_console`, USB-Serial-JTAG): `WIFI <ssid> [pass]`,
      `WIFI STATUS`, `WIFI RESET`, `STATUS`. All verbs verified over the wire.
      (Component named `cmd_console`, not `console`, to avoid shadowing ESP-IDF's
      built-in `console` component that `mdns` depends on.)

## WEB-1 — Web server + UI shell + terminal

**Status:** `[ ]` Not started

### Background
esp_http_server serving a **TypeScript + React** SPA (Vite build → minified +
gzipped bundle, embedded via EMBED_FILES — **no filesystem partition**; SPEC
§5.5). One WebSocket multiplexes terminal I/O, the EVENT feed, and status
pushes as JSON `{type:...}` frames (SPEC §5.2).

### Tasks
- [x] **WEB-1z** Frontend toolchain: standalone `web/` (Vite + TS + React 18,
      own `package.json`). `npm run build` = `tsc --noEmit && vite build`
      (single-file via `vite-plugin-singlefile`) → `scripts/embed_web.mjs`
      gzips → `main/www/index.html.gz` (+`.sha256`). **46.5 KB gzip** (< 200 KB
      target). Committed `.gz` so idf.py/CI need no Node. Embedded via
      `target_add_binary_data(${COMPONENT_LIB} ... BINARY)` **in the web_ui
      component** (not main — cross-component link order needs the _binary_*
      symbols in the same lib that references them). Shell renders Network +
      System cards from /api/status; Terminal/Tone/Radio/BT are stubs.
- [x] **WEB-1a** httpd (`web_ui.c`) + `GET /` (gzipped SPA, `Content-Encoding:
      gzip`) + `GET /api/status` (cJSON: device/version/uptime/heap + wifi
      snapshot + **live WROOM32 version via bt_link**). Hardware-verified over
      the LAN: `curl http://10.1.2.52/` returns the 47639 B gzip SPA; status
      JSON aggregates S3 + `wroom:{reachable,version}`. Added
      `wifi_mgr_get_info()` structured accessor.
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
  - [ ] **RADIO-1c-i** User-entered stations: UI "Add station" (name + URL)
        and "Save station" on a one-off custom-URL audition both persist to
        NVS so they survive reboot and replay from the list. URL validation
        (scheme http/https, length cap); name defaults to host/ICY name if
        blank. Store as a versioned NVS blob (or per-slot keys), capacity
        **≥ 32 stations**; host-test add/edit/delete/persist/dedupe logic.

## RADIO-2 — Decode, resample, play

**Status:** `[ ]` Not started

### Tasks
- [ ] **RADIO-2a** Decoder task: add `espressif/esp_audio_codec ^2.6.0`
      managed component (plain IDF, **no ESP-ADF**); decode via
      `esp_audio_dec.h` + simple-decoder frame-finder — MP3 + AAC-LC +
      HE-AAC + **HE-AACv2** with **AAC-Plus (SBR/PS) enabled** in component
      config (needed for AAC+ streams like Dance UK). Content-Type →
      codec selection; handles mid-stream format changes; error containment
      (bad frame → resync, not crash).
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
