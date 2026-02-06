# ESP I2S Source — Product Requirements Document (PRD)

## 1. Purpose and Scope
- **Primary goal:** Act as the I2S audio source for `esp_bt_audio_source`, delivering 16-bit little-endian PCM, stereo, 48 kHz into the downstream pipeline with stable latency and no dropouts.
- **Command/control:** Send commands to `esp_bt_audio_source` via UART and process all responses so control flows (play/stop/status/etc.) can be driven from this device.
- **Secondary goal:** Provide a small admin/controls web UI. Initially, operate as a Wi‑Fi AP so a user can connect directly and drive commands; later may extend to STA/portal flows.

## 2. Users and Use Cases
- **Primary user:** Firmware integrator validating the end-to-end audio path into `esp_bt_audio_source`.
- **Use cases (initial scope):**
	- UC1: Stream 16-bit/48 kHz stereo PCM over I2S into `esp_bt_audio_source` continuously.
	- UC2: Issue control commands to `esp_bt_audio_source` (e.g., connect/play/stop/status) and parse/relay responses.
	- UC3: Provide a web admin UI (served from the AP) to trigger commands and view status/telemetry.
	- UC4: Generate test tones/beeps for diagnostics through the same I2S path.
	- UC5: Pull an Internet radio stream (user-supplied URL), decode to PCM, and feed `esp_bt_audio_source` over I2S.

## 3. Functional Requirements
- FR1: Initialize I2S as **master transmitter** (not slave) with BCLK/WS clock generation. Fixed output contract: 48 kHz, 16-bit, stereo (PCM over I2S). Downstream `esp_bt_audio_source` (configured as I2S slave) consumes this format. This device generates the I2S clocks; `esp_bt_audio_source` follows them.
- FR2: Provide audio source options: external PCM provider (FreeRTOS task), internal tone/beep generator for self-test.
- FR3: Deliver PCM frames into `esp_bt_audio_source` via I2S with bounded latency; handle underrun gracefully (zero-fill and count).
- FR4: Provide command channel to `esp_bt_audio_source` (serial/UART or other agreed link): send commands and fully parse/process responses.
- FR5: Backpressure handling on PCM ingress: configurable policy (drop-oldest vs block with timeout); default block with 50 ms timeout.
- FR6: Start/stop controls and state reporting (stopped/starting/running/error).
- FR7: Telemetry: overruns, underruns, consecutive I2S failures, peak level, last error.
- FR8: Runtime config updates (sample rate, gain, source select) with safe re-init sequencing.
- FR9: Optional WAV/test clip playback from SPIFFS (if partition present) into the I2S source path.
- FR10: Web admin: serve minimal control UI over HTTP; initial Wi‑Fi AP mode for direct connection.
- FR11: Web admin must let user choose Wi‑Fi mode:
	- AP mode (default) for first-time/field access.
	- STA mode to join an existing Wi‑Fi network; allow entering SSID/passphrase and persist credentials in NVS.
	- Provide a mode switch and status indicator (current mode, IP info).
	- Include “forget network” / revert-to-AP control for recovery.
- FR12: Internet radio ingest: accept a user-provided stream URL (HTTP/HTTPS), fetch and decode MP3 (initial codec), convert to 16-bit/48 kHz stereo PCM, and feed I2S output. Handle network drops with retry/backoff and surface status in the web UI.
- FR13: Format normalization: Any inbound audio (tone, WAV, radio, other PCM) must be converted/resampled to 48 kHz, 16-bit, stereo before I2S output. esp_bt_audio_source has resampling capability but receives 48 kHz for optimal performance.

## 4. Non-Functional Requirements
- NFR1: **Latency:** Audio decode completion to I2S DMA transmission < 20 ms (one-way) at 48 kHz stereo.
- NFR2: **Stability:** No watchdog resets during continuous 10-minute capture runs.
- NFR3: **Resource usage:** RAM use bounded; honor DRAM-only mode with reduced buffer sizes. Prefer PSRAM when available.
- NFR4: **Logging:** Default INFO-level minimal; DEBUG/VERBOSE logs must be rate-limited to avoid WDT.
- NFR5: **Portability:** Compatible with ESP-IDF v5.4 toolchain; no platform-specific hacks outside IDF APIs.
- NFR6: **Wi‑Fi UX fallback:** Always provide a safe recovery path to AP mode if STA join fails or credentials are invalid/expired.
- NFR7: **Stream robustness:** Handle internet radio variability (buffering, retries on drop, graceful mute on underflow) without watchdog resets; expose clear status in UI.

