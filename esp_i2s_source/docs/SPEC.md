# ESP I2S Source — Specification (Redo, July 2026)

**Status:** Authoritative spec for the from-scratch rewrite. Supersedes
`docs/PRD.md` and `docs/FS.md` (Feb 2026) where they conflict; those remain
useful for component-level detail not repeated here.
**Target hardware:** AYWHP ESP32-S3 DevKit — **ESP32-S3-WROOM-1 N16R8**
(16 MB flash, 8 MB octal PSRAM, USB-C). Pinout per oceanlabz ESP32-S3
DevKit reference.
**Companion device:** `esp_bt_audio_source` on ESP32-WROOM32 (I2S slave
receiver + Bluetooth A2DP source), connected by I2S + UART.
**ESP-IDF:** v5.5.1 (same toolchain as the companion project).

## 1. Product summary

The S3 is the **system's face and brain**; the WROOM32 stays the Bluetooth
engine. The S3 provides:

1. **Web server** with an embedded single-page UI, reachable two ways:
   - **AP provisioning mode** (default when no WiFi credentials stored):
     the S3 broadcasts its own WiFi network; connect to it, open the UI,
     enter home-WiFi credentials → S3 joins the LAN (STA mode).
   - Normal STA mode on the home LAN (mDNS: `esp-i2s-source.local`).
2. **Web serial terminal** to the WROOM32: type any raw command, see its
   response, alongside a continuously scrolling `EVENT|` feed (shared
   WebSocket).
3. **Bluetooth management UI**: scan/discovery results, pair (incl. PIN /
   SSP confirm prompts driven by `EVENT|PAIR|` lines), connect/disconnect,
   paired-device list, volume — friendly buttons over the same UART protocol.
4. **Test tone generator**: sine/sweep at selectable frequencies out the
   I2S link — the audio path's built-in diagnostic.
5. **Internet Radio**: pick a station (NVS-stored presets, editable in the
   web UI, plus a paste-any-URL field), S3 fetches the stream (HTTP/HTTPS),
   decodes **MP3 and AAC (AAC-LC + HE-AAC)**, resamples to the I2S contract,
   and plays it through the WROOM32 to the Bluetooth speaker. ICY metadata
   (station/song title) shown in the UI when present. **Users can enter their
   own station URL and save it** (with a name) as a preset in NVS, so it
   persists across reboots and can be replayed later from the station list —
   the seeded defaults and user-added stations live in the same editable list.

## 2. Locked decisions (2026-07-04)

| Decision | Choice | Rationale |
| --- | --- | --- |
| Codebase | **From scratch** (`idf.py create-project`, target `esp32s3`) | Current tree is the stock IDF ws_echo_server example |
| Board | ESP32-S3-WROOM-1 **N16R8** | User's hardware (Amazon B0DG8L5NG5, 3-pack). Octal PSRAM ⇒ GPIO35-37 unusable; 8 MB PSRAM ⇒ generous audio/TLS buffers |
| I2S format | **44.1 kHz, 16-bit, stereo, Philips** — S3 is **master transmitter** | WROOM32 is I2S SLAVE-RX; A2DP is natively 44.1 kHz (supersedes old 48 kHz contract). Radio streams at other rates are resampled on the S3 |
| UART link role | **Full remote control** of the BT board + event monitoring | BT board's UART2 command port built for this |
| Audio decoders | Espressif `esp_audio_codec` managed component (MP3, AAC-LC, HE-AAC) | Covers the radio codec landscape without pulling in ESP-ADF |
| Station UX | Presets in NVS (web-editable) + custom-URL field; defaults seeded from **internet-radio.com** "Popular Stations" list (see §5.4) | User choice (2026-07-04) |
| Web UI stack | **TypeScript + React** (Vite), built off-device → minified + gzipped bundle **embedded via EMBED_FILES**; **no filesystem partition** | User choice (2026-07-04). 16 MB flash makes an embedded gzip bundle trivial; NVS holds all mutable state. See §5.5 |
| Terminal UX | Raw terminal pane + live EVENT feed | User choice |
| Web security | Open UI on the LAN; AP mode uses WPA2 with a default password printed on the S3 console | v1 scope; LAN-trusted device |

## 3. Pin map and wiring (the hardware contract)

### 3.1 ESP32-S3 pin selection

Avoids every restricted pin class on this board: strapping (GPIO 0/3/45/46),
flash/PSRAM bus (26–32), UART0 console (43/44), native USB (19/20), **octal
PSRAM (35/36/37 — consumed on N16R8)**, RGB LED (48).

