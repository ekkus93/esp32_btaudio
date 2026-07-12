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
- [~] **WEB-1b** `POST /api/wifi {ssid,pass}` (web_ui.c): validate → reply
      `{ok,host}` → **deferred** apply (provision_task, ~400 ms) so the response
      flushes before AP tears down. Frontend `ProvisionForm` shows in AP mode.
      Backend hardware-verified over STA: bad creds → 400; valid → 200 + clean
      reconnect to CONNECTED. Fixed a re-provision bug (esp_wifi_start()'s return
      isn't reliably NOT_STOPPED; now track s_wifi_started + force
      disconnect→reconnect when re-applying creds while holding an IP).
      **Pending: full browser AP→STA walk-through (M4 gate).**
- [x] **WEB-1c** WebSocket `/ws` (web_ui.c) multiplexing terminal + EVENT feed.
      `term_in` → `bt_link_send()` → `term_out` (sync in the WS handler); a
      `bt_link_subscribe()` fans WROOM32 `EVENT|` lines out to all clients as
      async `event` frames via `httpd_queue_work` + `httpd_ws_send_frame_async`
      (client fds tracked, lazily pruned on send failure). Frontend `Terminal`
      component: live WS, command input, scrolling colored log (sent/out/event),
      auto-reconnect. Hardware-verified: `VERSION`/`STATUS`/`VOLUME` round-trip
      as term_out; `DEBUG MOCK_PAIR` yielded both term_out AND a pushed
      `EVENT|PAIR|CONFIRM` frame.
- [x] **WEB-1d** Tone controls. Extracted the hardcoded always-on `tone_task`
      into a controllable `tone` component (on/off + frequency, atomic state,
      emits silence when off so the slave-TX stream keeps flowing). `POST
      /api/tone {hz}` / `DELETE /api/tone`; tone state added to /api/status;
      frontend Tone panel (freq input + presets + play/stop). **First
      browser-driven audio, objectively verified** via A2DP FFT: POST 1000→
      1000.00 Hz, 220→220.00 Hz, DELETE→silence, 440→440.00 Hz. First step of
      retiring the always-on tone (full arbitration at RADIO-2c).

## BTUI-1 — Bluetooth management UI

**Status:** `[ ]` Not started

Foundation: extended `bt_link_session` to fan out **INFO** lines (scan results,
paired items) to subscribers too, not just EVENT (+host test). web_ui pushes
them as `info` WS frames (EVENT → `event`). A shared `web/src/ws.ts` singleton
multiplexes one `/ws` across the Terminal + Bluetooth panels.

### Tasks
- [x] **BTUI-1a** Scan: button → `SCAN` → `INFO|SCAN|RESULT|<mac>,<name>` info
      frames accumulate into a discovered-device list. Verified over WS (`OK
      STARTED`; info-frame path proven via PAIRED's identical fan-out — no
      devices discoverable in range at test time).
- [x] **BTUI-1b** Per-device Pair/Connect buttons; global Disconnect; volume
      slider (`VOLUME <n>`); paired list (`PAIRED` → `INFO|PAIRED|ITEM`) with
      Unpair (`UNPAIR <mac>`). Verified: 2 paired devices returned as info
      frames; commands round-trip as term_out.
- [x] **BTUI-1c** Pairing prompt: `EVENT|PAIR|CONFIRM` → modal → Accept/Reject
      send `CONFIRM_PIN ACCEPT|REJECT`. Verified via `DEBUG MOCK_PAIR` →
      `EVENT|PAIR|CONFIRM` frame surfaced.
- [~] **BTUI-1d** Hardware E2E, full pair→connect→volume browser-driven —
      validated with the **laptop as the A2DP sink** (proven LaptopBT
      auto-accept agent): UNPAIR → PAIR (`EVENT|PAIR|SUCCESS`) → CONNECT (A2DP
      up, confirmed via laptop BlueZ) → VOLUME (tracked as `VOL=45`; A2DP
      volume is AVRCP at the sink, correctly not in the source stream).
      Limitation of the substitute: `SCAN` doesn't discover a laptop (poor
      inquiry target) — real Echo Buds in pairing mode would appear. Also
      surfaced+fixed: `bt_link_send()` now returns the response DATA field, so
      the terminal shows full replies (STATUS VOL/underruns, VERSION build).
      (M5) — remaining: real Echo Buds run for the SCAN step + user sign-off.

## RADIO-1 — Stream client + parsers

**Status:** `[ ]` Not started

### Background
HTTP/HTTPS stream fetch with playlist resolution and ICY metadata.
Compressed-frame ring in PSRAM decouples network jitter from decode.

