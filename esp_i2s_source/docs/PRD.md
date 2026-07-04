> **⚠️ SUPERSEDED (July 2026):** the from-scratch ESP32-S3 rewrite is
> specified in [SPEC.md](SPEC.md) (with task breakdown in
> [REDO1_TODO.md](REDO1_TODO.md)). Where this document conflicts with
> SPEC.md — notably the 48 kHz I2S contract (now **44.1 kHz**), the web-UI
> scope (deferred) and pin assignments (now locked in SPEC.md §2) — SPEC.md
> wins. Retained for component-level detail and historical reference.

# ESP I2S Source — Product Requirements Document (PRD)

## 1. Purpose and Scope
- **Primary goal:** Act as the I2S audio source for `esp_bt_audio_source`, delivering 16-bit little-endian PCM, stereo, 48 kHz into the downstream pipeline with stable latency and no dropouts.
- **Command/control:** Send commands to `esp_bt_audio_source` via UART and process all responses so control flows (play/stop/status/etc.) can be driven from this device.
- **Secondary goal:** Provide a small admin/controls web UI. Initially, operate as a Wi‑Fi AP so a user can connect directly and drive commands; later may extend to STA/portal flows.
- **Target Hardware:** ESP32-S3 (512 KB SRAM, optional PSRAM support). The companion `esp_bt_audio_source` device uses ESP32 WROOM32. ESP32-S3 provides sufficient resources for WiFi, web server, MP3 decoder, and internet radio streaming with comfortable memory headroom.

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
- FR12: Internet radio streaming (phased approach):
	- **FR12.1 (MVP):** HTTP-only MP3 streaming. Accept user-provided stream URL via web UI, fetch and decode MP3 to 16-bit/48 kHz stereo PCM, feed I2S output. Handle network drops with retry/backoff (max 3 attempts, exponential backoff). Surface stream status in web UI (buffering, playing, error).
	- **FR12.2 (Future):** HTTPS support with Espressif certificate bundle for secure streams.
	- **FR12.3 (Future):** Additional codec support: AAC, OGG Vorbis, FLAC (common in internet radio).
	- **FR12.4 (Future, Nice-to-Have):** ICY/Shoutcast metadata parsing for song titles and stream info display. Parse ICY headers (`icy-metaint`) and extract metadata blocks from stream. Display current song title, artist, and station name in web UI. UI design to be determined during web server implementation phase (deferred until web UI layout finalized).
- FR13: Format normalization: Any inbound audio (tone, WAV, radio, other PCM) must be converted/resampled to 48 kHz, 16-bit, stereo before I2S output. esp_bt_audio_source has resampling capability but receives 48 kHz for optimal performance.
- FR14: Configuration Persistence: All user settings (Wi-Fi credentials, radio stream URL, audio gain, I2S pins) persist across reboots in NVS. Invalid NVS data triggers fallback to Kconfig defaults with warning log. Factory reset clears NVS and reboots to AP mode.
- FR15: Web UI Endpoints: RESTful API with JSON payloads for status queries, Wi-Fi configuration, radio URL entry, and `esp_bt_audio_source` command relay. See Section 5.4 for endpoint specification.
- FR16: Command Response Handling: Parse all `OK|...` and `ERR|...` responses from `esp_bt_audio_source`. Surface errors to web UI with human-readable messages. Log all command/response pairs for diagnostics. Support asynchronous `EVENT|...` lines (e.g., Bluetooth connection state changes).

## 4. Non-Functional Requirements
- NFR1: **Latency:** Audio decode completion to I2S DMA transmission < 20 ms (one-way) at 48 kHz stereo.
- NFR2: **Stability:** No watchdog resets during continuous 10-minute capture runs.
- NFR3: **Resource usage:** RAM use bounded; honor DRAM-only mode with reduced buffer sizes. Prefer PSRAM when available.
- NFR4: **Logging:** Default INFO-level minimal; DEBUG/VERBOSE logs must be rate-limited to avoid WDT.
- NFR5: **Portability:** Compatible with ESP-IDF v5.4 toolchain; no platform-specific hacks outside IDF APIs.
- NFR6: **Wi‑Fi UX fallback:** Always provide a safe recovery path to AP mode if STA join fails or credentials are invalid/expired.
- NFR7: **Stream robustness:** Handle internet radio variability (buffering, retries on drop, graceful mute on underflow) without watchdog resets; expose clear status in UI.
- NFR8: **Internet radio CPU usage:** MP3 decoding shall not exceed 30% CPU on ESP32-S3 @ 240 MHz during continuous playback (less than ESP32 due to improved instruction set).
- NFR9: **Internet radio buffering:** Default 64 KB network buffer for stream resilience against jitter (configurable via Kconfig, range: 32-128 KB). ESP32-S3's 512 KB SRAM provides comfortable headroom for larger buffers.
- NFR10: **Internet radio reconnect:** Auto-reconnect after stream drop with exponential backoff (1s, 2s, 4s), max 3 attempts, then fail with user notification. Distinguish between recoverable (network glitch, timeout) and non-recoverable (404, 403, bad URL) errors. Mute audio output during reconnection attempts. See Section 10.4 for detailed resilience strategy.

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

### 5.1. UART Physical Interface to `esp_bt_audio_source`

This device communicates with `esp_bt_audio_source` (the Bluetooth peer) via UART for command/control.

**Physical Configuration:**
- **Baud rate:** 115200, 8N1 (8 data bits, no parity, 1 stop bit)
- **GPIO pins (this device, `esp_i2s_source`):**
  - TX: GPIO16 (transmit commands to `esp_bt_audio_source`)
  - RX: GPIO17 (receive responses/events from `esp_bt_audio_source`)
  - Common ground required between both ESP32s
- **Wiring to `esp_bt_audio_source`:**
  ```
  esp_i2s_source (WiFi)          Physical Wire       esp_bt_audio_source (BT)
  GPIO16 (TX) ──────────────────────────────────────> GPIO17 (RX)
  GPIO17 (RX) <─────────────────────────────────────── GPIO16 (TX)
  GND         ─────────── common ground ────────────> GND
  ```
- **Hardware flow control:** None (software buffering only)
- **Line discipline:** Newline-terminated text protocol (see Section 5.2)

**Rationale:** 115200 baud matches `esp_bt_audio_source` default; GPIO16/17 are standard UART pins on ESP32 and avoid conflicts with I2S (GPIO18/23/19).

### 5.2. Command Protocol Format

This device **inherits the command protocol from `esp_bt_audio_source` exactly**. See `/esp_bt_audio_source/docs/FS.md` for authoritative specification.

