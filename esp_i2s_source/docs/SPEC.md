# ESP I2S Source — Specification (Redo, July 2026)

**Status:** Authoritative spec for the from-scratch rewrite. Supersedes
`docs/PRD.md` and `docs/FS.md` (Feb 2026) where they conflict; those remain
useful for component-level detail not repeated here.
**Target hardware:** ESP32-S3 DevKit (WROOM-1 module class; pinout per
oceanlabz ESP32-S3 DevKit reference).
**Companion device:** `esp_bt_audio_source` on ESP32-WROOM32 (I2S slave
receiver + Bluetooth A2DP source), connected by I2S + UART.
**ESP-IDF:** v5.5.1 (same toolchain as the companion project).

## 1. Locked decisions (2026-07-04)

| Decision | Choice | Rationale |
| --- | --- | --- |
| Codebase | **From scratch** (`idf.py create-project`, target `esp32s3`) | Current tree is the stock IDF ws_echo_server example; target chip changes |
| Audio sources | **Phase 1: test-signal generator. Phase 2: WiFi network audio (PCM over TCP).** | Prove the I2S link first with zero external dependencies; then the real use case. Internet-radio/MP3 stays future work. |
| UART link role | **Full remote control** of the BT board (CONNECT/START/VOLUME/STATUS + EVENT monitoring) — the S3 is the system's orchestrator | BT board's UART2 command port was built for exactly this |
| I2S format | **44.1 kHz, 16-bit, stereo, Philips** — S3 is **master transmitter** | BT board is I2S SLAVE-RX (`i2s_manager.c`: `I2S_ROLE_SLAVE`, RX-only); A2DP is natively 44.1 kHz, so 44.1 out avoids the fractional resampler entirely (supersedes the old 48 kHz contract in PRD FR1) |
| Web UI | Deferred (future phase) | Not needed for the audio path or control; old PRD FR10-FR12 parked |

## 2. Pin map and wiring (the hardware contract)

### 2.1 ESP32-S3 pin selection

Chosen to avoid every restricted pin class on S3 DevKit boards: strapping
(GPIO 0/3/45/46), flash/PSRAM (26–32), UART0 console (43/44), native USB
(19/20), octal-PSRAM variants (35/36/37 — board variant unknown, so avoided),
and the DevKitC RGB LED (48).

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

### 2.2 Wiring table (S3 ↔ WROOM32)

```
ESP32-S3 (esp_i2s_source)            ESP32-WROOM32 (esp_bt_audio_source)
GPIO5  BCLK  out ──────────────────▶ GPIO26  BCLK  in   (I2S slave)
GPIO6  WS    out ──────────────────▶ GPIO25  WS    in
GPIO7  DOUT  out ──────────────────▶ GPIO22  DIN   in
GPIO17 UART1 TX ───────────────────▶ GPIO16  UART2 RX  (115200 8N1)
GPIO18 UART1 RX ◀────────────────── GPIO17  UART2 TX
GND ◀─────────────────────────────▶ GND
```

Electrical notes: both boards are 3.3 V — direct connection, no level
shifting. Keep the three I2S jumpers short and similar length (BCLK runs at
~2.8 MHz). Each board powered by its own USB (per root README power policy).
The WROOM32's USB port stays fully functional (console, UARTAUDIO, flashing).

### 2.3 I2S slot format — must match the WROOM32 slave exactly

The BT board's `configure_i2s()` (`components/audio_processor/i2s_manager.c`)
uses `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` with: data width 16-bit,
**slot width 32-bit**, ws_width 32, bit_shift true, stereo (both slots),
MCLK unused. The S3 TX channel must be configured with the identical Philips
slot configuration (16-bit data in 32-bit slots) or channels will be
misaligned/garbled.

## 3. Architecture (components to build)

```
[PC sender tool]──WiFi/TCP──▶ net_audio ──▶┐
                                            ├─▶ pcm_ring ──▶ i2s_out (master TX, 44.1k)──▶ WROOM32
                  signal_gen (tones/sweeps)─┘
bt_link (UART1) ◀──── commands/responses/events ────▶ WROOM32 UART2
console (USB serial, UART0): local command interface for the S3 itself
config: NVS (WiFi creds, volume policy, autostart behavior)
```

