# ESP I2S Source Documentation Review

**Review Date:** February 5, 2026  
**Reviewer:** GitHub Copilot (Claude Sonnet 4.5)  
**Documents Reviewed:**
- `/home/phil/work/esp32/esp32_btaudio/esp_i2s_source/docs/PRD.md`
- `/home/phil/work/esp32/esp32_btaudio/esp_i2s_source/docs/FunctionalSpecs.md` (currently empty)

**Cross-Reference:**
- `/home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/ARCH.md`
- `/home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/main/README.md`

---

## Executive Summary

The esp_i2s_source PRD is **well-structured** and shows clear intent, but has several **critical inconsistencies and gaps** when compared to the actual esp_bt_audio_source implementation. The FunctionalSpecs.md is **empty** and needs to be written.

**Status:** ⚠️ **Needs Major Revisions Before Implementation**

**Key Issues:**
1. **I2S terminology needs clarification:** PRD must explicitly state "I2S master transmitter with clock generation" (roles are correct, terminology is vague)
2. **Missing FunctionalSpecs.md:** Empty file needs complete specification
3. **Pin conflicts:** Same GPIO assignments on both devices will cause issues
4. **UART interface undefined:** Command protocol not specified
5. **Audio format assumptions need verification:** Conversion/resampling on source side unclear

---

## Detailed Findings

### 1. CRITICAL: I2S Master/Slave Terminology Clarity

**Issue Severity:** 🔴 **BLOCKER**

**PRD States (FR1):**
> Initialize I2S in **TX/source** mode with a fixed output contract of 48 kHz, 16-bit, stereo (PCM over I2S).

**Architecture Reality (from ARCH.md):**
```
ESP32 #2 (WiFi)                     ESP32 #1 (Bluetooth)
----------------                    -------------------
I2S_BCK (GPIO26, Master) ---------> I2S_BCK (GPIO26, Slave)
I2S_WS (GPIO25, Master)  ---------> I2S_WS (GPIO25, Slave)
I2S_DO (GPIO22, Master)  ---------> I2S_DI (GPIO22, Slave)
```

**esp_bt_audio_source Implementation (from main/README.md):**
> Configures an I2S RX channel as **slave**, sets DMA descriptors/counts, and assigns pins from `audio_config_t`

**Analysis:**
- esp_i2s_source PRD correctly identifies it should be I2S **master/transmitter** ✅
- esp_bt_audio_source is configured as I2S **slave/receiver** ✅
- **The architecture and roles are CORRECT** ✅
- **Issue:** PRD uses vague "TX/source mode" terminology instead of explicit "I2S master transmitter with BCLK/WS clock generation"
- Risk: Developer might not understand they need to configure peripheral as master (clock generator) vs slave (clock follower)

**Recommendation:**
- ✅ **Clarify PRD FR1:** "Initialize I2S as **master transmitter** (not slave) with BCLK/WS generation"
- Add explicit note: "esp_bt_audio_source expects to be I2S slave; we generate clocks"
- Reference ARCH.md diagram explicitly

---

### 2. CRITICAL: GPIO Pin Assignment Conflict

**Issue Severity:** 🔴 **BLOCKER**

**PRD Implies (Section 6, inherited from ARCH.md):**
Both devices using same GPIO numbers:
- BCLK: GPIO26
- WS: GPIO25  
- DATA: GPIO22

**Problem:**
These are **pin numbers on each individual ESP32**, not a shared bus specification. Each ESP32 can use different GPIO pins for I2S.

**Current esp_bt_audio_source defaults:**
```c
// From main.c load_audio_boot_config()
.i2s_bclk_pin = GPIO_NUM_26,
.i2s_ws_pin = GPIO_NUM_25,
.i2s_din_pin = GPIO_NUM_22,
```

**Recommendation:**
- 🔧 **PRD must specify esp_i2s_source pin assignments** explicitly
- Suggested defaults for esp_i2s_source (to avoid confusion):
  - BCLK output: GPIO18 (or GPIO26 if mirroring is intentional)
  - WS output: GPIO23 (or GPIO25)
  - DOUT: GPIO19 (or GPIO22)
  - **Document that these are wired to esp_bt_audio_source GPIO26/25/22**