**Protocol Summary:**
- **Commands (sent by `esp_i2s_source` to `esp_bt_audio_source`):**
  - Format: `COMMAND [ARGS]\n`
  - Example: `SCAN\n`, `CONNECT AA:BB:CC:DD:EE:FF\n`, `VOLUME 75\n`, `STATUS\n`
  
- **Responses (received from `esp_bt_audio_source`):**
  - Success: `OK|COMMAND|[RESULT_DATA]\n`
    - Example: `OK|SCAN|2\n` (2 devices found)
  - Error: `ERR|COMMAND|ERROR_CODE|MESSAGE\n`
    - Example: `ERR|CONNECT|NOT_FOUND|Device not paired\n`
  
- **Asynchronous Events (received from `esp_bt_audio_source`):**
  - Format: `EVENT|TYPE|SUBTYPE|DATA\n`
  - Example: `EVENT|BT|CONNECTED|AA:BB:CC:DD:EE:FF\n`
  - Events arrive independent of command/response flow; must be parsed asynchronously

**Minimum Command Set** (commands `esp_i2s_source` must support sending):
- `SCAN` — Trigger Bluetooth device scan
- `CONNECT <mac>` — Connect to paired device by MAC address
- `DISCONNECT` — Disconnect current Bluetooth connection
- `START` — Start audio playback to Bluetooth sink
- `STOP` — Stop audio playback
- `PLAY [file]` — Play file from SPIFFS (if supported)
- `VOLUME <pct>` — Set volume (0-100)
- `STATUS` — Query current state (connection, playback, audio source)
- `SAMPLE_RATE <rate>` — Set sample rate (if dynamic adjustment supported)
- `RESET` — Soft reset `esp_bt_audio_source`

**Response Parsing Requirements:**
- Line-buffered: accumulate bytes until `\n`, then parse
- Tokenize on `|` delimiter
- Validate response matches command (e.g., `OK|SCAN` for `SCAN` command)
- Handle timeout if no response within 5 seconds (see Section 5.3)
- Queue asynchronous `EVENT|...` lines for separate processing (e.g., connection state updates)

**Reference Implementation:**
- See `esp_bt_audio_source/main/cmd_*.c` for command handlers
- See `esp_bt_audio_source/docs/FS.md` Section 4 for full protocol specification

### 5.3. Command State Machine and Error Handling

This section defines how `esp_i2s_source` sequences commands to `esp_bt_audio_source` and handles responses/errors.

**Command Sequencing for Common Operations:**

1. **Pair and Connect to Bluetooth Speaker:**
   - Send: `SCAN\n`
   - Wait for: `OK|SCAN|<count>\n`
   - If `ERR|SCAN|...`, display error and abort
   - User selects device from scan results (via web UI)
   - Send: `PAIR <mac>\n`
   - Wait for: `OK|PAIR\n` (may take 10-30 seconds for pairing)
   - If `ERR|PAIR|...`, display error ("Pairing failed") and retry option
   - Send: `CONNECT <mac>\n`
   - Wait for: `OK|CONNECT\n` or `EVENT|BT|CONNECTED|...`
   - If `ERR|CONNECT|NOT_FOUND`, display "Device not paired. Please pair first."

2. **Start Internet Radio Playback:**
   - Prerequisite: Bluetooth speaker already connected
   - Decode internet radio stream to PCM on this device (see FR12)
   - Send PCM to `esp_bt_audio_source` via I2S
   - Send: `START\n` (tells `esp_bt_audio_source` to begin A2DP streaming)
   - Wait for: `OK|START\n`
   - If `ERR|START|BUSY`, retry after 1 second (max 3 attempts)
   - If `ERR|START|NOT_CONNECTED`, display "No Bluetooth speaker connected"

3. **Adjust Volume:**
   - Send: `VOLUME <pct>\n` (immediate, no preconditions)
   - Wait for: `OK|VOLUME\n`
   - If `ERR|VOLUME|...`, log warning but continue (non-critical)

4. **Query Status (periodic poll):**
   - Send: `STATUS\n` every 5 seconds (configurable)
   - Wait for: `OK|STATUS|<state_data>\n`
   - Parse state data for: connection status, playback state, audio source, errors
   - Display in web UI status panel

**Timeout and Retry Policy:**
- **Default timeout:** 5 seconds per command (configurable via Kconfig)
- **Retry strategy:**
  - Critical commands (`CONNECT`, `START`): retry once after 1 second
  - Non-critical (`VOLUME`, `STATUS`): no retry, log warning
  - Pairing commands (`PAIR`): no auto-retry (user-initiated only, long timeout 30s)
- **Timeout action:** Log error, return failure to caller, surface in web UI

**Error Code Mapping to User Messages:**

| `esp_bt_audio_source` Error Response | `esp_i2s_source` Action | Web UI Message |
|--------------------------------------|------------------------|----------------|
| `ERR|CONNECT|NOT_FOUND|...` | Stop command, display error | "Device not paired. Please pair first." |
| `ERR|CONNECT|FAILED|...` | Stop command, display error | "Connection failed. Check device is on and in range." |
| `ERR|START|BUSY|...` | Retry after 1s, max 3 attempts | "Audio busy, retrying..." |
| `ERR|START|NOT_CONNECTED|...` | Stop command, display error | "No Bluetooth speaker connected." |
| `ERR|PLAY|FS|FILE_NOT_FOUND|...` | Display error, no retry | "File not found on BT device." |
| `ERR|VOLUME|OUT_OF_RANGE|...` | Clamp to 0-100, retry | "Volume adjusted to valid range (0-100)." |
| `ERR|SCAN|BUSY|...` | Wait 2s, retry once | "Scan in progress, please wait..." |
| `ERR|<any>|TIMEOUT|...` | Log timeout, surface error | "Command timeout. Check UART connection." |

**Asynchronous Event Handling:**
- `EVENT|BT|CONNECTED|<mac>` → Update UI: "Connected to <mac>"
- `EVENT|BT|DISCONNECTED|...` → Update UI: "Disconnected", stop audio streaming
- `EVENT|BT|PAIRING|...` → Update UI: "Pairing in progress..."
- `EVENT|AUDIO|UNDERRUN|...` → Log warning, increment telemetry counter
- `EVENT|ERROR|...` → Display error in UI status bar

**State Synchronization:**
- On boot: Send `STATUS\n` to query `esp_bt_audio_source` state
- If `esp_bt_audio_source` not responding: retry 3 times over 15 seconds, then display "UART connection failed"
- Maintain local state mirror (connected/disconnected, playing/stopped) updated by responses and events

### 5.4. Web UI API Endpoints (FR15)

RESTful HTTP API for web UI control. All endpoints return JSON.

