# esp_i2s_source — ESP32-S3 internet-radio / tone source

The "brain" board of a two-ESP32 internet-radio system. This **ESP32-S3**
(WROOM-1 N16R8, 16 MB flash / 8 MB PSRAM) fetches internet radio (or generates
a test tone), decodes + resamples it to 44.1 kHz stereo, and streams PCM over
**I2S** to a second ESP32 (`esp_bt_audio_source`, a WROOM32) which bridges it to
a Bluetooth A2DP speaker/earbuds. WiFi provisioning, a web UI, station presets,
and the Bluetooth control link all live here; the WROOM32 is a thin A2DP sink
driver commanded over UART.

```
  Internet ──WiFi──▶ ESP32-S3 (this) ──I2S──▶ ESP32-WROOM32 ──A2DP──▶ speaker/earbuds
                     radio decode +           esp_bt_audio_source
                     resample + web UI        (commanded over UART1)
```

> This supersedes `README_orig.md` (the original WROOM32-era stub — its
> WebSocket JSON command API never matched the built firmware and is kept only
> for historical reference). The authoritative design is `docs/SPEC.md`; the
> task history is `docs/REDO1_TODO.md`.

## Architecture

Component (in `components/`) responsibilities:

| Component | Role |
|---|---|
| `signal_gen` | pure sample generators (sine tone) |
| `i2s_out` | I2S slave-TX channel + pure pump + PSRAM ring + pre-I2S software volume |
| `tone` | controllable test-tone fill source |
| `bt_link` | UART1 command protocol to the WROOM32 (line parser + session state machine + device glue) |
| `wifi_mgr` | STA/AP fallback state machine + provisioning + mDNS |
| `radio` | HTTP(S) stream client + ICY demux + PSRAM rings + `esp_audio_codec` MP3/AAC decoder + streaming resampler + station store |
| `web_ui` | `esp_http_server`: embedded SPA + REST API + WebSocket terminal/EVENT feed |
| `ctrl` | boot orchestrator: NVS config + pure FSM that auto-connects the sink and resumes the last station |
| `cmd_console` | UART console for runtime WiFi provisioning |

The single audio output path: `main`'s `audio_out` task arbitrates the source
(radio decoded PCM when playing, else the tone, else silence), applies the
pre-I2S software volume, packs 16-in-32, and writes to `i2s_out`.

## Hardware wiring (S3 ↔ WROOM32)

See `docs/SPEC.md` §3 for the full contract. Summary:

| Signal | S3 (slave) | WROOM32 (master) |
|---|---|---|
| I2S BCLK | GPIO15 | (master out) |
| I2S WS | GPIO16 | (master out) |
| I2S DOUT→DIN | GPIO7 → | (data in) |
| UART1 TX→RX | GPIO17 → | GPIO16 |
| UART1 RX←TX | GPIO18 ← | GPIO17 |

I2S is **16-bit data in 32-bit slots** (`ws_width=32`, `bit_shift`) — both sides
must agree exactly or the audio garbles. The S3 is the I2S **slave**; the
WROOM32 drives the clocks.

## Build

ESP-IDF **v5.5.1**, target `esp32s3`. WiFi credentials and other settings are
generated config — edit `sdkconfig.defaults`, not `sdkconfig` (which is
regenerated). `managed_components/` is gitignored (restored by the build).

```bash
. $HOME/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor    # S3 is on /dev/ttyACM0 (USB-JTAG)
```

The WROOM32 is on `/dev/ttyUSB0` — a **different** device; never flash it from
here (see the repo-root CLAUDE.md).

## Host tests (no hardware)

Pure logic is host-tested off-device with CTest + Unity:

```bash
tools/run_host_tests.sh            # build + run all host tests
tools/run_host_tests.sh --coverage # optional gcov/lcov
tools/run_host_tests.sh --asan     # optional AddressSanitizer
```

Current inventory: **13 suites** — `test_sanity`, `test_signal_gen`,
`test_pcm_ring`, `test_i2s_out_pump`, `test_i2s_out_gain`, `test_bt_link_parser`,
`test_bt_link_session`, `test_wifi_sm`, `test_radio_parse`, `test_station_store`,
`test_radio_resampler`, `test_ctrl_cfg`, `test_ctrl_sm`. Device/E2E behaviour
(A2DP output, endurance, cold-start) is validated on hardware — see
`docs/SPEC.md` §7 and the changelog in §9.

## Web API

Served on port 80 (also `http://esp-i2s-source.local/` via mDNS). The SPA is
embedded as a gzip blob; the same endpoints back it:

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | embedded single-page UI |
| `/api/status` | GET | aggregated status (wifi, wroom, tone, radio, i2s telemetry) |
| `/api/wifi` | POST | STA provisioning (`{ssid, pass}`) |
| `/api/tone` | POST/DELETE | enable (`{hz}`) / disable the test tone |
| `/api/radio` | POST/DELETE | play (`{url[, id]}`) / stop a stream |
| `/api/stations` | GET/POST/PUT/DELETE | station preset CRUD |
| `/api/volume` | POST | S3 pre-I2S software gain (`{pct}` 0–100) |
| `/api/ctrl` | GET/POST | boot-orchestrator config (`{sink_mac, autostart}`) |
| `/ws` | WS | terminal console (`term_in`/`term_out`) + live WROOM32 EVENT feed |

The `/ws` terminal forwards typed commands to the WROOM32 over `bt_link`
(`STATUS`, `CONNECT <mac>`, `VOLUME <0-100>`, `PAIRED`, …) and streams its
INFO/EVENT lines back.

## Volume — two stages

- **S3 pre-I2S** (`/api/volume {pct}`): trims the source level before I2S.
  In-memory (resets to 100 on boot).
- **WROOM32 post-mix** (`VOLUME 0-100` via the console): scales the final A2DP
  PCM; persisted in NVS. This is the primary control.

Both are linear and were verified on hardware via an A2DP capture sweep.