- Add wiring diagram showing:
  ```
  esp_i2s_source          Physical Wire          esp_bt_audio_source
  GPIO18 (BCLK) -------- 3-wire cable --------> GPIO26 (BCLK)
  GPIO23 (WS)   -------- 3-wire cable --------> GPIO25 (WS)
  GPIO19 (DOUT) -------- 3-wire cable --------> GPIO22 (DIN)
  GND           -------- common ground -------> GND
  ```

---

### 3. CRITICAL: UART Command Protocol Undefined

**Issue Severity:** 🔴 **BLOCKER**

**PRD States (Section 5, FR4):**
> Provide command channel to `esp_bt_audio_source` (serial/UART or other agreed link): send commands and fully parse/process responses.

**Problem:**
- No specification of UART baud rate, pins, or protocol format
- esp_bt_audio_source uses **115200 8N1** on GPIO16/17 (see ARCH.md)
- esp_bt_audio_source command protocol is **well-defined** (see README.md, FS.md)
  - Format: `COMMAND ARGS\n`
  - Responses: `OK|CMD|RESULT\n` or `ERR|CMD|CODE|MESSAGE\n`
  - Events: `EVENT|TYPE|SUBTYPE|DATA\n`

**Missing from PRD:**
- UART pins for esp_i2s_source (suggested: GPIO16 TX, GPIO17 RX to match peer)
- Baud rate specification (should be 115200 to match)
- Command list that esp_i2s_source will send:
  - `SCAN`, `CONNECT`, `DISCONNECT`, `START`, `STOP`, `PLAY`, `VOLUME`, `STATUS`, etc.
- Response parsing strategy (line-buffered, state machine, timeout handling)
- How to handle asynchronous `EVENT|...` lines from esp_bt_audio_source

**Recommendation:**
- 🔧 **Add Section 5.1: UART Physical Interface**
  - Pins: GPIO16 (TX), GPIO17 (RX), GND
  - Baud: 115200, 8N1
  - Reference esp_bt_audio_source command interface spec
- 🔧 **Add Section 5.2: Command Protocol**
  - Inherit esp_bt_audio_source command format exactly
  - Document minimum command set esp_i2s_source must support
  - Specify response timeout and retry policy
  - Document event stream handling (async EVENT lines)

---

### 4. MAJOR: Audio Format Conversion Responsibilities Unclear

**Issue Severity:** 🟡 **MAJOR**

**PRD States (FR13):**
> Format normalization: Any inbound audio (tone, WAV, radio, other PCM) must be converted/resampled to 48 kHz, 16-bit, stereo before I2S output.

**esp_bt_audio_source Reality:**
- **I2S manager** (`i2s_manager.c`) reads I2S RX as **slave** and:
  - Converts bit depth if needed
  - Resamples to output rate (default 48 kHz) if needed
  - Fills ring buffer with processed audio
- **Expects:** I2S input can be various formats, will normalize internally
- **Design:** esp_bt_audio_source is **flexible** and can handle different I2S input rates

**Design Decision:**
- ✅ **PRD FR13 is correct:** esp_i2s_source should normalize to 48 kHz/16-bit/stereo
- **Rationale:** 
  - 48 kHz is the professional/streaming standard (aligns with modern internet radio)
  - Reduces CPU load on BT side (esp_bt_audio_source can resample if sink needs different rate)
  - esp_bt_audio_source is designed to be flexible for various I2S sources
  - esp_i2s_source is primarily a proof-of-concept test source
- **Note:** esp_bt_audio_source can handle other rates thanks to its resampling capability, but 48 kHz input is optimal for modern streaming sources.
- Document what happens if esp_i2s_source can't convert (e.g., unsupported codec):
  - Fallback to silence/tone?
  - Report error via UART to esp_bt_audio_source?

---

### 5. MAJOR: Internet Radio Implementation Complexity

**Issue Severity:** 🟡 **MAJOR**

**PRD States (FR12):**
> Internet radio ingest: accept a user-provided stream URL (HTTP/HTTPS), fetch and decode MP3 (initial codec), convert to 16-bit/48 kHz stereo PCM, and feed I2S output.

**Concerns:**
1. **Codec support:** MP3 only? What about AAC, OGG, FLAC (common in internet radio)?
2. **HTTPS:** Certificate validation? Which root CAs? Espressif's cert bundle?
3. **Buffering:** How much? Circular buffer for network jitter?
4. **Stream metadata:** ICY/Shoutcast metadata parsing (song titles, etc.)?
5. **Resilience:** What if stream dies mid-playback? Auto-reconnect? User intervention?
6. **Resource usage:** MP3 decoder uses significant RAM/CPU on ESP32