- **`i2s_out`** — I2S master TX at the §2.3 contract; owns a PCM ring buffer
  (SPSC — reuse the proven `audio_ringbuffer.c` pattern from the BT project);
  zero-fills on underrun and counts it; telemetry counters.
- **`signal_gen`** — sine/sweep/silence at 44.1 kHz stereo; the Phase-1
  source and permanent diagnostic (port the tone math from
  `esp_bt_audio_source` scratchpad tools / `synth_manager` patterns).
- **`net_audio`** — Phase 2: TCP server accepting length-framed 44.1 kHz
  s16le stereo PCM; feeds `pcm_ring` with backpressure (drop-oldest policy +
  fill feedback to sender, mirroring the proven UARTAUDIO UA|FILL design).
  Companion PC tool `tools/stream_audio_net.py` (sibling of the BT project's
  `stream_audio_uart.py` — same pacing/feedback structure, TCP transport).
- **`bt_link`** — UART1 client speaking the BT board's command protocol:
  send `CMD\r\n`, await `OK|`/`ERR|` (single-line completion contract),
  collect asynchronous `EVENT|` lines (pairing, UARTAUDIO stopped, etc.).
  State machine mirrors the python `ESP32Serial` driver
  (`esp_bt_audio_source/test/laptop_bt_tests/esp32_serial.py`) in C.
- **`orchestrator`** — boot flow: bring up I2S + signal source → via
  `bt_link`: `STATUS` → `CONNECT <sink>` (from NVS) → `START` → audio flows.
  Reacts to EVENT lines (reconnect on disconnect, etc.).
- **`console`** — minimal command set on the S3's own USB serial for humans:
  `TONE <hz>|OFF`, `NET ON|OFF`, `BT <passthrough command>`, `STATUS`.

## 4. Interfaces

### 4.1 UART command link (S3 → WROOM32 UART2)
- 115200 8N1; protocol exactly as documented in
  `esp_bt_audio_source/README.md` (`<STATUS>|<COMMAND>|<RESULT>[|<DATA>]`,
  `EVENT|` interleaving allowed, single terminal response per command).
- The S3 treats `EVENT|` lines as async notifications; responses correlate
  by command token.
- Note: the BT board broadcasts events to both its UARTs; the S3 will see
  everything a USB console sees.

### 4.2 Network audio protocol (Phase 2)
- TCP, port 5005 (configurable). Frame: `magic(2) len(2) seq(2)` + PCM
  payload (≤4096 B, multiple of 4). Server replies with periodic fill-level
  feedback lines for sender pacing (design lifted from the UARTAUDIO
  protocol, which is proven; CRC omitted — TCP guarantees integrity).

## 5. Verification strategy

1. **Bench E2E is the north star**: S3 tone → I2S → WROOM32 → A2DP →
   earbuds. Audible tone + `AUDIO_STATUS` on the BT board showing
   `I2S_BYTES` growing and `SOURCE=I2S` is the acceptance test for Phase 1.
2. The UART link phase doubles as the **physical verification of the BT
   board's UART2 port** (never yet exercised with real hardware).
3. Host tests: reuse the BT project's host-test architecture (CTest + Unity
   FetchContent, mocks directory) for pure logic: frame parser for net_audio,
   response parser for bt_link, signal generator sample math.
4. The BT board's existing `AUDIO_STATUS`/`READ_BPS` instrumentation serves
   as the far-end measurement of I2S delivery health.

## 6. Milestones

| ID | Milestone | Definition of done |
| --- | --- | --- |
| M1 | Project scaffold on esp32s3 | `idf.py set-target esp32s3 && idf.py build` green from scratch tree |
| M2 | Tone over I2S | Audible tone in earbuds via WROOM32; `I2S_BYTES` growing; no WROOM32-side underruns |
| M3 | UART control | S3 drives CONNECT/START/VOLUME/STATUS over UART2; EVENT lines parsed; = physical UART2 validation |
| M4 | Orchestrated boot | Power both boards → music path up with no human action |
| M5 | WiFi PCM streaming | PC tool → S3 → earbuds, ≥3 min clean (counters zero both ends) |
| M6 | Docs + regression | READMEs/specs updated; host tests green in CI pattern |

Task breakdown: see `REDO1_TODO.md`.