| Function | ESP32-S3 pin | Direction | Notes |
| --- | --- | --- | --- |
| I2S BCLK | **GPIO5** | out (master) | 2.8224 MHz at 44.1 kHz × 32-bit slots × 2 |
| I2S WS/LRCLK | **GPIO6** | out (master) | 44.1 kHz |
| I2S DOUT | **GPIO7** | out | PCM data to the BT board |
| UART1 TX | **GPIO17** | out | native IOMUX UART1 pin |
| UART1 RX | **GPIO18** | in | native IOMUX UART1 pin |
| GND | GND | — | common ground, mandatory |

Reserved for future phases (kept free): GPIO8/9 (I2C default), GPIO10–13
(SPI — SD card option), GPIO4/15/16/21 spare.

### 3.2 Wiring table (S3 ↔ WROOM32)

```
ESP32-S3 (esp_i2s_source)            ESP32-WROOM32 (esp_bt_audio_source)
GPIO5  BCLK  out ──────────────────▶ GPIO26  BCLK  in   (I2S slave)
GPIO6  WS    out ──────────────────▶ GPIO25  WS    in
GPIO7  DOUT  out ──────────────────▶ GPIO22  DIN   in
GPIO17 UART1 TX ───────────────────▶ GPIO16  UART2 RX  (115200 8N1)
GPIO18 UART1 RX ◀────────────────── GPIO17  UART2 TX
GND ◀─────────────────────────────▶ GND
```

Electrical notes: both boards 3.3 V — direct connection. Keep the three I2S
jumpers short and similar length (~2.8 MHz BCLK). Each board on its own USB
power. The WROOM32's USB stays fully functional (console, UARTAUDIO, flash).

### 3.3 I2S slot format — must match the WROOM32 slave exactly

The BT board's `configure_i2s()` (`components/audio_processor/i2s_manager.c`)
uses `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` with: data width 16-bit,
**slot width 32-bit**, ws_width 32, bit_shift true, stereo, MCLK unused.
The S3 TX channel must use the identical Philips slot configuration or the
audio will be channel-shifted/garbled.

## 4. Architecture

```
                                     ┌──────────────── web_ui (httpd) ───────────────┐
                                     │ REST: wifi, stations, tone, bt actions        │
        Browser ◀──WiFi──▶           │ WS:   terminal I/O + EVENT feed + status push │
                                     └──────┬─────────────────┬───────────────┬──────┘
                                            │                 │               │
 Internet ──HTTP(S)──▶ radio ──decode──▶ resample ──▶┐   bt_link (UART1)   wifi_mgr
   (MP3/AAC stream,    (esp_audio_codec)  (to 44.1k) │        │            (AP⇄STA, NVS)
    m3u/pls, ICY)                                    ├─▶ pcm_ring ─▶ i2s_out (master TX)──▶ WROOM32
                       signal_gen (tones/sweeps) ────┘
                                                          bt_link ◀──cmd/resp/EVENT──▶ WROOM32 UART2
 console (S3 USB serial): local fallback commands; config: NVS
```

- **`i2s_out`** — I2S master TX per §3.3; SPSC PCM ring (port the proven
  `audio_ringbuffer.c` pattern; ring lives in PSRAM, sized ≥256 KB for
  radio jitter absorption); zero-fill + count on underrun; stats API.
- **`signal_gen`** — sine/sweep/silence at 44.1 kHz stereo; Phase-1 source
  and permanent diagnostic.
- **`radio`** — stream client (esp_http_client + esp-tls), `.m3u`/`.pls`
  resolution, ICY metadata extraction, compressed-frame ring (PSRAM) →
  decoder task (`esp_audio_codec`: MP3/AAC-LC/HE-AAC) → resampler to
  44.1 kHz (decoder output rates 22.05–48 kHz) → `pcm_ring`. Reconnect with
  backoff on stream drop; buffer/underrun telemetry surfaced to the UI.
- **`bt_link`** — UART1 client speaking the WROOM32 command protocol:
  one-in-flight command with timeout, `OK|`/`ERR|` correlation, `EVENT|`
  fan-out to subscribers (web terminal, BT UI state, orchestrator). C
  equivalent of the proven `esp32_serial.py` driver.
- **`wifi_mgr`** — STA with NVS creds; falls back to AP mode
  (`ESP32-S3-Audio` / WPA2, password printed on console) when no creds or
  connect fails; mDNS `esp-i2s-source.local` in STA mode.