**Status and Info:**
- `GET /api/status`
  - Returns: `{"wifi_mode": "AP", "ip": "192.168.4.1", "bt_connected": true, "bt_device": "AA:BB:CC:DD:EE:FF", "audio_state": "playing", "volume": 75, "stream_url": "http://...", "uptime_s": 12345}`
  - Use for periodic status polling (every 2-5 seconds)

**Wi-Fi Configuration:**
- `POST /api/wifi/mode`
  - Body: `{"mode": "AP"}` or `{"mode": "STA"}`
  - Action: Switch Wi-Fi mode, persist to NVS, reboot
  - Response: `{"status": "ok", "message": "Rebooting to <mode> mode..."}`
  
- `POST /api/wifi/sta/config`
  - Body: `{"ssid": "MyNetwork", "password": "secret123"}`
  - Action: Save STA credentials to NVS (encrypted if supported)
  - Response: `{"status": "ok", "message": "Credentials saved. Switch to STA mode to connect."}`
  
- `POST /api/wifi/reset`
  - Action: Clear Wi-Fi credentials, revert to AP mode, reboot
  - Response: `{"status": "ok", "message": "Factory reset. Rebooting to AP mode..."}`

**Internet Radio:**
- `POST /api/radio/url`
  - Body: `{"url": "http://stream.example.com:8000/radio.mp3"}`
  - Action: Validate URL format, save to NVS, start streaming
  - Response: `{"status": "ok", "message": "Stream started"}`
  - Error: `{"status": "error", "message": "Invalid URL format"}`

**Bluetooth Control (relay to `esp_bt_audio_source`):**
- `POST /api/bt/command`
  - Body: `{"command": "SCAN"}` or `{"command": "CONNECT", "args": "AA:BB:CC:DD:EE:FF"}`
  - Action: Send command via UART, wait for response, return result
  - Response: `{"status": "ok", "result": "OK|SCAN|2"}` or `{"status": "error", "result": "ERR|CONNECT|NOT_FOUND|..."}`
  
- `GET /api/bt/status`
  - Returns: Latest status from `esp_bt_audio_source` (cached from last `STATUS` command)
  - Response: `{"connected": true, "device": "AA:BB:CC:DD:EE:FF", "playing": true, "source": "i2s"}`

**Audio Control:**
- `POST /api/audio/volume`
  - Body: `{"volume": 75}` (0-100)
  - Action: Send `VOLUME` command via UART
  - Response: `{"status": "ok"}`

**Factory Reset:**
- `POST /api/factory_reset`
  - Action: Clear all NVS (Wi-Fi, radio URL, settings), reboot to AP mode
  - Response: `{"status": "ok", "message": "Factory reset complete. Rebooting..."}`

**Security Note:** Initial release has no authentication in AP mode (acceptable risk for direct physical access use case). Future: add basic auth for STA mode.

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

### 9.1. NVS Persistence Policy

**Namespace:** `esp_i2s_src` (avoid conflicts with `esp_bt_audio_source` NVS namespace)

**Persisted Keys and Data Types:**

| NVS Key | Type | Size | Description | Default |
|---------|------|------|-------------|---------|
| `wifi_mode` | uint8 | 1 byte | Wi-Fi mode: 0=AP, 1=STA | 0 (AP) |
| `sta_ssid` | string | 32 bytes | STA mode SSID | (empty) |
| `sta_pass` | string | 64 bytes | STA mode password (consider NVS encryption) | (empty) |
| `radio_url` | string | 256 bytes | Internet radio stream URL | (empty) |
| `audio_gain` | uint8 | 1 byte | Audio gain/volume (0-100) | 75 |
| `i2s_bclk` | uint8 | 1 byte | I2S BCLK GPIO pin number | 18 |
| `i2s_ws` | uint8 | 1 byte | I2S WS GPIO pin number | 23 |
| `i2s_dout` | uint8 | 1 byte | I2S DOUT GPIO pin number | 19 |
| `uart_tx` | uint8 | 1 byte | UART TX GPIO pin number | 16 |
| `uart_rx` | uint8 | 1 byte | UART RX GPIO pin number | 17 |
| `schema_ver` | uint8 | 1 byte | NVS schema version tag for migration | 1 |

**Version Migration:**
- `schema_ver` key allows future schema updates
- On boot, read `schema_ver`:
  - If missing or `< 1`: initialize with defaults and set `schema_ver = 1`
  - If `schema_ver > CURRENT_VERSION`: log error, factory reset to safe state
  - If `schema_ver == CURRENT_VERSION`: load all keys
- Version-specific migration functions handle schema changes between releases

**Data Validation on Load:**
- GPIO pins: validate in range 0-39 (ESP32 valid GPIO)
- Wi-Fi mode: must be 0 (AP) or 1 (STA)
- Strings: null-terminate, validate length < max size
- Audio gain: clamp to 0-100
- Invalid data: log warning, use Kconfig default for that key, continue boot

**Fallback Policy:**
- If NVS partition not initialized: `nvs_flash_init()` and format if needed
- If any key read fails (not found, corrupted): use Kconfig default for that key
- If all keys fail: assume fresh boot, initialize NVS with defaults from Kconfig
- Log all NVS errors at WARNING level for diagnostics

**Factory Reset Mechanism:**
- Web UI endpoint: `POST /api/factory_reset` (see Section 5.4)
- Action: `nvs_erase_all()` in `esp_i2s_src` namespace, reboot
- Fallback: UART command `FACTORY_RESET\n` (if web UI inaccessible)
- After reset: boots to AP mode with Kconfig defaults

**Security Considerations:**
- **STA password storage:** Use NVS encryption if ESP-IDF supports it on target
  - Enable via `CONFIG_NVS_ENCRYPTION=y` in sdkconfig
  - Requires secure boot or flash encryption for key protection
  - If encryption unavailable: document risk (password readable via flash dump)
- **Radio URL:** No encryption needed (publicly accessible stream URL)
- **Factory reset:** No authentication in AP mode (physical access assumed)

**NVS Write Policy:**
- Write to NVS only when settings change (not on every boot)
- Commit after each write; no batching (to prevent partial state on power loss)
- Rate-limit writes: max 1 write per key per 10 seconds (prevent flash wear from UI spam)

## 10. Security and Safety
- No credentials stored/handled; N/A for audio-only path.
- Ensure ISR paths are IRAM-safe where required; avoid dynamic allocs in ISR.

### 10.1. Internet Radio Library Dependencies (FR12.1 MVP)

**For HTTP MP3 Streaming:**

**ESP-IDF Components (built-in):**
- `esp_http_client` - HTTP client for stream fetching
- `esp_netif` - Network interface abstraction
- `lwip` - TCP/IP stack

**Third-Party Audio Framework (choose one):**