**Missing from PRD:**
- Codec library choice (e.g., ESP-ADF, libhelix-mp3, minimp3)
- Memory budget for decoder + network buffers
- Supported streaming protocols (HTTP, HTTPS, HLS, RTSP?)
- User experience: how to enter stream URL (web UI form?)
- Error handling: malformed stream, 404, DNS failure, timeout

**Recommendation:**
- 🔧 **Break FR12 into sub-requirements:**
  - FR12.1: HTTP-only MP3 streaming (MVP)
  - FR12.2: HTTPS support (with Espressif cert bundle)
  - FR12.3: AAC codec support (future)
  - FR12.4: Stream metadata display (future)
- 🔧 **Add Non-Functional Requirements:**
  - NFR8: Internet radio decoding shall not exceed 40% CPU on ESP32 @ 240 MHz
  - NFR9: Minimum 32 KB network buffer for stream resilience
  - NFR10: Auto-reconnect after stream drop with exponential backoff (max 3 attempts)
- 🔧 **Document library dependencies:**
  - ESP-IDF components: esp_http_client, esp_https_ota (for cert bundle)
  - Third-party: libhelix-mp3 or ESP-ADF audio elements
- 🔧 **Add FR12.5:** User shall enter stream URL via web UI form; validate URL format before fetch

---

### 6. MAJOR: Web UI Scope and Security

**Issue Severity:** 🟡 **MAJOR**

**PRD States (FR10, FR11):**
> Web admin: serve minimal control UI over HTTP; initial Wi‑Fi AP mode for direct connection.
> Web admin must let user choose Wi‑Fi mode: AP mode (default) vs STA mode