### Tasks
- [x] **RADIO-1a** Pure `radio_parse` (host-tested, 13 cases):
      `radio_playlist_first_url()` resolves `.pls` (`FileN=`), extended/plain
      `.m3u` (skips `#` comments), or a bare URL to the first http(s) stream
      URL; `radio_icy_stream_title()` pulls `StreamTitle` out of a SHOUTcast
      ICY metadata block (NUL-padding tolerant). No ESP-IDF deps.
- [x] **RADIO-1b** `radio.c` stream task: esp_http_client + esp-tls (cert
      bundle), playlist resolve (fetch .pls/.m3u → stream URL), `Icy-MetaData:1`,
      content-type → codec (mp3/aac), 256 KB PSRAM ring, ICY demux (pure,
      host-tested state machine), reconnect w/ backoff, telemetry. `/api/radio`
      POST/DELETE (deferred), radio state in /api/status, Radio UI panel
      (presets + URL + now-playing + buffer/telemetry). **Hardware-verified**
      (SomaFM): `.pls` resolved on-device → MP3 128k, icy-name + live StreamTitle
      demuxed from the real stream, bytes climbing, 0 reconnects. Needed a
      **custom 6 MB app partition** (`partitions.csv`) — TLS+http_client overflowed
      the default ~1 MB single-app. Notes: the SPEC §5.4 snapshot stations had
      unreachable stream ports here (laptop failed too) — presets swapped to
      SomaFM until RADIO-1c; audio plays once the RADIO-2 decoder drains the ring.
- [x] **RADIO-1c** Station store. Pure `station_store` CRUD (host-tested, 7
      cases): add/update/delete, http(s) URL validation, blank-name→host
      defaulting, exact-URL dedupe, capacity 40 (≥32). `stations.c` device
      wrapper: NVS blob persistence (magic-versioned) + first-boot seed + mutex.
      `/api/stations` GET/POST/PUT(?id)/DELETE(?id) with 400 on
      invalid/duplicate/full. Radio UI: station list (play/edit/delete) + add
      form + one-off "Play". **Hardware-verified**: seed→add→edit→delete over
      REST, and edits persist across reboot. Bumped httpd `max_uri_handlers`
      (12 routes now).
  - [x] **RADIO-1c-i** Add-station + edit + one-off Play all persist to NVS;
        URL validated (http/https), blank name → URL host; capacity ≥ 32.
        NVS seed uses SomaFM (the SPEC §5.4 snapshot stations were unreachable
        here — see RADIO-1b); the list is web-editable so the SPEC set / any
        station can be added. Persist/dedupe host-tested.

## RADIO-2 — Decode, resample, play

**Status:** `[ ]` Not started

### Tasks
- [x] **RADIO-2a** Decoder task (radio.c): `espressif/esp_audio_codec ^2.6.0`
      via `esp_audio_simple_dec` (frame-parser) — MP3 + AAC (`aac_plus_enable`
      for HE-AAC/AAC+). Pulls compressed from the network ring, decodes,
      resamples (RADIO-2b) → 128 KB decoded-PCM ring (`radio_pcm_read` for the
      I2S feeder). content-type→codec, format-change re-open, bad-frame resync
      (force-consume 1 byte). **Hardware-verified**: SomaFM MP3 decodes to
      44100/stereo, 0 decode errors, PCM ring fills. Gotcha: must call BOTH
      `esp_audio_dec_register_default()` (low-level codecs) AND
      `esp_audio_simple_dec_register_default()` (wrappers) — simple-dec only
      registers container parsers (WAV/M4A/TS/OGG); MP3/AAC need the low-level
      registration or open fails "Decoder MP3 not registered" (-7).
- [ ] **RADIO-2b** Resampler stage to 44.1 kHz stereo s16 (decoder output
      may be 22.05/24/32/44.1/48 kHz, mono or stereo) — math host-tested
      against known-exact conversions.
- [x] **RADIO-2c** Source arbitration. Single `audio_out` feeder in main:
      radio decoded PCM when playing (radio_pcm_read; underrun→brief silence),
      else tone (itself silence when off) → 16-in-32 pack → i2s_out. Retired
      the always-on tone_task; `tone` now exposes `tone_fill()` (no i2s dep).
      **Hardware-verified via A2DP FFT**: radio ON → broadband music (peak
      ~26k); radio+tone → radio wins; stop radio → 440 Hz tone; tone off →
      silence. `/api/radio` play/stop drives it; ICY title shows in the Radio
      UI via /api/status poll (WS push is an optional later nicety).