| Library | Pros | Cons | Recommendation |
|---------|------|------|----------------|
| **ESP-ADF** | Multi-codec (MP3/AAC/FLAC/OGG), metadata support, audio pipeline, HTTP(S) streaming, Espressif-maintained | Larger footprint (~200 KB flash, ~60 KB heap), more complex | ✅ **Recommended (ESP32-S3)** |
| libhelix-mp3 | Lightweight (~30 KB flash, ~20 KB heap), fixed-point, mature | MP3-only, no metadata parsing, manual HTTP handling | Only if S3 unavailable |
| minimp3 | Tiny (~10 KB), simple API | Less battle-tested on ESP32, may need porting | Not recommended |

**MVP Decision:** Use **ESP-ADF** for FR12.1 (and all future phases)
- Rationale: 
  - **ESP32-S3 has sufficient resources** (512 KB SRAM, 100+ KB free heap after WiFi)
  - **Multi-codec from day one:** MP3 works immediately, AAC/FLAC/OGG trivial to add later (FR12.3)
  - **Built-in stream handling:** HTTP client, HTTPS (FR12.2), ICY metadata parsing (FR12.4) included
  - **No refactoring needed:** Avoid switching libraries when adding codecs
  - **Espressif-maintained:** Official support, examples, community
- Integration: Add ESP-ADF via IDF Component Manager (`idf_component.yml`) or clone into `components/`
- License: Apache 2.0 (compatible with commercial use)

**Implementation Path:**
- FR12.1 (MVP): Use ESP-ADF `mp3_decoder` element with `http_stream` input
- FR12.2: Replace `http_stream` with `https_stream` (one-line change + cert bundle config)
- FR12.3: Add `aac_decoder`, `flac_decoder`, `ogg_decoder` to pipeline (codec selection at runtime)
- FR12.4: Enable `icy_metadata_parser` element in pipeline (already part of ESP-ADF)

### 10.2. HTTPS Client Certificate Validation (FR12.2)

When adding HTTPS support in FR12.2, SSL/TLS certificate validation is **handled automatically by ESP-ADF/ESP-IDF**:

**Implementation (ESP-ADF):**
```c
// For HTTPS streams (FR12.2), use ESP-ADF's https_stream element:
audio_element_handle_t http_stream_reader = https_stream_init(&https_cfg);
https_stream_cfg_t https_cfg = {
    .type = AUDIO_STREAM_READER,
    .enable_playlist_parser = false,
    .cert_pem = NULL,  // NULL = use ESP-IDF cert bundle (automatic validation)
};
// ESP-ADF internally calls esp_crt_bundle_attach for certificate validation
```

**How ESP-IDF Certificate Bundle Works:**
- **Built-in Root CAs:** ESP-IDF includes Mozilla's CA certificate bundle (~130+ trusted root CAs)
- **Automatic Validation:** `esp_crt_bundle_attach` enables automatic verification of:
  - Certificate chain validity
  - Certificate expiration dates
  - Hostname matching (prevents MITM attacks)
  - Signature verification
- **No Manual Work:** No need to embed certificates, parse X.509, or maintain CA lists
- **Error Handling:** Connection fails if certificate invalid; surface error in web UI

**Configuration:**
- Enable `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` in menuconfig (usually enabled by default)
- Optional: Reduce bundle size by selecting specific CAs if flash-constrained

**Rationale for Deferring to FR12.2:**
- MVP (FR12.1) uses HTTP only to minimize complexity and flash usage
- HTTPS adds ~50 KB flash for cert bundle + TLS stack
- Most internet radio streams support both HTTP and HTTPS; HTTP sufficient for proof-of-concept

**Memory Budget (FR12.1 MVP with ESP-ADF on ESP32-S3):**
- ESP-ADF framework: ~200 KB flash, ~60 KB heap (includes MP3 decoder, audio elements, resampler)
- Network buffer: 64 KB (NFR9, default for S3)
- I2S DMA buffer: ~8 KB (existing)
- Web server: ~15 KB heap
- WiFi stack: ~40 KB heap
- **Total:** ~187 KB heap, ~200 KB flash
- **Available on ESP32-S3:** ~200-300 KB free heap after WiFi init → **comfortable 70-110 KB margin**
- **Note:** ESP-ADF overhead amortized across multi-codec support (no per-codec increase for AAC/FLAC/OGG later)

### 10.3. Network Buffering and Memory Constraints

**Buffering Strategy (ESP32-S3):**
- **Circular Buffer:** Use ring buffer for network audio data to handle jitter and temporary connection slowdowns
- **Default Size:** 64 KB (NFR9) — provides ~340 ms buffering at 48 kHz/16-bit/stereo (192 KB/s bitstream)
- **Implementation:** Two-buffer approach:
  1. **Network buffer:** Circular buffer filled by HTTP client task (64 KB default, configurable 32-128 KB)
  2. **Decode buffer:** MP3 decoder frame buffer (4-8 KB for frame assembly)
- **Flow Control:** HTTP fetch task monitors buffer fullness; pause fetching when >75% full, resume when <25%

**Platform Memory Constraints:**

| Platform | SRAM | Typical Free Heap | Max Network Buffer | PSRAM Support | Target Use |
|----------|------|-------------------|-------------------|---------------|------------|
| **ESP32-S3** (PRIMARY) | 512 KB | ~200-300 KB after WiFi stacks | 64-128 KB (comfortable) | Yes (2-8 MB optional) | **esp_i2s_source** (this project) |
| ESP32-S3 + PSRAM | 512 KB + 2-8 MB | Same SRAM, PSRAM for buffers | 256 KB+ (in PSRAM) | Yes | Future high-reliability variant |
| ESP32 WROOM32 | 320 KB | ~100-150 KB after WiFi/BT stacks | 32-64 KB (tight at 64 KB) | No | **esp_bt_audio_source** (companion device) |