- **`web_ui`** — esp_http_server serving a **TypeScript + React** single-page
  app. The app is built off-device (Vite → one minified JS bundle + CSS +
  `index.html`), gzipped, and **baked into the firmware via EMBED_FILES**;
  served with `Content-Encoding: gzip`. **No filesystem partition** — NVS holds
  all mutable state (WiFi creds, station presets), so the only thing a FS would
  host is the static UI, and a ~50–150 KB gzipped bundle is negligible against
  16 MB flash. REST endpoints for actions/config; one WebSocket multiplexing
  terminal I/O, EVENT feed, and status updates (JSON, `type`-tagged). See §5.5
  for the frontend toolchain and the no-Node-in-IDF-build contract.
- **`source arbitration`** — explicit user action wins: starting radio
  stops tone and vice versa; silence when idle.
- **`console`** — minimal S3-local USB commands (`WIFI`, `TONE`, `RADIO`,
  `BT <raw>`, `STATUS`) as fallback when the web UI is unreachable.

## 5. Interfaces

### 5.1 UART command link (S3 → WROOM32 UART2)
115200 8N1; protocol per `esp_bt_audio_source/README.md`
(`<STATUS>|<COMMAND>|<RESULT>[|<DATA>]`, single terminal response per
command, `EVENT|` interleaving). The WROOM32 broadcasts events to both its
UARTs, so the S3 sees everything a USB console sees.

### 5.2 Web API (sketch — final shape decided during WEB-1)
- `GET /` — embedded UI. `GET /api/status` — aggregated S3 + WROOM32 state.
- `POST /api/wifi {ssid,pass}` — provision; reply then switch AP→STA.
- `GET/POST/PUT/DELETE /api/stations` — preset CRUD (NVS-backed). `POST` adds
  a user-entered station (`{name,url}`) to the saved list; `PUT` edits one;
  `DELETE` removes one. This is the "save my URL for later" path.
- `POST /api/radio {url|preset_id}` / `DELETE /api/radio` — play/stop. A raw
  `url` plays immediately without saving (one-off audition); to keep it, the
  UI offers a **"Save station"** action that `POST`s it to `/api/stations`.
- `POST /api/tone {hz}` / `DELETE /api/tone` — tone on/off.
- `POST /api/bt/{scan|pair|connect|disconnect|volume}` — BT actions
  (thin wrappers over bt_link commands).
- `WS /ws` — JSON frames: `{type:"term_in"|"term_out"|"event"|"status"|"icy", ...}`.

### 5.3 Radio stream handling
HTTP and HTTPS (esp-tls, no client certs); follow redirects; resolve
`.m3u`/`.pls` to the first stream URL; request `Icy-MetaData:1` and strip/
parse metadata blocks; content types: `audio/mpeg` → MP3 decoder,
`audio/aac(p)`/`audio/mp4` variants → AAC decoder. Unknown → error surfaced
in UI. HE-AAC (SBR) supported via esp_audio_codec.