- [x] **RADIO-2d** Hardware E2E (M6) — **PASSED**. MP3 (SomaFM Groove Salad
      128k) and AAC+ (Dance UK Radio 32k, `uk2.internet-radio.com:8024`,
      `audio/aacp`, HE-AAC→44.1k via SBR) each streamed **30 min to the laptop
      A2DP sink with zero dropouts**: 0 rebuffers, 0 PCM starvation, 0 decode
      errors / reconnects / I2S underruns; PCM ring held 88–99% full
      throughout; 18/18 (MP3) and 19/19 (AAC) FFT windows audible; WROOM32
      sink counters clean after both (`UNDERRUNS=0, PKT_ERR=0`). Two fixes
      were required to get here (both found via this gate):
      • **WiFi modem power-save** (IDF default `WIFI_PS_MIN_MODEM`) throttled
        the stream to ~119 kbps w/ jitter → choppy 128k. Fixed with
        `esp_wifi_set_ps(WIFI_PS_NONE)`.
      • **Shallow PCM buffer** (0.74 s) had no jitter tolerance → deepened to
        ~5.9 s ring + prebuffer gate (`radio_audio_ready`, re-arm on drain).
      Telemetry made observable via new `i2s{}` + `radio.buffering` fields in
      `/api/status`. Note: the i2s underrun counter is blind to PCM-ring
      starvation (audio_out zero-fills a full block); the real dropout signal
      is `radio.buffering`/`pcm_used`. Hirschmilch not needed (Dance UK was a
      cleaner AAC+ target; hirschmilch.de:7000 reports `audio/mpeg`).

## CTRL-1 — Orchestrated boot + polish

**Status:** `[~]` Implementation done + host-tested; M7 hardware gate passed for
the sink-available path (see CTRL-1c caveat).

### Tasks
- [x] **CTRL-1a** NVS config store (`components/ctrl/ctrl_cfg`): target sink
      MAC, autostart flag, last station index (magic-guarded blob, namespace
      "ctrl"). Pure `ctrl_cfg_mac_valid()` host-tested (5 cases).
- [x] **CTRL-1b** Orchestrator. Pure `ctrl_sm` FSM (host-tested, 9 cases):
      WAIT_WIFI → STATUS → CONNECT `<mac>` → settle → START → resume last
      station → RUNNING with health-poll; failed START / dropped link (RUN=0)
      → backoff → retry CONNECT up to max_retries → GAVEUP. `ctrl.c` glue
      drives it via `bt_link_send` (STATUS/CONNECT/START) + `radio_play`,
      reading `RUN=1` as the connected/streaming signal. `/api/ctrl` GET/POST
      configures sink+autostart; POST `/api/radio {id}` records last station.
      **Reconnect note:** the WROOM32 emits no connect/disconnect EVENT (its
      callbacks are NULL), so reconnect is STATUS-polled, not push-EVENT — the
      FSM is source-agnostic. Pure-EVENT push is a WROOM32-side follow-up.
- [x] **CTRL-1c** Power-on-to-music (M7) — **PASSED on hardware (Echo Buds).**
      Disconnected the sink, cold-rebooted the S3 → orchestrator auto-connected
      the earbuds and resumed the last station (jazz) in **~6 s with zero human
      interaction**. Required an orchestrator fix (commit ee48824c): the old
      CONNECT→settle→START-or-backoff raced a slow sink (earbuds aren't linked
      at +4 s, so START failed and it never resumed). New flow: CONNECT → nudge
      START (result ignored) → poll STATUS until `RUN=1` or connect_timeout →
      resume. Note: a laptop test-sink refuses a cold WROOM32-initiated A2DP
      connection (environmental), but a real speaker/earbud accepts it — so M7
      is demonstrated against the actual product sink. Known polish item: the
      WROOM32 resets VOL to 40 on a fresh A2DP connection, so autostart music
      currently comes up at 40 rather than the persisted level.

## DOC-1 — Documentation + regression

**Status:** `[x]` Done.

### Tasks
- [x] **DOC-1a** `README.md` rewritten to describe the built S3 system
      (architecture, component table, S3↔WROOM32 wiring incl. the real
      GPIO15/16/7 I2S pins, build/test/flash, the actual `/api/*` + `/ws`
      surface, two-stage volume). Explicitly supersedes `README_orig.md`
      (the WROOM32-era stub, kept for reference).
- [x] **DOC-1b** Host tests run via `tools/run_host_tests.sh` (CTest, reports
      pass/fail counts; `--coverage`/`--asan` flags). **13 suites**: sanity,
      signal_gen, pcm_ring, i2s_out_pump, i2s_out_gain, bt_link_parser,
      bt_link_session, wifi_sm, radio_parse, station_store, radio_resampler,
      ctrl_cfg, ctrl_sm — 13/13 passing.
- [x] **DOC-1c** SPEC.md §9 "Hardware changelog" added — records the on-bench
      contract deviations (I2S pins 15/16/7, 16-in-32 slots, WiFi PS_NONE, ~5.9 s
      PCM buffer, audio/aacp detection, WROOM32 volume dead-code + s16 fix,
      laptop-sink cold-connect limitation, orchestrator RUN=1 wait, VOL-reset).

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