**Design Decisions:**
- **Primary Target:** ESP32-S3 with 64 KB buffer (comfortable headroom for WiFi + web server + MP3 decoder + internet radio)
- **Rationale for ESP32-S3:** Internet radio streaming, web server, and MP3 decoding require more resources than ESP32 WROOM32 can comfortably provide. ESP32-S3's 512 KB SRAM eliminates memory pressure.
- **Companion Device:** `esp_bt_audio_source` uses ESP32 WROOM32 (sufficient for Bluetooth Classic + I2S slave role)
- **Configurable via Kconfig:** `CONFIG_RADIO_NETWORK_BUFFER_SIZE` (default: 65536, range: 32768-131072)
- **PSRAM Option:** ESP32-S3 with PSRAM enables 256 KB+ buffer for extreme reliability or future features (AAC decoder, larger buffers)
  - PSRAM buffer allocation: `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
  - Note: PSRAM access slower than SRAM; use for network buffers only (MP3 decoder uses SRAM)
- **Validation Plan:** M3 milestone includes 10-minute internet radio stress test on ESP32-S3
  - Monitor heap watermarks (expect >100 KB free after streaming starts), measure drop rate, tune buffer size if needed

**Memory Protection:**
- **Heap Monitoring:** Check `esp_get_free_heap_size()` before buffer allocation; fail gracefully if insufficient
- **OOM Handling:** If allocation fails, try smaller buffer (min 16 KB) or report error to web UI
- **Watchdog Safety:** Ensure decoder task yields regularly during large buffer fills (no WDT triggers)

**Rationale:**
- 32 KB buffer tested as minimum viable for stable streaming on typical internet radio bitrates (128-192 kbps MP3)
- Configurable size allows optimization based on specific use case (low-latency vs resilience)
- ESP32-S3 upgrade path available if WROOM32 proves insufficient during prototyping

### 10.4. Internet Radio Stream Resilience and Error Handling

**Failure Detection:**

The internet radio component must detect and classify stream failures to apply appropriate recovery strategies:

| Failure Type | Detection Method | Classification | Retry Strategy |
|--------------|-----------------|----------------|----------------|
| Connection timeout | `esp_http_client_open()` returns timeout | Recoverable | Exponential backoff |
| DNS failure | DNS lookup fails | Recoverable (transient) | Retry DNS, then full reconnect |
| HTTP 404/403 | HTTP status code | Non-recoverable | Immediate failure, no retry |
| HTTP 500/503 | HTTP status code | Recoverable (server error) | Exponential backoff |
| No data received | No bytes after 10s | Recoverable | Close connection, reconnect |
| Malformed stream | Decoder returns error | Partially recoverable | Skip bad frames; reconnect if persistent |
| Network disconnect | WiFi EVENT_STA_DISCONNECTED | Recoverable | Wait for WiFi reconnect, then resume |

**Auto-Reconnect Strategy:**

1. **Immediate Response (0-100ms):**
   - Detect failure (connection lost, timeout, no data)
   - Mute I2S output (write silence to prevent noise/glitches)
   - Update web UI status: "Buffering..." or "Reconnecting..."
   - Log detailed error: DNS failure, HTTP error code, timeout, etc.

2. **Exponential Backoff (NFR10):**
   - **Attempt 1:** Wait 1 second, retry connection
   - **Attempt 2:** Wait 2 seconds, retry connection
   - **Attempt 3:** Wait 4 seconds, retry connection (final attempt)
   - **Total retry duration:** ~7 seconds
   - During retries: Keep web UI updated with "Reconnecting (attempt X/3)..."

3. **Success Path:**
   - Connection established, HTTP 200 OK received
   - Start filling network buffer (wait for 25% full before decode)
   - Resume audio output (unmute I2S)
   - Update web UI: "Playing" with station name/URL
   - Reset retry counter to 0

4. **Failure Path (after 3 attempts):**
   - Stop all reconnection attempts
   - Keep I2S output muted (silence)
   - Update web UI: **"Stream unavailable"** with error details
   - Display user intervention options (see below)
   - Log final failure state for diagnostics

**User Intervention (After Max Retries):**

Web UI displays actionable error message with options:

```
┌─────────────────────────────────────────────┐
│ ⚠️  Stream Unavailable                      │
│                                             │
│ Error: Connection timeout                   │
│ URL: http://stream.example.com/radio.mp3   │
│                                             │
│ [Retry Now]  [Change URL]  [Use Tone]      │
└─────────────────────────────────────────────┘
```

**Button Actions:**
- **Retry Now:** Reset attempt counter to 0, try immediate reconnect (bypass exponential backoff for first try)
- **Change URL:** Navigate to stream URL entry form, allow user to enter new URL
- **Use Tone:** Switch audio source to tone generator (1 kHz sine wave), disable internet radio

**Error Code Mapping:**

| HTTP Status / Error | User-Friendly Message | Retry? |
|---------------------|----------------------|--------|
| Connection timeout | "Stream unreachable. Check network connection." | Yes (3×) |
| DNS failure | "Cannot resolve stream URL. Check URL or DNS settings." | Yes (3×) |
| HTTP 404 | "Stream not found (404). Check URL is correct." | No |
| HTTP 403 | "Access denied (403). Stream may require subscription." | No |
| HTTP 500/503 | "Server error. Stream may be temporarily down." | Yes (3×) |
| No data (10s) | "Stream stopped sending data. Reconnecting..." | Yes (3×) |
| Decoder error (persistent) | "Stream format unsupported or corrupted." | No |
| WiFi disconnected | "WiFi connection lost. Reconnecting to network..." | Wait for WiFi, then retry stream |

**Edge Cases:**

1. **Long Pause in Stream Data (Buffering Event):**
   - If network buffer drops below 10% full but connection still alive:
   - Display "Buffering..." in web UI (not "Reconnecting")
   - Do NOT close connection; wait for data to resume
   - Timeout after 10 seconds; if no data, treat as "No data received" and reconnect

2. **Metadata Corruption (ICY/Shoutcast):**
   - If metadata parsing fails (invalid length, bad format):
   - Skip corrupted metadata block, continue audio playback
   - Log warning, increment telemetry counter
   - Do NOT reconnect (audio stream likely still valid)

3. **WiFi Roaming / IP Change:**
   - If WiFi EVENT_STA_DISCONNECTED followed by EVENT_STA_GOT_IP (new IP):
   - Close existing HTTP connection
   - Wait 1 second for network stack to stabilize
   - Reconnect to stream URL (treat as Attempt 1, full exponential backoff)

4. **DNS Server Temporarily Unavailable:**
   - If initial DNS lookup fails:
   - Retry DNS lookup after 2 seconds (up to 3 times)
   - If DNS succeeds on retry, proceed with connection (don't count as reconnect attempt)
   - If DNS fails 3 times, treat as "DNS failure" error

5. **Stream Redirect (HTTP 301/302):**
   - Follow HTTP redirects automatically (ESP-IDF `esp_http_client` handles this)
   - Max 3 redirects to prevent loops
   - Log redirect chain for diagnostics
   - If redirect fails (404 on new URL), treat as non-recoverable

**Telemetry and Diagnostics:**

Maintain counters for monitoring stream health (exposed via `/api/status`):

```c
typedef struct {
    uint32_t total_reconnects;        // Lifetime reconnect count
    uint32_t dns_failures;            // DNS lookup failures
    uint32_t http_errors;             // HTTP 4xx/5xx responses
    uint32_t timeouts;                // Connection/read timeouts
    uint32_t decoder_errors;          // MP3 frame decode errors
    uint32_t metadata_errors;         // ICY metadata parse errors
    uint32_t buffer_underruns;        // Network buffer went empty
    uint32_t successful_connections;  // Total successful connects
    time_t last_failure_time;         // Timestamp of last failure
    char last_error_msg[128];         // Human-readable last error
} radio_stream_stats_t;
```

**Implementation Notes:**

- Use FreeRTOS task for stream fetch (blocks on network I/O, yields to other tasks)
- Separate task for decoder (consumes from ring buffer, writes PCM to I2S)
- Use task notifications for state changes (connected, failed, user stop)
- Watchdog safety: Yield during long waits, network I/O, buffer operations
- Memory safety: Always check `esp_get_free_heap_size()` before large allocations

**Rationale:**
- Best-effort auto-reconnect maximizes user experience (no manual intervention for transient failures)
- Exponential backoff prevents hammering server or network
- Clear error messages empower user to fix non-recoverable issues (bad URL, 404)
- Distinguishing recoverable vs non-recoverable errors avoids wasting time on impossible retries
- Telemetry enables debugging and health monitoring

### 10.5. Web UI Implementation and Security

**Web Server Implementation:**
- **HTTP Server:** ESP-IDF's `esp_http_server` component (`httpd`)
- **Rationale:** Mature, well-tested, low overhead, integrated with ESP-IDF event system
- **Configuration:** Single instance, max 4 concurrent connections (single active user + 3 keepalive)

**Authentication Policy:**

1. **Default Credentials:**
   - **Initial username:** `admin`
   - **Initial password:** `esp32admin` (factory default)
   - **Storage:** NVS key `web_password` (SHA256 hash, not plaintext)

2. **First Login Flow:**
   - User logs in with default credentials
   - Web UI immediately redirects to "Change Password" page (forced, cannot skip)
   - New password must be:
     - Minimum 8 characters
     - At least one uppercase, one lowercase, one digit
   - Password change persists to NVS immediately
   - User redirected to main dashboard after successful change

3. **Subsequent Logins:**
   - HTTP Basic Auth on all `/api/*` endpoints
   - Session cookie with 1-hour timeout (optional, simplifies UX)
   - Failed login attempts: 3 max per minute (rate limiting to prevent brute force)

4. **AP Mode vs STA Mode:**
   - **AP mode:** Authentication required (anyone nearby can connect to AP SSID, but web UI login needed)
   - **STA mode:** Authentication required (same policy)
   - **No difference:** Same password for both modes (stored in NVS)

**Captive Portal (Future Feature):**
- **Status:** FR17 (deferred to M5 or later)
- **Description:** In AP mode, redirect all DNS queries to ESP32 IP so any URL navigates to web UI (like router setup page)
- **Implementation:** ESP-IDF DNS server component + HTTP redirect on all non-API requests
- **Rationale:** Nice-to-have for user convenience; not critical for MVP (users can manually navigate to `192.168.4.1`)

**STA Mode Recovery (Auto-Revert):**

1. **Join Failure Detection:**
   - If `esp_wifi_connect()` fails or no `IP_EVENT_STA_GOT_IP` within 30 seconds
   - Log error: "STA join failed: [SSID], [reason code]"

2. **Auto-Revert Policy:**
   - Wait 30 seconds for STA join to complete
   - If timeout or explicit failure event:
     - Disconnect from STA
     - Switch back to AP mode (call `esp_wifi_set_mode(WIFI_MODE_AP)`)
     - Do **not** clear NVS STA credentials (preserve for next manual retry)
   - Display in web UI: "Failed to join [SSID]. Reverted to AP mode. Check credentials and try again."

3. **Manual Retry:**
   - User can retry STA join via web UI: `POST /api/wifi/mode` with `mode=STA`
   - No automatic retries (user must initiate)
   - Rationale: Prevents boot loop if credentials permanently wrong

**Concurrent Access:**

- **Single User Policy:** Web UI designed for one active user at a time
- **Enforcement:**
  - HTTP server allows 4 concurrent connections (1 active + 3 keepalive/polling)
  - If 5th connection attempt: HTTP 503 "Service Unavailable, max users reached"
  - Active session timeout: 1 hour of inactivity (close idle WebSocket/SSE connections)
- **Rationale:** Simplifies state management, reduces RAM/CPU overhead, sufficient for personal device use case

**Security Considerations:**

1. **No HTTPS for MVP:**
   - HTTP only (TLS complexity out of scope for FR10)
   - Acceptable risk: Device intended for home/personal use, not public networks
   - Future (M6+): Optional HTTPS with self-signed cert (user must accept browser warning)

2. **NVS Password Storage:**
   - Store SHA256 hash of password, not plaintext
   - Use ESP-IDF `mbedtls_sha256()` for hashing
   - Rationale: If flash dumped, password not immediately exposed

3. **Wi-Fi Credential Storage:**
   - STA SSID/password stored in NVS (plaintext, ESP-IDF default)
   - Enable NVS encryption if ESP-IDF supports it (`CONFIG_NVS_ENCRYPTION=y`)
   - Rationale: Reduces risk if device physically compromised

4. **CSRF Protection:**
   - Not implemented in MVP (single-user, trusted network assumption)
   - Future (M6+): Add CSRF tokens to POST requests

**Rationale:**
- Default password + forced change balances ease of first use with security
- Auto-revert to AP prevents "locked out" scenario if STA credentials wrong
- Single-user policy simplifies implementation and reduces attack surface
- ESP-IDF httpd component provides solid foundation without custom HTTP stack complexity

## 11. Testing and Validation

### 11.1. Test Strategy Overview

esp_i2s_source testing follows a three-tier approach: **unit tests** (pure logic in isolation), **integration tests** (cross-component and cross-device), and **stress tests** (long-running reliability and performance).

**Test Framework:**
- **Unity** for unit and device tests (existing esp_bt_audio_source pattern)
- **Host tests:** Mock ESP-IDF APIs (I2S, WiFi, HTTP client, ESP-ADF) for fast iteration
- **Device tests:** Run on actual ESP32-S3 hardware with I2S loopback or logic analyzer validation
- **Integration tests:** Two-device setup (esp_i2s_source + esp_bt_audio_source) with BT speaker

### 11.2. Test Matrix

| Test Type | Component | Test Case | Pass Criteria | Test Location |
|-----------|-----------|-----------|---------------|---------------|
| **Unit** | Tone Generator | 1 kHz sine accuracy | < 0.1% frequency error, amplitude ±2% | Host |
| **Unit** | UART Parser | Command serialization | Matches esp_bt_audio_source format exactly | Host |
| **Unit** | UART Parser | Response parsing | Correctly extracts OK/ERR/EVENT fields | Host |
| **Unit** | WiFi State Machine | AP→STA mode switch | NVS persists correctly, mode enum updates | Host |
| **Unit** | WiFi State Machine | STA join failure | Auto-reverts to AP after 30s timeout | Host |
| **Unit** | Stream URL Validator | HTTP/HTTPS URLs | Valid URLs accepted, malformed rejected | Host |
| **Unit** | Web Auth | Password hash | SHA256 hash computed correctly | Host |
| **Unit** | Web Auth | First login flow | Forced redirect to change password page | Host |
| **Unit** | NVS Persistence | Config save/load | All 11 keys persist across simulated reboot | Host |
| **Unit** | Buffer Management | Network buffer flow control | Pause fetch at >75%, resume at <25% | Host |
| **Integration** | I2S Master | Tone → Logic Analyzer | BCLK = 1.536 MHz, WS = 48 kHz, data aligned | Device |
| **Integration** | I2S Master | Silent output | DOUT stable, no glitches during idle | Device |
| **Integration** | UART Cross-Device | Command → Response | esp_i2s_source sends `STATUS`, esp_bt_audio_source replies within 100ms | Device (2x ESP32) |
| **Integration** | UART Cross-Device | Event handling | esp_bt_audio_source `EVENT\|...` lines parsed correctly | Device (2x ESP32) |
| **Integration** | ESP-ADF Pipeline | MP3 decode → I2S | No audio glitches for 1 min, latency < 20ms | Device |
| **Integration** | ESP-ADF Pipeline | Stream reconnect | Auto-reconnect after simulated disconnect (exponential backoff) | Device |
| **Integration** | Web UI → BT Control | Web UI → UART → esp_bt_audio_source | Clicking "Connect" triggers `CONNECT` command, status updates in UI | Device (2x ESP32) |
| **Integration** | End-to-End | Internet radio → I2S → BT speaker | HTTP MP3 stream plays on BT speaker, no dropouts for 1 min | Device (2x ESP32 + speaker) |
| **Stress** | Internet Radio | 10-min playback | No WDT, no disconnects, < 5% frame drops, CPU < 30% | Device |
| **Stress** | Internet Radio | Memory stability | Heap free watermark > 70 KB throughout, no leaks | Device |
| **Stress** | WiFi Mode Switch | AP ↔ STA under load | Mode switch during audio playback, NVS persists, audio resumes | Device |
| **Stress** | Web UI | 10 concurrent connections | Server responds < 2s, HTTP 503 on 5th connection | Device |
| **Stress** | Web UI | 1-hour idle | Session timeout after 1 hour, login required | Device |
| **Performance** | CPU Profiling | Internet radio streaming | CPU usage ≤ 30% during playback @ 240 MHz | Device |
| **Performance** | Latency | Decode → I2S | Audio decode completion to I2S DMA < 20ms | Device |
| **Performance** | Buffer Utilization | Network buffer fullness | 64 KB buffer provides 340ms (±20ms) buffering | Device |

### 11.3. Host Test Strategy

**Mocking Approach:**
- **I2S HAL:** Mock `i2s_channel_init()`, `i2s_channel_enable()`, `i2s_channel_write()` to return success/failure without hardware
- **WiFi Stack:** Mock `esp_wifi_init()`, `esp_wifi_set_mode()`, `esp_wifi_connect()` to simulate AP/STA state transitions
- **HTTP Client:** Mock `esp_http_client_init()`, `esp_http_client_perform()` to return fake MP3 data
- **ESP-ADF:** Mock audio pipeline elements (`http_stream`, `mp3_decoder`, `i2s_stream_writer`) to simulate decode flow
- **NVS:** Use in-memory hash map to simulate NVS read/write/commit operations

**Benefits:**
- Fast iteration (no flashing, no hardware)
- Deterministic failure injection (simulate WDT, malloc failure, timeout)
- CI/CD friendly (run on GitHub Actions, GitLab CI)

**Limitations:**
- Cannot validate I2S timing (need logic analyzer)
- Cannot test cross-device UART communication (need 2x ESP32)
- Cannot measure actual CPU/heap usage (need device profiling)

### 11.4. Integration Test Plan

**Cross-Device Test Harness:**

1. **Setup:**
   - esp_i2s_source (ESP32-S3) running test firmware with UART loopback
   - esp_bt_audio_source (ESP32 WROOM32) connected via I2S + UART
   - BT speaker paired and connected to esp_bt_audio_source
   - Logic analyzer probes on I2S signals (BCLK, WS, DOUT)

2. **Test Execution:**
   - Python test script sends commands to esp_i2s_source via USB serial
   - esp_i2s_source forwards commands to esp_bt_audio_source via UART
   - Logic analyzer captures I2S waveforms for validation
   - Audio quality assessed by human listener (no dropouts, no distortion)

3. **Automated Checks:**
   - UART round-trip latency < 100ms
   - I2S BCLK frequency = 1.536 MHz ± 0.1%
   - I2S WS frequency = 48 kHz ± 0.1%
   - Audio buffer underruns logged (should be 0)
   - BT connection stable (no `DISCONNECTED` events)

**Logic Analyzer Validation:**
- **Tool:** Saleae Logic, Rigol, or PulseView (open-source)
- **Capture Duration:** 10 seconds minimum per test
- **Analysis:**
  - BCLK duty cycle = 50% ± 5%
  - WS transitions aligned with BCLK rising edge
  - DOUT data valid on BCLK falling edge
  - No glitches during source transitions (tone → radio)
  - No timing violations (setup/hold)

### 11.5. Web UI Testing

**Manual Testing (MVP):**
- Test all 10 API endpoints with `curl` or Postman
- Verify JSON responses match schema (Section 5.4)
- Test first-login forced password change flow
- Test STA join failure → auto-revert to AP
- Test concurrent access (open 5 browser tabs, verify HTTP 503 on 5th)
- Test session timeout (wait 1 hour, verify login required)

**Future Automated UI Tests (M6+):**
- Selenium or Playwright for browser automation
- Test suite:
  - Login flow (default password, forced change, subsequent login)
  - WiFi mode switching (AP → STA, STA → AP with failure)
  - Internet radio URL entry (valid/invalid URLs)
  - BT control (scan, pair, connect, volume)
  - Session timeout and logout

### 11.6. Performance Benchmarks

**CPU Profiling:**
- **Tool:** ESP-IDF `esp_timer_get_time()` for task execution time
- **Metrics:**
  - Audio decode task: ≤ 30% CPU @ 240 MHz (NFR8)
  - HTTP fetch task: ≤ 10% CPU @ 240 MHz
  - Web server task: ≤ 5% CPU @ 240 MHz
  - Idle task: ≥ 50% CPU (healthy system)
- **Measurement:** `vTaskGetRunTimeStats()` every 10 seconds during 10-minute soak test

**Memory Profiling:**
- **Tool:** `heap_caps_get_free_size()`, `heap_caps_get_largest_free_block()`
- **Metrics:**
  - Free heap high-water mark: > 70 KB throughout stress test (NFR9)
  - Largest free block: > 32 KB (prevents fragmentation)
  - No memory leaks: Free heap stable after 10-minute playback
- **Measurement:** Log every 10 seconds, plot trend graph

**Latency Profiling:**
- **Metric:** Audio decode completion to I2S DMA transmission < 20ms (NFR1)
- **Measurement:**
  - Timestamp when ESP-ADF decoder outputs PCM frame
  - Timestamp when `i2s_channel_write()` returns
  - Calculate delta, log max/avg/p99 over 1-minute sample
- **Pass Criteria:** p99 latency < 20ms

**Network Buffer Utilization:**
- **Metric:** 64 KB buffer provides ~340ms buffering ± 20ms (Section 10.3)
- **Measurement:**
  - Track buffer fullness (bytes used / 64 KB)
  - Calculate time-to-empty at 192 KB/s bitstream rate
  - Verify no underruns during 10-minute soak test
- **Pass Criteria:** Buffer never fully empties during normal streaming

### 11.7. Test Execution and CI/CD

**Pre-Commit:**
- Run all host tests (< 30 seconds)
- Run clang-tidy static analysis
- Run clang-format verification

**Pull Request:**
- Run all host tests + device unit tests (< 5 minutes)
- Run quick integration test (tone → I2S loopback, 30 seconds)
- Build firmware for ESP32-S3 target
- Generate code coverage report (host tests)

**Nightly:**
- Run full test matrix (unit + integration + stress)
- 10-minute soak test with internet radio stream
- Logic analyzer validation (if hardware available in CI lab)
- Generate performance profiling report (CPU, memory, latency)

**Release Candidate:**
- Full manual test checklist (web UI, BT control, WiFi modes)
- Cross-device integration test with esp_bt_audio_source
- 1-hour soak test (stress + stability)
- Logic analyzer validation report attached to release notes

### 11.8. Test Success Criteria Summary

**Unit Tests:**
- 100% pass rate (no skipped or disabled tests)
- Code coverage: > 80% for all components (target > 90% for critical paths)

**Integration Tests:**
- UART command → response round-trip < 100ms
- I2S timing meets spec (BCLK/WS frequency ± 0.1%, duty cycle 50% ± 5%)
- Cross-device audio playback: no dropouts for 1 minute

**Stress Tests:**
- 10-minute internet radio: 0 WDT resets, < 5% frame drops, CPU < 30%, heap > 70 KB
- WiFi mode switch: NVS persists, audio resumes within 5 seconds
- Web UI: Handles 4 concurrent connections, rejects 5th with HTTP 503

**Performance Benchmarks:**
- CPU usage ≤ 30% during streaming @ 240 MHz (NFR8)
- Free heap > 70 KB throughout (NFR9)
- Latency < 20ms decode → I2S (NFR1)
- Buffer provides 340ms ± 20ms buffering (Section 10.3)

**Manual Validation:**
- Web UI UX: All features accessible, no confusing error messages
- Audio quality: No audible distortion, dropouts, or glitches during manual listening test
- First-user experience: Can connect to AP, change password, enter radio URL, play stream without documentation

**Regression Prevention:**
- All fixed bugs must have corresponding regression test added
- No test disabled/skipped without GitHub issue explaining why
- Performance degradation > 10% triggers investigation before merge

## 12. Open Questions
- ✅ **Command transport:** UART fully specified (Section 5.1-5.3)
- ✅ **I2S GPIO pins:** GPIO18/23/19 documented (Section 6)
- ✅ **Web UI API:** Endpoints specified (Section 5.4)
- ✅ **UART protocol:** Inherits `esp_bt_audio_source` format exactly (Section 5.2)
- ✅ **Command error handling:** State machine and retry policy documented (Section 5.3)
- ✅ **NVS schema:** Fully specified in Section 9.1 (namespace, keys, migration, factory reset)
- ✅ **Internet radio MVP:** HTTP MP3 only (FR12.1); HTTPS/AAC/OGG/FLAC deferred to future releases (FR12.2-12.4)
- ✅ **Internet radio codec library:** ESP-ADF selected (Section 10.1)
- ✅ **Web server implementation:** ESP-IDF httpd component (Section 10.5)
- ✅ **Web UI authentication:** Default password with forced first-login change (Section 10.5)
- ✅ **Captive portal:** Deferred to future (FR17, Section 10.5)
- ✅ **STA mode recovery:** Auto-revert to AP on join failure (Section 10.5)
- ✅ **Concurrent web UI access:** Single user only (Section 10.5)
- ❓ **SPIFFS requirement:** Optional or mandatory for WAV test clips?
- ❓ **STA multi-network:** Single SSID or list of networks with priority?

## 13. Milestones (proposed)
- **M1:** I2S master transmitter + tone source + UART command relay (host + device tests)
- **M2:** Web UI (AP mode only) + authentication (default password, forced change) + basic controls (tone on/off, volume via UART relay)
- **M3:** Internet radio MVP (FR12.1) — HTTP MP3 decode with ESP-ADF, feed to I2S, basic error handling
- **M4:** STA mode + auto-revert on join failure + NVS persistence (WiFi credentials, radio URL, settings) + factory reset
- **M5: Testing & Validation**
  - Create test automation harness (host + device)
  - Logic analyzer validation for I2S timing (BCLK/WS verification)
  - Cross-device integration test suite (esp_i2s_source + esp_bt_audio_source)
  - 10-minute soak test (internet radio, CPU/memory profiling)
  - Performance benchmarks (CPU ≤ 30%, heap > 70 KB, latency < 20ms)
  - Manual web UI test checklist (all 10 endpoints, auth flow, error handling)
  - Code coverage report > 80% (target > 90% for critical paths)
- **M6: Performance Tuning and Hardening**
  - Optimize CPU usage based on M5 profiling (target < 25% headroom)
  - Memory leak detection and fixes (Valgrind-style heap tracing)
  - WiFi stability improvements (roaming, reconnect edge cases)
  - Buffer tuning (minimize latency while preventing underruns)
  - Error recovery stress testing (simulate network failures, device resets)
  - Stream resilience implementation (Section 10.4): auto-reconnect, exponential backoff, user intervention UI
- **M7: Advanced Features (optional, future)**
  - HTTPS support (FR12.2)
  - Multi-codec support (FR12.3): AAC, OGG Vorbis, FLAC
  - ICY metadata parsing and display (FR12.4)
  - Captive portal for AP mode (FR17)
  - Optional HTTPS for web UI (self-signed cert)
  - CSRF protection for POST requests
  - Automated UI tests (Selenium/Playwright)

---
_Update this PRD as decisions land (latency targets, consumer interface, SPIFFS requirement)._