**Missing Specifications:**
1. **Web server implementation:** Which HTTP server (ESP-IDF's httpd, custom)?
2. **API endpoints:** RESTful? JSON payloads? Server-Sent Events for live status?
3. **Authentication:** None in AP mode? WPA passphrase doubles as admin password?
4. **Captive portal:** Should AP mode redirect all traffic to web UI (like router setup)?
5. **Wi-Fi credential storage:** How to securely store STA SSID/password in NVS?
6. **STA mode recovery:** If STA join fails, how long to wait before auto-reverting to AP?
7. **Concurrent access:** What if two users open web UI simultaneously?

**Security Concerns:**
- Open AP with no auth = anyone nearby can control audio/settings
- Storing Wi-Fi passwords in NVS without encryption = security risk if flash is dumped
- HTTP (not HTTPS) = credentials sent in plaintext

**Recommendation:**
- 🔧 **Add Section 10.1: Web UI Security Policy**
  - AP mode: No authentication (acceptable for direct physical access use case)
  - STA mode: Optional basic auth (username/password) for web UI access
  - Wi-Fi credential storage: Use NVS encryption if IDF supports it
  - HTTPS: Out of scope for initial release (certificate complexity)
- 🔧 **Add FR14: Web UI Endpoints**
  - `GET /` - Serve main UI (HTML)
  - `GET /api/status` - JSON status (Wi-Fi mode, IP, connected devices, audio state)
  - `POST /api/wifi/mode` - Switch between AP/STA (with reboot warning)
  - `POST /api/wifi/sta/config` - Set SSID/password for STA mode
  - `POST /api/radio/url` - Set internet radio stream URL
  - `POST /api/bt/command` - Send command to esp_bt_audio_source via UART
  - `GET /api/bt/status` - Poll latest status from esp_bt_audio_source
- 🔧 **Add NFR11: Captive Portal**
  - In AP mode, redirect all DNS queries to ESP32 IP so any URL navigates to web UI
  - Use ESP-IDF's DNS server component

---

### 7. MODERATE: Testing Strategy Incomplete

**Issue Severity:** 🟠 **MODERATE**

**PRD States (Section 11):**
> Device tests: loopback or tone-playback assertions via existing Unity runner; 10-minute soak without WDT.

**Problems:**
1. **No host test strategy:** How to mock I2S TX, Wi-Fi, HTTP client, MP3 decoder?
2. **No integration test plan:** How to test esp_i2s_source + esp_bt_audio_source together?
3. **No web UI testing:** Manual only? Automated UI tests?
4. **No performance benchmarks:** CPU, memory, latency targets not measurable

**Missing Test Cases:**
- Unit tests:
  - Tone generator math (frequency accuracy, amplitude)
  - UART command serialization/parsing
  - Wi-Fi mode state machine
  - Stream URL validation
  - MP3 decoder frame handling
- Integration tests:
  - I2S master output → logic analyzer verification (BCLK/WS timing)
  - UART command → esp_bt_audio_source → parse response
  - Internet radio stream → decode → I2S output (end-to-end latency)
- Stress tests:
  - 10-minute internet radio playback (no drops, no WDT)
  - Wi-Fi mode switching (AP ↔ STA) under audio load
  - Concurrent web UI access + audio streaming

**Recommendation:**
- 🔧 **Expand Section 11 with test matrix:**

| Test Type | Component | Test Case | Pass Criteria |
|-----------|-----------|-----------|---------------|
| Unit | Tone Generator | 1 kHz sine accuracy | < 0.1% frequency error |
| Unit | UART | Command serialization | Matches esp_bt_audio_source format |
| Unit | Wi-Fi | Mode switch AP→STA | NVS persists correctly |
| Integration | I2S + BT | Tone → esp_bt_audio_source | Audio plays on BT speaker |
| Integration | Radio + I2S | MP3 stream → decode → I2S | No audio glitches for 1 min |
| Stress | Radio | 10-min internet radio | No WDT, < 5% frame drops |
| Stress | Web UI | 10 concurrent connections | Server responds < 2s |

- 🔧 **Add Milestone M5: Testing & Validation**
  - Create test automation harness
  - Logic analyzer validation for I2S timing
  - Cross-device integration test suite

---

### 8. MODERATE: Configuration and NVS Storage

**Issue Severity:** 🟠 **MODERATE**

**PRD States (Section 9):**
> Build-time: defaults via Kconfig (sample rate, bits, channels, buffer sizes, PSRAM use).
> Run-time: struct-driven config; rejects invalid combos

**Missing Details:**
1. **What gets persisted in NVS?**
   - Wi-Fi credentials (SSID, password, mode)
   - Internet radio stream URL
   - Volume/gain settings
   - I2S pin assignments
   - Audio source preference (tone vs radio)
2. **NVS namespace:** What prefix to avoid conflicts?
3. **Version migration:** What if NVS schema changes in future release?
4. **Factory reset:** How to clear NVS and revert to defaults?

**Recommendation:**
- 🔧 **Add Section 9.1: NVS Persistence Policy**
  - Namespace: `"esp_i2s_src"` (avoid conflicts with esp_bt_audio_source NVS)
  - Keys:
    - `wifi_mode` (uint8: 0=AP, 1=STA)
    - `sta_ssid` (string, max 32 bytes)
    - `sta_pass` (string, max 64 bytes, consider encryption)
    - `radio_url` (string, max 256 bytes)
    - `audio_gain` (uint8, 0-100)
    - `i2s_pins` (struct: bclk, ws, dout)
  - Version tag: `nvs_schema_v1` for future migration
  - Factory reset: Web UI button or UART command `RESET_NVS`
- 🔧 **Add FR15: Configuration Persistence**
  - All user settings (Wi-Fi, radio URL, gain) persist across reboots
  - Invalid NVS data triggers fallback to Kconfig defaults + warning log
  - Factory reset clears NVS and reboots to AP mode

---

### 9. MODERATE: Dependency on esp_bt_audio_source State

**Issue Severity:** 🟠 **MODERATE**

**PRD Implies:**
esp_i2s_source sends commands to esp_bt_audio_source, but doesn't specify:

1. **Startup coordination:** Which device boots first?
2. **Command synchronization:** What if esp_bt_audio_source isn't ready?
3. **Error propagation:** If esp_bt_audio_source returns `ERR|CONNECT|FAILED`, what does esp_i2s_source do?
4. **Status polling:** How often to query `STATUS` from esp_bt_audio_source?

**Scenario Example:**
- User connects to esp_i2s_source web UI
- User clicks "Connect to Speaker XYZ"
- esp_i2s_source sends `CONNECT AA:BB:CC:DD:EE:FF\n` via UART
- esp_bt_audio_source returns `ERR|CONNECT|NOT_FOUND|Device not paired`
- **What should web UI show?** "Connection failed" or "Please pair first"?

**Recommendation:**
- 🔧 **Add Section 5.3: Command State Machine**
  - Document command sequence for common operations:
    - Pair + Connect: `PAIR <mac>` → wait for `OK|PAIR` → `CONNECT <mac>`
    - Play internet radio: `START` (esp_bt_audio_source already connected)
    - Volume change: `VOLUME <pct>` (immediate, no waiting)
  - Specify timeout policy: If no response in 5 seconds, retry once then fail
  - Error mapping table:

| esp_bt_audio_source Error | esp_i2s_source Action | Web UI Message |
|---------------------------|----------------------|----------------|
| `ERR|CONNECT|NOT_FOUND` | Stop command, display error | "Device not paired. Please pair first." |
| `ERR|START|BUSY` | Retry after 1s, max 3 attempts | "Audio busy, retrying..." |
| `ERR|PLAY|FS|FILE_NOT_FOUND` | Display error, no retry | "File not found on BT device." |

- 🔧 **Add FR16: Command Response Handling**
  - Parse all `OK|...` and `ERR|...` responses from esp_bt_audio_source
  - Surface errors to web UI with human-readable messages
  - Log all command/response pairs for diagnostics
  - Support asynchronous `EVENT|...` lines (e.g., connection state changes)

---

### 10. MINOR: Terminology and Clarity

**Issue Severity:** 🟢 **MINOR**

**Issues:**
1. PRD uses "esp_bt_audio_source" in quotes or lowercase; should be consistent
2. "TX/source mode" for I2S is ambiguous (use "master transmitter")
3. FR2 says "external PCM provider (host/task)" — what does "host" mean here? (Confusing with "host tests")
4. NFR1 latency target "< 20 ms" — from what to what? (Internet radio fetch to I2S output?)

**Recommendations:**
- ✅ Use `esp_bt_audio_source` (monospace) consistently
- ✅ Replace "I2S TX/source mode" with "I2S master transmitter mode"
- ✅ Clarify FR2: "External PCM provider" = "FreeRTOS task providing PCM samples"
- ✅ Specify NFR1 latency endpoints: "Audio decode completion to I2S DMA transmission < 20 ms (one-way)"

---

## Summary of Required Actions

### Before Implementation Begins:

1. **🔴 BLOCKER: Fix I2S terminology** (Section 1, Finding #1)
   - Clarify master/slave roles explicitly in PRD

2. **🔴 BLOCKER: Define GPIO pin assignments** (Section 2, Finding #2)
   - Add wiring diagram showing esp_i2s_source pins → esp_bt_audio_source pins

3. **🔴 BLOCKER: Specify UART protocol** (Section 3, Finding #3)
   - Add Section 5.1 (UART Physical) and 5.2 (Command Protocol)
   - Reference esp_bt_audio_source command spec

4. **🟡 MAJOR: Write FunctionalSpecs.md** (Finding #0)
   - Empty file needs complete specification based on PRD

5. **🟡 MAJOR: Scope internet radio** (Section 5, Finding #5)
   - Break into MVP (HTTP MP3) vs future features
   - Document library dependencies and resource budget

6. **🟡 MAJOR: Define web UI API** (Section 6, Finding #6)
   - Add REST endpoint specs
   - Document security policy for AP vs STA mode

7. **🟠 MODERATE: Expand test strategy** (Section 7, Finding #7)
   - Add test matrix with pass criteria
   - Plan for logic analyzer validation

8. **🟠 MODERATE: Specify NVS schema** (Section 8, Finding #8)
   - Document all persisted keys
   - Add factory reset mechanism

9. **🟠 MODERATE: Document command sequences** (Section 9, Finding #9)
   - Add state machine for esp_bt_audio_source command flow
   - Error mapping table for web UI

10. **🟢 MINOR: Clarify terminology** (Section 10, Finding #10)
    - Consistent naming and technical terms

---

## Positive Aspects (What's Good)

✅ **Clear purpose and scope** - PRD articulates the "why" well  
✅ **Use cases are concrete** - UC1-UC5 provide testable scenarios  
✅ **Non-functional requirements** - NFR1-NFR7 set measurable targets  
✅ **Extensibility considered** - Internet radio and STA mode as future features  
✅ **Builds on existing architecture** - Leverages esp_bt_audio_source well  
✅ **Open questions documented** - Section 12 shows awareness of unknowns  

---

## Recommended Next Steps

1. **Update PRD (Priority: High)**
   - Address all BLOCKER findings (I2S role, GPIO, UART)
   - Add missing sections (web API, NVS, command sequences)
   - Clarify internet radio MVP scope

2. **Write FunctionalSpecs.md (Priority: High)**
   - Translate PRD into implementation details
   - Include sequence diagrams for key flows:
     - Boot sequence (esp_i2s_source)
     - Command/response flow with esp_bt_audio_source
     - Internet radio decode → I2S pipeline
     - Wi-Fi mode switching
   - API specifications for all components
   - State machines for Wi-Fi, audio source selection, command handling

3. **Create Interface Specification (Priority: High)**
   - New document: `esp_i2s_source/docs/INTERFACE_SPEC.md`
   - Pin-level wiring between both ESP32s
   - UART protocol definition (inherit from esp_bt_audio_source)
   - I2S timing diagrams (BCLK/WS/DATA)
   - Power-on sequencing and reset handling

4. **Prototype Critical Paths (Priority: Medium)**
   - I2S master transmitter test (tone generator → logic analyzer)
   - UART command send/receive with esp_bt_audio_source
   - Basic web UI (AP mode only, status display)
   - MP3 decode to PCM (using ESP-ADF or chosen library)

5. **Update Architecture Docs (Priority: Medium)**
   - Update `esp_bt_audio_source/ARCH.md` with esp_i2s_source details
   - Create `esp_i2s_source/ARCH.md` mirroring structure
   - Document component boundaries and ownership

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| I2S master/slave misconfiguration | Medium | High | Verify with logic analyzer before integration |
| UART protocol mismatch | High | High | Use esp_bt_audio_source parser as reference impl |
| Internet radio decoder resource exhaustion | High | Medium | Start with HTTP MP3 only, measure CPU/RAM |
| Wi-Fi mode switching bugs | Medium | Medium | Extensive NVS testing, factory reset escape hatch |
| Web UI security (open AP) | Low | Low | Document as acceptable for direct access use case |
| Cross-device integration complexity | High | High | Build incremental test harness, one feature at a time |

---

## Open Questions Requiring Decisions

From PRD Section 12, plus additional questions raised:

1. ✅ **Command transport:** UART confirmed (115200 8N1, GPIO16/17)
2. ✅ **Web admin auth:** None for AP mode (acceptable risk documented)
3. ✅ **Max latency:** < 20 ms decode-to-I2S confirmed
4. ❓ **SPIFFS requirement:** Optional or mandatory for WAV test clips?
5. ❓ **STA mode priority:** MVP or defer to M4?
6. ❓ **STA multi-network:** Single SSID or list of networks with priority?
7. ❓ **Internet radio MVP:** HTTP MP3 only or also HTTPS/AAC?
8. ❓ **Codec library choice:** ESP-ADF (heavy) vs libhelix-mp3 (light)?
9. ❓ **Captive portal:** Required for AP mode or nice-to-have?
10. ❓ **Boot order:** Can esp_i2s_source assume esp_bt_audio_source is always on, or handle disconnect/reconnect?

**Recommendation:** Schedule a design review meeting to resolve open questions before FunctionalSpecs.md is written.

---

## Conclusion

The esp_i2s_source PRD is a **solid foundation** but requires **significant clarification** before implementation can begin safely. The most critical issues are:

1. I2S master/slave role must be explicit
2. UART command protocol must reference esp_bt_audio_source spec exactly
3. GPIO wiring must be documented with physical diagrams
4. FunctionalSpecs.md must be written to bridge PRD → code

**Estimated Effort to Address Findings:**
- PRD updates: 4-6 hours
- FunctionalSpecs.md: 8-12 hours  
- Interface Spec: 4-6 hours
- Prototype validation: 16-24 hours

**Timeline Recommendation:**
- Week 1: Update PRD, write FunctionalSpecs.md
- Week 2: Write Interface Spec, resolve open questions
- Week 3: Prototype I2S + UART critical paths
- Week 4+: Begin implementation per milestones

Once these documents are solid, implementation can proceed with **high confidence** that both ESP32s will interoperate correctly.

---

**Document Status:** ✅ Review Complete  
**Next Action:** Schedule design review to resolve open questions and approve revised PRD  
**Reviewed By:** GitHub Copilot (Claude Sonnet 4.5)  
**Review Date:** February 5, 2026