### 5.4 Default station presets (user decision, 2026-07-04)
Ship the defaults from **internet-radio.com**'s "Popular Stations" list
(https://www.internet-radio.com/). These are `.pls` playlist links that
`RADIO-1a` resolves to the underlying Shoutcast/Icecast stream. The list on
the site rotates, so this is a *snapshot* — all presets are web-editable and
the user can add/remove/replace them from the UI, so drift is cosmetic, not a
bug. Seed the NVS store with these on first boot (chosen to span genres and
exercise the decoders):

| # | Name | Genre | Playlist URL |
| - | --- | --- | --- |
| 1 | Retro80sRadio | Rock/Pop | `https://securestreams.reliastream.com:1079/listen.pls?sid=1` |
| 2 | Smooth Jazz Deluxe | Smooth Jazz | `https://cast1.torontocast.com:4490/listen.pls?sid=1` |
| 3 | A Mississippi Blues | Blues | `https://cast1.torontocast.com:4450/listen.pls?sid=1` |
| 4 | Airport Lounge Radio | Lounge | `https://az1.mediacp.eu:443/listen/airport-lounge-radio/listen.pls?sid=1` |
| 5 | Different Drumz D&B | Drum & Bass | `https://differentdrumz.radioca.st:443/listen.pls?sid=1` |

Most internet-radio.com popular stations are MP3 (Shoutcast). The seed list
above is MP3-first and validates the MP3 path end to end. For the AAC decode
path (`RADIO-2d`) the user picked these two AAC stations (2026-07-04):

| Name | Codec (claimed) | Playlist URL | Notes |
| --- | --- | --- | --- |
| Dance UK Radio | **AAC+ (AACP) 32 kbps** | `http://uk2.internet-radio.com:8024/listen.pls` | Confirmed AAC+ — primary HE-AAC decode test |
| Hirschmilch Electronic | AAC (per station slug) | `http://hirschmilch.de:7000/listen.pls?sid=5` | ⚠️ page fetch reported `audio/mpeg`/128 kbps on port 7000 (their Prog-House MP3 feed); the AAC electronic stream is a different port. **Verify actual codec on hardware at RADIO-2d**; correct the URL/port if it decodes as MP3 |

Playlist URLs above are the underlying `.pls` links (internet-radio.com wraps
them in a `playlistgenerator` redirect; `RADIO-1a` resolves `.pls` directly).

### 5.5 Frontend toolchain (TypeScript + React, embedded — no filesystem)
The web UI is a **TypeScript + React** SPA built with **Vite**, living in a
standalone `web/` project (its own `package.json`; not part of the IDF build
tree). Build output is a single minified JS bundle + CSS + `index.html`.

- **Serving model:** `vite build` → assets are **gzipped** and embedded into
  the firmware via `EMBED_FILES` (`target_add_binary_data(... TEXT)` on the
  `.gz`); httpd serves them with `Content-Encoding: gzip`. No LittleFS/SPIFFS
  partition — mutable state lives in NVS, so the FS would only host static
  bytes, and a ~50–150 KB gzip bundle is negligible on 16 MB flash.
- **No Node in the IDF/CI build:** the gzipped artifacts are checked into
  `main/www/` (generated). `web/` has an `npm run build` that regenerates them
  via a small `scripts/embed_web.mjs` (build + gzip + copy). `idf.py build`
  and CI never invoke npm — they consume the committed `.gz`. A pre-commit or
  make target keeps the artifact in sync; a build-time hash check warns if the
  committed bundle is stale vs `web/src`.
- **Bundle discipline:** target < 200 KB gzipped. Prefer a lean dependency set
  (React + a small WS/fetch layer; no heavyweight UI kit). If size ever
  matters, `preact/compat` is a drop-in React alias that cuts the runtime to
  ~4 KB — noted as an escape hatch, not the default, since 16 MB flash makes
  React itself a non-issue here.
- **Dev ergonomics:** `vite dev` runs the UI against the device's REST/WS over
  the LAN (CORS allowed in dev builds only) for fast iteration without
  reflashing; production builds embed and lock down.

## 6. Memory budget (N16R8: 512 KB SRAM + 8 MB PSRAM)
- PSRAM: compressed stream ring (~512 KB), decoded PCM ring (~256 KB),
  TLS buffers, HTTP scratch — total well under 2 MB.
- SRAM: decoder working set, I2S DMA descriptors, WiFi/lwIP, httpd.
- `CONFIG_SPIRAM` (octal) enabled from INFRA-1 so allocations can prefer
  PSRAM via `heap_caps_malloc` from day one.

## 7. Verification strategy
1. **Bench E2E per phase**: tone → earbuds (Phase 1); web-driven pairing;
   radio station → earbuds ≥30 min without dropout.
2. LINK-1 doubles as the first **physical validation of the WROOM32's
   UART2 port**.
3. Host tests (CTest + Unity harness cloned from esp_bt_audio_source):
   bt_link response parser, playlist/ICY parsers, resampler math,
   signal_gen sample math, station-store CRUD logic.
4. Far-end health: WROOM32 `AUDIO_STATUS` (`I2S_BYTES`, `READ_BPS`,
   underruns) is the independent measurement of I2S delivery.
5. Fidelity spot-check: BT project's `compare_bt_capture.py` method with a
   44.1 kHz reference via laptop-as-sink.

## 8. Milestones

| ID | Milestone | Definition of done |
| --- | --- | --- |
| M1 | Scaffold on esp32s3 (N16R8, SPIRAM on) | clean `idf.py build` + boot banner |
| M2 | Tone over I2S | audible in earbuds; WROOM32 `I2S_BYTES` growing, no underruns |
| M3 | UART control (bt_link) | S3 drives CONNECT/START/VOLUME/STATUS; = physical UART2 validation |
| M4 | WiFi + web shell | AP provisioning flow works; STA + mDNS; UI serves; WS terminal + EVENT feed live |
| M5 | BT management UI | scan/pair/connect/volume from the browser, incl. PIN/SSP prompts |
| M6 | Internet Radio | MP3 + AAC stations play to earbuds ≥30 min; presets CRUD; ICY titles shown |
| M7 | Orchestrated boot + docs | power-on → auto-connect → last source resumes; docs/tests updated |

Task breakdown: see `REDO1_TODO.md`. (A PC→WiFi raw-PCM streaming mode,
formerly NET-2, is parked as optional future work — radio covers the
primary use case.)