## 5. Interfaces and Commands
- API surface (proposed):
	- `esp_err_t audio_source_init(const audio_source_cfg_t *cfg);`
	- `esp_err_t audio_source_start(void);`
	- `esp_err_t audio_source_stop(void);`
	- `esp_err_t audio_source_read(uint8_t *buf, size_t len, size_t *out, uint32_t timeout_ms);`
	- `esp_err_t audio_source_set_gain(uint8_t pct);`
	- `esp_err_t audio_source_select_input(audio_input_t input); // I2S vs tone`
	- `audio_source_stats_t audio_source_get_stats(void);`
- CLI/diag hooks (optional): simple UART commands to dump stats and toggle tone mode.

## 6. Audio and Data Formats
- Default: **16-bit little-endian PCM, stereo, 48 kHz** (primary contract to `esp_bt_audio_source`). This device operates as I2S master transmitter; `esp_bt_audio_source` operates as I2S slave receiver.
- **I2S Physical Wiring:**
  - This is a **two-ESP32 system** with separate devices connected by 3-wire I2S cable + common ground
  - GPIO numbers are **per-device** (each ESP32 has its own independent GPIO pins)
  - Suggested pin assignments for `esp_i2s_source` (this device):
    - BCLK output: GPIO18
    - WS output: GPIO23
    - DOUT: GPIO19
  - Physical wiring to `esp_bt_audio_source` (peer device at GPIO26/25/22):
    ```
    esp_i2s_source (WiFi)      Physical Wire       esp_bt_audio_source (BT)
    GPIO18 (BCLK out) ────────────────────────────> GPIO26 (BCLK in)
    GPIO23 (WS out)   ────────────────────────────> GPIO25 (WS in)
    GPIO19 (DOUT)     ────────────────────────────> GPIO22 (DIN)
    GND               ─────── common ground ──────> GND
    ```
  - Note: Using different GPIO numbers (18/23/19 vs 26/25/22) clarifies in documentation that these are separate devices, not shared pins. Both choices are electrically equivalent; pins are configurable via Kconfig.
- Acceptable configs: 8/16/24/32-bit; sample rates 8 kHz–48 kHz; mono/stereo.
- Frame sizing: align DMA frames to 256–1024 samples per channel; expose in config.

## 7. Performance & Buffering
- I2S DMA: tune `dma_desc_num` and `dma_frame_num` to keep ISR load low while meeting latency.
- Ringbuffer: capacity scaled by build-time/boot-time memory; ALLOWSPLIT to let consumer read smaller chunks.
- Backpressure policy: selectable (block with timeout vs drop-oldest); default block with 50 ms timeout.

## 8. Reliability, Errors, and Telemetry
- Track counters: overruns, underruns, consecutive I2S failures, last `esp_err_t` from driver.
- Health check API returns status and last error.
- Optional tone self-test command to validate end-to-end path.

## 9. Configuration Matrix
- Build-time: defaults via Kconfig (sample rate, bits, channels, buffer sizes, PSRAM use).
- Run-time: struct-driven config; rejects invalid combos (e.g., 32-bit samples with mono-only codec).

## 10. Security and Safety
- No credentials stored/handled; N/A for audio-only path.
- Ensure ISR paths are IRAM-safe where required; avoid dynamic allocs in ISR.

## 11. Testing and Validation
- Unit tests: tone generation math, ringbuffer backpressure policy, stats counters.
- Host tests: mock I2S to verify init/start/stop/read behaviors.
- Device tests: loopback or tone-playback assertions via existing Unity runner; 10-minute soak without WDT.

## 12. Open Questions
- ✅ **Command transport:** UART confirmed (115200 8N1, GPIO16 TX / GPIO17 RX, inherit `esp_bt_audio_source` protocol exactly)
- ✅ **I2S GPIO pins:** Use GPIO18/23/19 (different from `esp_bt_audio_source` GPIO26/25/22) or mirror same pin numbers? Document wiring in INTERFACE_SPEC.md.
- Web admin scope: which commands, what telemetry, and auth/no-auth for AP mode?
- Mandatory SPIFFS test clip support, or optional?
- Do we need STA mode or captive portal after initial AP-only release?
- STA join details: support WPA2? WPA3? multi-network memory or single SSID? timeout/retry policy?
- Internet radio: only MP3 to start? Any required bitrates? Buffering target? HTTPS certificate handling?
- Internet radio codec library: ESP-ADF (heavy, full-featured) vs libhelix-mp3 (lightweight)?

## 13. Milestones (proposed)
- M1: API skeleton + tone source + stats (host tests).
- M2: I2S RX path stable on hardware with backpressure and telemetry.
- M3: Integration with downstream consumer (e.g., A2DP) and 10-minute soak pass.
- M4: Optional WAV playback and CLI diagnostics.

---
_Update this PRD as decisions land (latency targets, consumer interface, SPIFFS requirement)._ 
