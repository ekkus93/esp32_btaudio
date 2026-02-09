# ESP32 Bluetooth + WiFi Split Architecture

> **⚠️ Document Status (February 2026):**  
> This document contains historical architecture documentation including **obsolete sections** for WAV playback, play_manager, and SPIFFS functionality which were removed in Version 0.3.0.  
>   
> **Obsolete sections** (retained for historical reference):
> - "WAV Playback Lossless Architecture" (line ~204)
> - play_manager references throughout
> - SPIFFS partition and filesystem references
> - AUDIO_SOURCE_WAV enum references
>  
> **Current architecture:** 3 audio sources (I2S, synth, silence) - see [main/README.md](main/README.md) and [docs/FS.md](docs/FS.md) for up-to-date documentation.

## Overview

This project uses two ESP32 devices to overcome the limitations of running WiFi and Bluetooth Classic simultaneously on a single ESP32:

1. **ESP32 #1: Bluetooth Audio Source**
   - Handles all Bluetooth A2DP audio streaming
   - Receives audio from ESP32 #2 via I2S
   - Streams audio to Bluetooth speakers/headphones

2. **ESP32 #2: WiFi and Web Interface**
   - Provides WiFi connectivity (Access Point or client)
   - Hosts web server for user interface
   - Sends audio data to Bluetooth ESP32 via I2S
   - Controls Bluetooth ESP32 via UART

This separation ensures better reliability and performance than trying to run both wireless stacks on a single ESP32.

## Detailed Architecture

### ESP32 #1 (Bluetooth-focused)

**Primary Responsibilities:**
- Connect to Bluetooth speakers/headphones using A2DP profile
- Receive and buffer audio data from ESP32 #2
- Stream audio data to connected Bluetooth devices
- Accept control commands from ESP32 #2 (via UART)
- Send status updates to ESP32 #2 (via UART)

**Key Components:**
- Bluetooth Classic A2DP source profile
- I2S slave receiver for audio input
- UART for command/control interface
- Optional: status LEDs

### ESP32 #2 (WiFi-focused)

**Primary Responsibilities:**
- Provide WiFi Access Point or client connection
- Host web server for user interface
- Generate or process audio data
- Send audio data to ESP32 #1
- Send control commands to ESP32 #1
- Receive status updates from ESP32 #1

**Key Components:**
- WiFi stack (AP or client mode)
- Web server with HTML/CSS/JS interface
- I2S master transmitter for audio output
- UART for command/control interface
- Optional: Additional audio processing (effects, volume control)

## Communication Interfaces

### 1. I2S Audio Interface

Used for high-quality digital audio transmission between the ESP32s:

**Connection Diagram:**
```
ESP32 #2 (WiFi)                     ESP32 #1 (Bluetooth)
----------------                    -------------------
I2S_BCK (GPIO26, Master) ---------> I2S_BCK (GPIO26, Slave)
I2S_WS (GPIO25, Master)  ---------> I2S_WS (GPIO25, Slave)
I2S_DO (GPIO22, Master)  ---------> I2S_DI (GPIO22, Slave)
GND                      ---------> GND
```

**Note:** For ESP32 WROOM32 modules, these are recommended GPIO pins for I2S. They avoid pins used for boot modes or connected to internal flash.

### 2. UART Control Interface

Used for commands and status updates between the ESP32s:

**Connection Diagram:**
```
ESP32 #2 (WiFi)                 ESP32 #1 (Bluetooth)
----------------                -------------------
TX (GPIO17)       ------------> RX (GPIO16)
RX (GPIO16)       <------------ TX (GPIO17)
GND               ------------> GND
```

**Note:** These UART pins (GPIO16/17) are chosen to avoid conflicts with other functions on ESP32 WROOM32 modules. For higher reliability, use a baud rate of 115200.

## Audio Pipeline

1. Audio is generated or processed on ESP32 #2 (WiFi)
2. Audio data is sent via I2S to ESP32 #1 (Bluetooth)
3. ESP32 #1 receives the audio via I2S and buffers it
4. ESP32 #1 streams the audio via A2DP to connected Bluetooth speakers/headphones

This separation allows each ESP32 to focus on its primary wireless protocol, ensuring better performance and reliability.

## Troubleshooting & Debugging

### SPANLOG Command - Audio Engine Diagnostics

**Purpose:** Debug audio truncation, underruns, and source switching issues

The `SPANLOG` command dumps the last N audio engine events, showing exactly what the audio processor is doing:

```bash
> SPANLOG 10
OK|SPANLOG|
seq,timestamp_us,bytes_written,ring_used,ring_free,source,beep_active,underruns
1001,5234567,4096,8192,24576,I2S,0,0
1002,5238789,4096,12288,20480,I2S,0,0
1003,5243012,4096,16384,16384,I2S,0,0
...
```

**Columns:**
- `seq`: Sequence number (increments per audio chunk produced)
- `timestamp_us`: Microsecond timestamp when chunk was written
- `bytes_written`: Actual bytes written to ring buffer
- `ring_used`: Ring buffer used bytes after write
- `ring_free`: Ring buffer free bytes after write
- `source`: Active audio source (I2S/SYNTH/SILENCE)
- `beep_active`: Whether beep overlay is active (0/1)
- `underruns`: Cumulative underrun counter snapshot

**Common Diagnostic Patterns:**

1. **Audio truncation / early stop:**
   - Look for `source` switching unexpectedly (I2S → SILENCE)
   - Check if `bytes_written` starts decreasing before audio should end
   - Verify I2S source is active when expected

2. **Underruns (choppy audio):**
   - Watch `underruns` counter increasing
   - Look for `ring_free` approaching capacity (ring starving)
   - Check if producer is keeping up (consistent `bytes_written`)

3. **Overruns (backpressure):**
   - Look for `bytes_written < expected` (partial writes)
   - Check if `ring_used` approaches capacity (consumer can't keep up)
   - May indicate Bluetooth transmission slower than audio capture

4. **SYNTH mode not working:**
   - After `SYNTH ON`, verify `source` column shows `SYNTH`
   - If stuck on `I2S`, SYNTH priority fix may have regressed

**Usage Tips:**
- `SPANLOG` (no args) = last 10 entries
- `SPANLOG 50` = last 50 entries (useful for longer sequences)
- `SPANLOG 100` = maximum available entries
- Combine with `STATUS` to see current state + historical events

### SYNTH Command - Force Audio Source

**Purpose:** Override I2S audio with synthesized 1kHz sine wave for testing

```bash
> START          # I2S audio starts
> SYNTH ON       # Overrides I2S → synth tone plays
OK|SYNTH|mode=force_on
> SYNTH OFF      # Returns to I2S audio
OK|SYNTH|mode=force_off
```

**Use Cases:**
- Verify Bluetooth transmission path works independently of I2S
- Debug I2S issues by eliminating the I2S hardware as a variable
- Test A2DP streaming with known-good audio source
- Verify audio processor→BT pipeline is functioning

**How It Works (CODE_REVIEW7 Fix):**

The `get_active_source()` function now checks audio sources in priority order:

1. **Priority 1:** Forced SYNTH mode (`s_force_synth == true`)
2. **Priority 2:** I2S if running (`i2s_manager_is_running()`)
3. **Priority 3:** Silence (fallback)

**Before CODE_REVIEW7:** SYNTH mode was unreachable after `START` because I2S check came first.  
**After CODE_REVIEW7:** SYNTH override works correctly - `SYNTH ON` always forces synth audio regardless of I2S state.

### Ring Buffer SPSC Contract

**Critical Threading Constraint:** The audio ring buffer is **SPSC only** (Single-Producer Single-Consumer).

**CORRECT Usage:**
- **ONE producer:** `audio_engine_task` writes audio chunks
- **ONE consumer:** BT A2DP callback reads audio via `audio_processor_read()`

**UNDEFINED BEHAVIOR:**
- ❌ Multiple producers writing simultaneously
- ❌ Multiple consumers reading simultaneously  
- ❌ Using ring buffer from multiple uncoordinated tasks

**Why This Matters:**
- Ring buffer uses simple spinlock for metadata (head/tail/used_bytes)
- No multi-producer or multi-consumer synchronization
- Violating SPSC contract can cause race conditions, corruption, silent failures

**If You Need MPMC:** Use FreeRTOS queues or add external synchronization (mutexes/semaphores).

**See also:** `components/audio_processor/audio_ringbuffer.h` for detailed threading documentation.

### Buffer Statistics

**Monitor via `STATUS` command:**

```bash
> STATUS
OK|STATUS|rb_capacity=32768|rb_used=8192|rb_free=24576|rb_peak=16384|
underruns=0|overruns=0|...
```

**Key Metrics:**
- `rb_capacity`: Total ring buffer size (default 32 KB, configurable)
- `rb_used`: Current bytes in ring buffer
- `rb_free`: Available space in ring buffer
- `rb_peak`: Peak usage since boot (watermark)
- `underruns`: Count of times consumer tried to read but ring was empty
- `overruns`: Count of times producer wrote less than expected (backpressure)

**Healthy State:**
- `rb_used` oscillates between LOW and HIGH watermarks (8KB ↔ 24KB default)
- `underruns` and `overruns` counters remain at 0 or very low
- `rb_peak` stays well below `rb_capacity` (allows burst tolerance)

**Problem Indicators:**
- `underruns` increasing → Producer not keeping up (I2S issues, slow source)
- `overruns` increasing → Consumer not keeping up (BT transmission slow)
- `rb_used` stuck at 0 → Producer not running or source silent
- `rb_used` stuck at capacity → Consumer not draining or BT disconnected

### WAV Playback Removed

**Version 0.3.0 (Feb 2026):** WAV playback and SPIFFS support completely removed.

**If you try WAV_STATUS:**
```bash
> WAV_STATUS
ERROR|COMMAND_NOT_FOUND
```

**Available Audio Sources (Current):**
1. **I2S** - Primary source, captures audio from I2S microphone/input
2. **SYNTH** - Debug/test source, generates 1kHz sine wave
3. **SILENCE** - Fallback when no active source

**Historical Note:** PLAY command, SPIFFS partition, and WAV playback were removed to simplify architecture and reduce failure modes. See [docs/FS.md](docs/FS.md) for historical context.

---

## NVS Write Strategy & Flash Wear Prevention

**Background**: ESP32 NVS (Non-Volatile Storage) uses SPI flash with limited write cycles (~100k per sector). Excessive writes reduce device lifetime.

### Configuration Parameters Persisted in NVS

| Parameter | Key | Frequency | Write Strategy |
|-----------|-----|-----------|----------------|
| Audio Volume | `volume` | High (user adjustments) | **Debounced (500ms delay)** |
| I2S Pins | `i2s_bclk`, `i2s_ws`, `i2s_din`, `i2s_dout` | Very Low (setup) | Immediate commit (acceptable) |
| Audio Autostart | `audio_autostart` | Very Low (toggle) | Immediate commit (acceptable) |
| BT Device Name | `device_name` | Very Low (setup) | Immediate commit (acceptable) |
| Default PIN | `default_pin` | Very Low (setup) | Immediate commit (acceptable) |
| Paired Devices | `paired_mac_N`, `paired_name_N`, `paired_count` | Low (pairing events) | Immediate commit (event-driven) |

### Volume Commit Debouncing (CODE_REVIEW8 Task D)

**Problem**: Each volume adjustment (VOLUME command) previously wrote to NVS immediately.
- User adjusts 0→100 volume = 100 flash writes in seconds
- Reduces flash lifetime significantly with frequent use

**Solution**: Implemented debounced commit with 500ms delay timer
- Volume changes update in-memory value instantly (immediate audio response)
- Timer canceled and restarted on each new VOLUME command
- NVS commit only fires 500ms after **last** volume change
- Result: 100 rapid changes → 1 flash write (99% reduction)

**Implementation** (audio_processor.c):
```c
static esp_timer_handle_t s_volume_commit_timer = NULL;

static void volume_commit_timer_callback(void* arg) {
    nvs_storage_set_volume(s_volume_gain);  /* Deferred commit */
}

esp_err_t audio_processor_set_volume(uint8_t volume) {
    s_volume_gain = volume;  /* Immediate in-memory update */
    
    /* Debounce: cancel pending timer, restart with 500ms delay */
    if (s_volume_commit_timer != NULL) {
        esp_timer_stop(s_volume_commit_timer);
        esp_timer_start_once(s_volume_commit_timer, 500000);  /* 500ms */
    }
    
    return ESP_OK;
}
```

**Benefits**:
- ✅ Immediate user feedback (in-memory update)
- ✅ Reduced flash writes from "every change" to "once per session"
- ✅ 10x+ device lifetime improvement for heavy volume users
- ✅ No data loss risk (volume commits before deinit)

**Testing**: See `code_review/NVS_WRITE_AUDIT.md` for comprehensive audit results

### Other NVS Write Paths

All other configuration parameters have inherently low write frequencies:

1. **I2S Pin Configuration**: User-initiated command, typically once during initial setup
2. **Audio Autostart**: Toggle command, rarely changed after initial preference set
3. **BT Device Name**: Setup parameter, changed infrequently
4. **Default PIN**: Setup parameter, set once and rarely modified
5. **Paired Devices**: Event-driven (successful pairing), typically <10 writes in device lifetime

**Verdict**: Only volume requires active wear prevention. Other writes are acceptable.

### Flash Wear Lifecycle Estimates

**Assumptions**:
- ESP32 flash: ~100,000 write cycles per sector
- NVS wear leveling: Distributes writes across multiple sectors
- Heavy user: Adjusts volume 10 times/day

**Without Debouncing**:
- 10 adjustments/day × 100 writes/adjustment = 1000 writes/day
- Flash lifetime: ~100 days (3 months) before degradation risk

**With Debouncing (500ms)**:
- 10 adjustments/day × 1 write/adjustment = 10 writes/day
- Flash lifetime: ~10,000 days (**27+ years**)

**Conclusion**: Debouncing extends theoretical flash lifetime by **100x** for volume-heavy usage patterns.

### NVS Commit Failure Handling

All `nvs_storage_set_*` functions return `esp_err_t`:
- Commit failures are logged but **not fatal** (graceful degradation)
- On next boot, configuration reverts to previous saved state
- User can retry command if needed

**Example** (I2S pin config):
```c
esp_err_t err = nvs_storage_set_i2s_pins(bclk, ws, din, dout);
if (err != ESP_OK) {
    cmd_send_response("ERROR", "I2S_CONFIG", "NVS_COMMIT_FAILED", esp_err_to_name(err));
    return CMD_ERROR_HARDWARE_FAILURE;
}
```

### References

- ESP-IDF NVS Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
- CODE_REVIEW8.md: External code review identifying volume write rate issue
- code_review/NVS_WRITE_AUDIT.md: Comprehensive audit of all NVS write paths
- code_review/CODE_REVIEW8_TODO.md: Task tracking for implementation

---

## Future Expansion Possibilities

- Add a microSD card to ESP32 #2 for audio file playback
- Implement streaming audio from web sources on ESP32 #2
- Add audio effects processing on ESP32 #2
- Use additional GPIO pins for hardware controls (buttons, rotary encoders)
- Add a display to ESP32 #2 for local user interface
- Implement battery level monitoring if devices are battery powered
- Add OTA (Over-The-Air) updates for the WiFi ESP32
- Create mobile app interface for remote control
- Add multi-room audio synchronization with multiple BT transmitters

## Recommended Hardware Configuration

### ESP32 #1 (Bluetooth)
- ESP32 DevKit or similar
- Connected to power source
- Optional: Power LED indicator
- Optional: Status LED for Bluetooth connection

### ESP32 #2 (WiFi)
- ESP32 DevKit or similar with more flash memory (for web interface)
- Connected to power source
- Optional: SSD1306 OLED display for status
- Optional: microSD card for audio storage
- Optional: Control buttons or rotary encoder for volume control

## Software Architecture on ESP32 #1 (Bluetooth)

### main.c Responsibilities

**Purpose:** Bootstrap policy layer - orchestrates subsystem initialization in correct order

**Core Responsibilities:**
1. **Early boot diagnostics:** DIAG markers for test harness synchronization (EARLY_BOOT_MARKER, UART_READY_FOR_CMD_LAYER)
2. **Platform services initialization:**
   - BLE memory release (A2DP-only optimization)
   - UART driver install (single install, never delete)
   - NVS initialization (single call to `nvs_storage_init()`)
3. **Subsystem composition:**
   - Command interface (control plane)
   - Bluetooth manager (data plane)
   - Audio processor (media plane)
4. **Configuration policy:**
   - Load audio boot config (sample rate, volume, pins)
   - Check autostart flags (NVS overrides Kconfig)
   - Apply runtime customization

**What main.c IS:**
- **Bootstrap orchestrator** - "when to initialize what"
- **Policy layer** - decisions about init order, defaults, autostart
- **Diagnostic gateway** - early boot markers for test automation
- **Configuration loader** - centralized config policy (Kconfig + NVS)

**What main.c IS NOT:**
- ❌ **Not** a Bluetooth implementation (no esp_a2d_*, esp_avrc_*, esp_bt_gap_*, esp_bluedroid_* calls)
- ❌ **Not** a command processor (delegates to cmd_handlers)
- ❌ **Not** an audio engine (delegates to audio_processor)
- ❌ **Not** a state machine (delegates to bt_manager)

**Line Budget:** ~320 lines (Feb 2026) - kept small by delegating all logic to components

**Architectural Principle:** main.c is **thin orchestration** - it knows WHEN to call what, but components know HOW to do it.

### main.c Bootstrap (current size: ~319 lines)
- **Purpose:** Clean entry point for ESP32 firmware
- **Responsibilities:**
  - Early boot diagnostics and UART initialization
  - **NVS (Non-Volatile Storage) initialization** - SINGLE call to `nvs_storage_init()` early in boot
  - Memory optimization (`esp_bt_controller_mem_release` for BLE)
  - Delegate ALL Bluetooth initialization to `bt_manager`
  - Initialize command interface and audio processor
  - Auto-configure I2S pins from NVS storage
  - Load audio boot config with Kconfig defaults + NVS overrides
  - Check autostart flag before initializing audio
- **What main.c MUST NOT contain:**
  - Direct Bluetooth API calls (esp_a2d_*, esp_avrc_*, esp_bt_gap_*, esp_bluedroid_*)
  - Bluetooth state machines or callbacks
  - Device-specific BT logic
  - Command processing logic (delegates to cmd_handlers)
  - Audio pipeline logic (delegates to audio_processor)
- **Policy:** ALL Bluetooth logic lives in the `bt_manager` component (enforced by CI)

### bt_manager Component (Single Source of Truth for BT)
- **Purpose:** Centralized Bluetooth lifecycle management
- **Responsibilities:**
  - Initialize Bluetooth controller and Bluedroid stack
  - Register ALL Bluetooth callbacks (A2DP, AVRCP, GAP)
  - Manage Bluetooth state machines (connection, pairing, streaming)
  - Handle device discovery and pairing
  - Coordinate with audio_processor for streaming
- **Sub-components:**
  - A2DP Source profile implementation
  - AVRCP profile for remote control
  - GAP for device discovery and pairing
  - Connection manager for device lifecycle
  - Streaming manager for audio data flow

### Audio Processor Component
- **Purpose:** Audio pipeline orchestration with lossless WAV playback guarantees
- **Responsibilities:**
  - I2S slave configuration and management
  - Audio buffer management and ring buffer
  - Audio queue for multiple sources (I2S, WAV, beep, synth)
  - Coordinate with bt_manager for A2DP streaming
  - Generate keepalive tones and beeps
  - **WAV file playback from SPIFFS with zero data loss guarantee**

#### ⚠️ OBSOLETE: WAV Playback Lossless Architecture (CODE_REVIEW4 Phase 1)

> **This section is obsolete as of Version 0.3.0 (February 2026).**  
> WAV playback, play_manager, and SPIFFS support were removed. This content is retained for historical reference only.  
> See MIGRATION.md for details.

The audio processor implements a robust WAV playback system with **lossless guarantees** even under memory pressure or queue backpressure conditions. This architecture prevents audio truncation and ensures complete file playback.

**Core Data Loss Prevention Mechanisms:**

1. **File Rewind on Enqueue Failure (play_manager.c)**
   - When `audio_chunk_enqueue_block()` fails due to queue full or out-of-memory, the file pointer is rewound to the last successful position
   - Bytes are re-read on the next fill iteration, ensuring zero data loss
   - Instrumentation tracks retry attempts via `s_enqueue_fail_count`
   - **Guarantee:** No bytes are lost when enqueue operations fail; playback automatically retries

2. **Frame Boundary Alignment (play_manager.c)**
   - All reads are aligned to audio frame boundaries (e.g., 4 bytes for stereo 16-bit)
   - Two-step alignment: (1) clamp to remaining bytes, (2) align down to frame size
   - Prevents partial frames that would cause glitches, corruption, or misaligned samples
   - **Guarantee:** Every chunk delivered to Bluetooth contains only complete audio frames

3. **WAV Chunk Padding Handling (play_manager.c)**
   - WAV/RIFF specification requires word-alignment (even byte offsets)
   - Odd-sized chunks have 1 padding byte that must be skipped: `skip = chunk_size + (chunk_size & 1)`
   - Prevents file pointer drift that would cause chunk header misinterpretation
   - **Guarantee:** File parsing is accurate regardless of chunk sizes

4. **Residual Buffer Flush Ordering (audio_processor_read.c)**
   - Residual buffer holds leftover bytes from previous read operations
   - Early-return check MUST verify residual buffer is empty before skipping reads
   - Prevents tail truncation where final bytes are lost when sources become inactive
   - **Guarantee:** All buffered data is flushed before playback is considered complete

5. **Instrumentation and Verification (play_manager.c, Task 0.2/6.1)**
   - Tracks `s_expected_data_bytes` (from WAV header), `s_bytes_read_from_file_total`, `s_bytes_enqueued_total`
   - Public API `play_manager_get_instrumentation()` exposes counters for test verification
   - Device test `test_wav_playback_completeness()` validates: expected == bytes_read (no file errors)
   - **Guarantee:** Data loss is detectable and tracked; regression tests prevent future truncation bugs

**Queue Backpressure Handling:**

The audio queue can experience backpressure when:
- Bluetooth transmission is slower than audio generation
- Queue fills up faster than A2DP can drain it
- Temporary memory pressure prevents new chunk allocation

**Backpressure Response Strategy:**
1. **Enqueue Blocking:** `audio_chunk_enqueue_block()` waits up to 1 second for queue space
2. **Automatic Retry:** If enqueue fails after timeout, file pointer rewinds and retry occurs on next fill
3. **Transparent to Caller:** play_manager handles retry internally; caller sees eventual success
4. **No Data Loss:** Retry mechanism ensures all bytes are eventually queued (unless file read errors occur)
5. **Graceful Degradation:** Under sustained backpressure, playback may slow but will not truncate

**Error Propagation:**
- File read errors: Propagated to caller as `ESP_ERR_INVALID_STATE`
- Enqueue failures: Returned as `ESP_ERR_NO_MEM` after retry exhausted (caller can retry later)
- All errors logged with `ESP_LOGE` for diagnostics

**Correctness Invariants:**
- `s_bytes_read_from_file_total == s_expected_data_bytes` when playback completes successfully
- Data loss percentage = 0% for functioning subsystems
- Retry operations are transparent and do not alter audio content
- Frame alignment is maintained across all chunk boundaries

**Testing:**
- **Host tests:** Verify data flow through audio_queue under normal conditions
- **Device tests:** `test_wav_playback_completeness()` validates complete file drainage and instrumentation accuracy
- **Stress tests:** Validate behavior under memory pressure and queue backpressure (manual testing)

**Benefits:**
- **Lossless playback:** Complete audio files play without truncation or glitches
- **Robust under pressure:** Handles queue full and OOM conditions gracefully
- **Verifiable:** Instrumentation allows automated regression testing
- **Maintainable:** Clear separation of concerns (file I/O, alignment, queueing, error handling)

#### Stateful Streaming Resampler Architecture (CODE_REVIEW5 Phase 1)

The audio processor implements a **stateful streaming resampler** that eliminates cumulative frame loss during sample rate conversion. This architecture replaces the previous block-local resampling that suffered from rounding errors accumulating over multiple blocks.

**Problem Statement:**

Previous resampler implementation:
- Processed audio in fixed 1024-byte blocks independently
- Used block-local `floor()` for output frame calculation
- Lost ~0.64 frames per block for 44.1kHz→48kHz upsampling
- Cumulative loss: ~55 frames (1.15ms) per 500ms WAV file
- Scaled linearly: 10-second files would lose ~23ms of audio
- Caused audible "ends early" behavior on longer playback

**Solution Architecture:**

**1. Fixed-Output, Variable-Input Pipeline**

The streaming resampler inverts the traditional processing model:
- **Output:** Always produces exactly 256 frames (1024 bytes) per block
- **Input:** Reads variable number of frames based on resampling ratio
- **Benefit:** Predictable output size simplifies downstream queue management

**Pipeline Flow:**
```
WAV File → PCM Stash → Streaming Resampler → Fixed 1KB Blocks → Audio Queue
         (variable)   (phase-preserving)      (constant size)
```

**2. Q16.16 Fixed-Point Phase Accumulator**

Core resampling algorithm:
- **Phase tracking:** 32-bit Q16.16 fixed-point position (16 integer bits, 16 fractional bits)
- **Step calculation:** `step_q16 = (src_rate << 16) / dst_rate`
- **Phase carry:** Fractional position carries across block boundaries
- **No rounding loss:** Cumulative error eliminated by preserving phase state

Example (44.1kHz → 48kHz):
```c
step_q16 = (44100 << 16) / 48000 = 60293 (0.91875 in Q16.16)
// For each output frame:
pos_q16 += step_q16;  // Position advances by 0.91875 input frames
in_idx = pos_q16 >> 16;  // Integer part = input frame index
frac = pos_q16 & 0xFFFF;  // Fractional part for interpolation
```

**3. Linear Interpolation**

Sample interpolation between adjacent input frames:
```c
// For stereo 16-bit:
left_sample = ((0x10000 - frac) * in[i0].left + frac * in[i0+1].left) >> 16;
right_sample = ((0x10000 - frac) * in[i0].right + frac * in[i0+1].right) >> 16;
```

- Smooth transitions between samples (prevents aliasing)
- Simple implementation (no division, only multiplication + shift)
- Sufficient quality for audio playback (higher-order filters not needed for this use case)

**4. PCM Stash Buffer**

Input buffer decouples file reads from resampling:
- **Purpose:** Accumulate variable input frames needed for fixed output
- **Capacity:** 2048 frames (~8KB for stereo 16-bit)
- **Operations:**
  - `pcm_stash_append_frames()`: Add converted frames from WAV file
  - `pcm_stash_consume_frames()`: Remove frames consumed by resampler
  - `pcm_stash_free_frames()`: Query available space

**Stash Flow:**
```
1. Resampler calculates: "I need ≥N input frames for 256 output frames"
2. ensure_stash_frames(N): Reads from WAV until stash has ≥N frames
3. Resampler processes: Consumes exactly M frames, produces 256 frames
4. Stash updated: memmove() to shift unconsumed frames to buffer start
```

**5. Variable Input Frame Calculation**

Minimum input frames required for N output frames:
```c
size_t audio_resampler_stream_min_in_frames(const audio_resampler_stream_t *rs, size_t out_frames) {
    // Account for current phase position
    uint32_t next_pos = rs->pos_q16 + (rs->step_q16 * out_frames);
    size_t in_frames = (next_pos >> 16) + 1;  // +1 for interpolation buffer
    return in_frames;
}
```

Example (44.1kHz → 48kHz, 256 output frames):
- Step: 0.91875 input frames per output frame
- Total position advance: 256 × 0.91875 = 235.2 frames
- Minimum input needed: 236 frames (⌈235.2⌉ + 1 for interpolation)

**6. EOF Handling with Zero-Padding**

When input exhausted before reaching desired output count:
- Resampler produces exactly 256 output frames (never short blocks)
- Remaining output filled with silence (zero samples)
- Ensures consistent block size for audio queue
- No glitches or pops at end of file

**Implementation Files:**

1. **audio_resampler_stream.h/c** (new module)
   - `audio_resampler_stream_init()`: Initialize resampler state, compute step_q16
   - `audio_resampler_stream_min_in_frames()`: Calculate input frames needed
   - `audio_resampler_stream_process()`: Perform resampling with phase carry

2. **play_manager.c** (PCM stash + integration)
   - `pcm_stash_t`: Input buffer structure
   - `pcm_stash_*()`: Buffer management functions
   - `ensure_stash_frames()`: Read from WAV to fill stash
   - `produce_one_output_block()`: Orchestrate stash → resampler → queue
   - `play_manager_fill()`: Main loop producing fixed 1KB blocks

**Correctness Guarantees:**

1. **Frame accuracy:** Q16.16 accumulator prevents cumulative rounding loss
2. **Exact ratio:** Total output frames = ⌊input frames × (dst_rate / src_rate)⌋
3. **Lossless:** All input frames consumed (verified by instrumentation)
4. **Smooth interpolation:** No aliasing or glitches from sample rate conversion
5. **Predictable output:** Always produces 256-frame blocks (simplifies queue management)

**Validation:**

- **Unit tests (19 tests):** Verify step_q16 calculation, min_in_frames accuracy, phase carry, frame ratios
- **Device test (baseline):** 500ms WAV plays in 500ms (0ms error, 1.0000 frame ratio)
- **Frame instrumentation:** Completion report shows 0.00% frame loss, "Duration accuracy: EXCELLENT"
- **Stress test:** Queue backpressure test validates lossless behavior under slow consumption

**Performance:**

- **Binary size:** +4224 bytes (streaming resampler + stash buffer + instrumentation)
- **Heap usage:** ~8KB for PCM stash (allocated on WAV start, freed on stop)
- **CPU overhead:** Minimal (linear interpolation, no division, simple fixed-point math)
- **Latency:** <1ms added latency from stash buffering (negligible for audio playback)

**Benefits:**

- **Mathematically correct:** No cumulative frame loss over any playback duration
- **Verifiable:** Instrumentation tracks exact frame counts (src vs dst vs expected)
- **Robust:** Handles any sample rate pair (upsampling, downsampling, no-op)
- **Maintainable:** Clear separation of concerns (file I/O, stash, resampling, queue)
- **Testable:** Fast host tests validate correctness without hardware

#### Ring Buffer Architecture (CODE_REVIEW6 Phase 1-3)

The audio processor implements a **Single Producer Single Consumer (SPSC) ring buffer** that replaced the previous multi-producer queue architecture. This eliminates race conditions, simplifies reasoning about correctness, and provides natural backpressure via watermarks.

**Problem Statement (CODE_REVIEW6):**

Previous queue-based architecture suffered from:
- **P0-C:** I2S truncation - only first 1KB of 8KB buffer enqueued (massive audio loss)
- **P1-D:** Backpressure drops audio - WAV/I2S/beep could lose chunks when queue full
- **P2-E:** Multiple producers + queue complexity - race conditions, hard to reason about
- **Memory fragmentation:** Block pool allocations/deallocations caused heap fragmentation
- **Enqueue failures:** Queue full conditions required complex retry logic

**Solution Architecture:**

**1. SPSC Ring Buffer Design**

Core properties:
- **Single Producer:** Audio engine task (only this task writes to ring)
- **Single Consumer:** A2DP callback (audio_processor_read())
- **Lock-free:** Uses `portENTER_CRITICAL` only for very short sections
- **Never blocks:** Write/read return available bytes, never wait
- **Wrap-around safe:** Handles buffer boundary correctly

Ring buffer structure:
```c
typedef struct {
    uint8_t *buffer;        // Circular buffer (heap or PSRAM)
    size_t   capacity;      // Total buffer size (configurable, default 32KB)
    size_t   head;          // Write position (producer)
    size_t   tail;          // Read position (consumer)
    size_t   used_bytes;    // Current occupancy (eliminates full/empty ambiguity)
    size_t   peak_used;     // Peak occupancy for diagnostics
} audio_rb_t;
```

API:
```c
// Init/deinit (called by audio_processor)
esp_err_t audio_rb_init(audio_rb_t **rb, size_t capacity, bool use_psram);
void      audio_rb_deinit(audio_rb_t *rb);

// Producer API (audio_engine_task only)
size_t    audio_rb_write(audio_rb_t *rb, const uint8_t *src, size_t len);

// Consumer API (A2DP callback only)
size_t    audio_rb_read(audio_rb_t *rb, uint8_t *dst, size_t len);

// Query API (non-destructive, safe from any context)
size_t    audio_rb_available_to_read(const audio_rb_t *rb);
size_t    audio_rb_available_to_write(const audio_rb_t *rb);
size_t    audio_rb_capacity(const audio_rb_t *rb);
size_t    audio_rb_peak_used(const audio_rb_t *rb);
```

**2. Audio Engine Task (Single Producer)**

Core responsibilities:
- **Source arbitration:** Decide which source is active (WAV → I2S → Synth → Silence)
- **Audio production:** Call active source's fill() API to generate audio
- **Beep overlay:** Mix beep over base source when beep active
- **Ring buffer write:** Write produced audio to ring (1KB chunks)
- **Watermark management:** Pause at high watermark, resume at low watermark

Task structure:
```c
static void audio_engine_task(void *arg) {
    const TickType_t tick_ms = pdMS_TO_TICKS(2);  // 2ms tick
    const size_t chunk_bytes = 1024;
    uint8_t *chunk_buf = heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);
    
    for (;;) {
        // Check watermarks (hysteresis)
        size_t used = capacity - audio_rb_available_to_write(ring);
        if (used >= HIGH_WATERMARK) engine_paused = true;
        if (used <= LOW_WATERMARK) engine_paused = false;
        
        if (!engine_paused) {
            // Produce audio from active source
            size_t produced = produce_audio_chunk(chunk_buf, chunk_bytes);
            
            if (produced > 0) {
                size_t written = audio_rb_write(ring, chunk_buf, produced);
                // Update stats, track per-source bytes
            }
        }
        
        vTaskDelay(tick_ms);
    }
}
```

Watermarks (configurable, 32KB ring):
- **Low watermark:** 8KB (resume filling)
- **High watermark:** 24KB (pause filling)
- **Hysteresis:** Prevents thrashing (pause/resume cycling)

**3. Source Fill APIs (Producers → Ring)**

Each audio source implements a standardized fill() API:

```c
// WAV playback (play_manager.c)
size_t wav_source_fill(uint8_t *dst, size_t dst_bytes);
bool   wav_source_is_active(void);

// I2S capture (i2s_manager.c)
size_t i2s_source_fill(uint8_t *dst, size_t dst_bytes);
bool   i2s_source_is_active(void);

// Synthesizer (synth_manager.c)
size_t synth_source_fill(uint8_t *dst, size_t dst_bytes);
bool   synth_manager_is_active(void);

// Beep overlay (beep_manager.c)
void   beep_overlay_fill(uint8_t *buffer, size_t bytes);  // Mix in-place
bool   beep_manager_is_active(void);
```

Contract:
- **Non-blocking:** Return immediately with available bytes (0 if none)
- **Exact or less:** May return fewer bytes than requested (EOF, underrun)
- **Frame-aligned:** Always return complete audio frames
- **Thread-safe:** Can be called from audio engine task safely

Source selection priority (audio_engine_task):
1. **WAV:** Highest priority (file playback)
2. **I2S:** Second (microphone/line-in capture)
3. **Synth:** Third (tone generator / keepalive)
4. **Silence:** Fallback (zero-fill when no source active)

**4. Audio Processor Read (Single Consumer)**

A2DP callback reads directly from ring buffer:

```c
esp_err_t audio_processor_read(uint8_t *buffer, size_t size, size_t *bytes_read) {
    // Direct non-blocking read from ring
    size_t read = audio_rb_read(s_audio_ring, buffer, size);
    
    if (read < size) {
        // Underrun - zero-fill remainder
        memset(buffer + read, 0, size - read);
        s_stats.buffer_underruns++;
        s_stats.underrun_bytes += (size - read);
    }
    
    s_stats.bytes_read += size;
    *bytes_read = size;  // Always return full size (zero-filled if needed)
    
    return ESP_OK;
}
```

Properties:
- **Never blocks:** Critical for A2DP callback timing
- **Zero-fill underruns:** Prevents glitches from stale data
- **Tracks stats:** Underrun count/bytes for diagnostics

**5. Beep Mixing Architecture**

Beep is an **overlay**, not a separate source:

```c
void beep_overlay_fill(uint8_t *buffer, size_t bytes) {
    // Generate beep samples
    int16_t beep_sample = generate_sine_sample(phase, frequency);
    
    // Mix into existing buffer (16-bit stereo)
    for (size_t i = 0; i < frames; i++) {
        int32_t base_l = ((int16_t*)buffer)[i*2];
        int32_t base_r = ((int16_t*)buffer)[i*2+1];
        
        // Attenuate base (70%), add beep (50%)
        base_l = clamp_int16((base_l * 7 / 10) + (beep_sample * 5 / 10));
        base_r = clamp_int16((base_r * 7 / 10) + (beep_sample * 5 / 10));
        
        ((int16_t*)buffer)[i*2] = base_l;
        ((int16_t*)buffer)[i*2+1] = base_r;
    }
    
    // Update beep state (phase, remaining duration)
}
```

Benefits:
- **No separate chunks:** Beep mixes over base audio (WAV/I2S/synth)
- **No timing issues:** Beep always plays immediately (no queue delays)
- **Smooth mixing:** Saturating add prevents clipping

**6. Metadata Span Log (Debug Only)**

Append-only history of ring buffer writes:

```c
typedef struct {
    uint32_t seq;              // Monotonic sequence
    uint32_t timestamp_ms;     // When written
    size_t   bytes;            // Bytes written
    uint16_t ring_used_kb;     // Ring occupancy after write (KB)
    uint8_t  source;           // AUDIO_SOURCE_WAV/I2S/etc
    uint8_t  flags;            // BEEP_OVERLAY, etc
} audio_rb_span_t;
```

Properties:
- **Not position-coupled:** Just historical log (no ties to ring positions)
- **Wrap-safe:** Small ring buffer (256 entries, ~4KB)
- **Thread-safe:** portENTER_CRITICAL for push operations
- **Query API:** span_log_get_last_n() for debugging

Use cases:
- Timeline debugging (when did source switch?)
- Beep overlay detection (which chunks had beep mixed?)
- Ring occupancy trends (watermark behavior validation)

**7. Statistics and Telemetry**

Always-on stats (audio_stats_t):
```c
// Per-source byte counts (Phase 4.2)
uint64_t bytes_by_source[4];  // [WAV, I2S, SYNTH, SILENCE]
uint32_t source_switch_count; // Times active source changed
uint32_t beep_overlay_count;  // Times beep was overlaid
uint64_t beep_overlay_bytes;  // Total bytes with beep mixed

// Ring buffer stats
size_t   ring_peak_used;      // Peak occupancy (bytes)
uint32_t engine_write_calls;  // audio_rb_write() calls
uint64_t engine_write_bytes;  // Total bytes written to ring
uint32_t engine_pause_count;  // Times paused due to watermark

// Consumer stats
uint64_t bytes_read;          // Total bytes consumed
uint32_t buffer_underruns;    // Times ring was empty
uint64_t underrun_bytes;      // Total zero-filled bytes
```

AUDIO_STATUS command output:
```
OK|AUDIO_STATUS|CURRENT|
RING_CAP=32768,RING_USED=8192,RING_FREE=24576,RING_PEAK=28000,
SOURCE=WAV,BEEP=no,
UNDERRUNS=5,UNDERRUN_BYTES=1280,
WAV_BYTES=1234567,I2S_BYTES=0,SYNTH_BYTES=0,SILENCE_BYTES=45000,
SOURCE_SWITCHES=3,BEEP_OVERLAYS=10,BEEP_BYTES=5120,
ENGINE_WRITES=1500,ENGINE_BYTES=1536000,ENGINE_PAUSES=2
```

**Implementation Files:**

1. **audio_ringbuffer.h/c** (new module)
   - SPSC ring buffer implementation
   - Lock-free read/write with critical sections
   - Capacity management and peak tracking

2. **audio_span_log.h/c** (new module, debug only)
   - Span entry append-only ring buffer
   - Query API for last N spans
   - Thread-safe push operations

3. **audio_processor.c** (modified)
   - audio_engine_task() - single producer task
   - produce_audio_chunk() - source selection + beep mixing
   - Watermark management (pause/resume logic)
   - Stats tracking (per-source, ring, underruns)

4. **audio_processor_read.c** (modified)
   - audio_processor_read() now reads from ring (not queue)
   - Zero-fill underruns
   - Removed queue dequeue logic

5. **play_manager.c** (modified)
   - wav_source_fill() - fill buffer from WAV (no enqueue)
   - Keeps streaming resampler + stash (unchanged)

6. **i2s_manager.c** (modified)
   - i2s_source_fill() - fill buffer from I2S (no enqueue)
   - Keeps format conversion + resampling (unchanged)

7. **beep_manager.c** (modified)
   - beep_overlay_fill() - mix beep into existing buffer
   - In-place saturating add mixing

8. **synth_manager.c** (modified)
   - synth_source_fill() - generate synth audio
   - Direct buffer fill (no queue)

**Correctness Guarantees:**

1. **No data loss:** Ring buffer never drops bytes (watermarks prevent overflow)
2. **No races:** SPSC design eliminates multi-producer contention
3. **No truncation:** I2S fills entire buffer (P0-C fixed by design)
4. **No backpressure loss:** Watermarks pause engine cleanly (P1-D fixed)
5. **Underrun handling:** Zero-fill prevents glitches (A2DP callback safe)

**Testing:**

- **Unit tests (20 tests):** Ring buffer wrap-around, edge cases, stress tests
- **Stress tests (7 tests):**
  - Ring buffer: 10,000 random I/O, 1,000 fill-drain cycles, 5,000 asymmetric ops
  - Audio engine: rapid source switching, concurrent beeps, watermark behavior
- **Integration tests (33 tests via test_app_audio):** Real pipeline validation
- **Regression tests (461 total):** All previous tests pass (no regressions)

**Validation Results (2026-02-05):**

- Host tests: 295/295 passed (39 binaries)
- Device tests: 166/166 passed (9 suites)
- Stress tests: All 7 passing (no crashes, invariants maintained)
- Total: 461/461 tests passing (100% success rate)
- Linting: clang-tidy clean (0 issues)

**Performance:**

- **Binary size:** +8KB (ring buffer + span log + audio engine task)
- **Heap usage:** 32KB ring buffer (default, configurable 8-256KB)
- **PSRAM option:** 128KB ring for 667ms buffer (optional, Kconfig)
- **CPU overhead:** <2% for audio engine task (2ms tick)
- **Latency:** 167ms @ 32KB buffer (acceptable for Bluetooth streaming)

**Benefits Over Previous Queue Architecture:**

1. **Simpler:** SPSC eliminates complex multi-producer synchronization
2. **Safer:** No block pool allocations (no heap fragmentation)
3. **More robust:** Watermarks provide natural backpressure
4. **More diagnosable:** Stats + span log provide full visibility
5. **Better performance:** No allocation/deallocation overhead
6. **Easier to reason about:** Single writer, single reader, clear ownership

**Migration Path:**

The ring buffer architecture was implemented in phases:
- **Phase 0:** Fixed critical bugs (mono→stereo, EOF, I2S truncation)
- **Phase 1:** Implemented ring buffer module + tests
- **Phase 2:** Created audio engine task + watermarks
- **Phase 3:** Refactored sources to fill() APIs
- **Phase 4:** Added metadata span log + stats
- **Phase 5:** Comprehensive stress testing + validation

Old queue-based code remains in parallel (not yet removed) for rollback safety, but is not used in the active audio path. Final cleanup (audio_queue removal) is deferred to Phase 6.

### Command Interface Component
- **Purpose:** UART-based control protocol
- **Responsibilities:**
  - Command parser and dispatcher
  - Status reporting to ESP32 #2
  - Error handling and validation
  - Command processing task (polls every 20ms)

## Initialization Ownership and Layering

### NVS (Non-Volatile Storage) Ownership
**Decision:** main.c owns NVS initialization (Option A)

**Rationale:**
- NVS is a **platform service** (like memory, filesystems) - it should be initialized once at boot
- Multiple components use NVS (bt_manager, audio_processor, nvs_storage abstraction)
- **Single ownership** prevents redundant init calls and race conditions
- Early initialization ensures NVS is ready before any component needs it
- Follows ESP-IDF best practice: platform services initialized in app_main()

**Implementation:**
- main.c calls `nvs_storage_init()` **once** early in app_main() (after BLE mem release, before BT init)
- `nvs_storage_init()` handles version mismatch and erase-on-error internally
- bt_manager, audio_processor, and other components **assume NVS is already initialized**
- Components call nvs_storage_get/set_* functions directly without re-initializing

**What NOT to do:**
- ❌ bt_manager must NOT call `nvs_flash_init()` or `nvs_storage_init()`
- ❌ No component should redundantly initialize NVS "just in case"
- ❌ No lazy initialization - NVS must be ready before subsystems start

**Benefits:**
- Clear ownership (main.c owns platform, components own logic)
- Prevents duplicate initialization
- Easier to reason about boot sequence
- Supports future two-ESP32 architecture (each ESP32's main.c initializes its own NVS)

### UART (Console) Ownership
**Decision:** Option C - Split ownership: main.c installs early for diagnostics, cmd_init assumes ready

**Rationale:**
- Early boot diagnostics are **critical** for programmatic test harness and host injectors
- Diagnostic markers (DIAG|BOOT|EARLY_BOOT_MARKER, DIAG|BOOT|UART_READY_FOR_CMD_LAYER) must appear before subsystem init
- printf() and esp_rom_printf() alone are **insufficient** - they are buffered/unreliable for host capture
- UART driver installation is required for unbuffered uart_write_bytes() diagnostic output
- cmd_init() and other components need UART already operational for synchronous I/O
- **Single install** at boot avoids driver reinstall complexity and state confusion

**Implementation:**
- main.c installs UART driver **once** early in app_main() (after early boot markers, before NVS)
- Installation is **best-effort** with error checking but continues on failure
- main.c uses uart_write_bytes() for critical diagnostic markers before subsystems init
- cmd_init() and command_interface **assume UART is already installed** - no reinstall
- command_interface checks `uart_is_driver_installed()` before writes (defensive but not required)

**What NOT to do:**
- ❌ main.c must NOT call `uart_driver_delete()` after install (breaks logging, esp-console, cmd layer)
- ❌ cmd_init() must NOT reinstall UART driver (causes state reset, double-init)
- ❌ No component should assume UART is uninstalled and try to install it

**Boot sequence:**
1. Very early: printf/esp_rom_printf for EARLY_BOOT_MARKER (before driver)
2. Early: main.c installs UART driver for unbuffered diagnostics
3. Early: main.c writes UART_READY_FOR_CMD_LAYER via uart_write_bytes()
4. Platform init: NVS, BT manager (all emit diagnostic markers)
5. cmd_init: command interface ready (assumes UART operational)

**Benefits:**
- Early diagnostics reliable and visible to test harness
- UART installed exactly once - no reinstall complexity
- cmd_init can immediately read/write without driver setup
- Clear contract: main.c owns platform UART, cmd_init owns command protocol
- Supports test injection and automated capture from first boot moment

### Initialization Order and Rationale

**Actual init sequence (as of Jan 2026, Phase 2 Task 2.6):**
```
1. Early diagnostics (UART install)
2. NVS init (platform service)
3. CMD init (control plane ready)
4. CMD task start (command interface processing)
5. BT manager init (data plane ready - NOW commands work!)
6. Audio init/start (if autostart enabled)
```

**Key Architectural Principle: Control Plane → Data Plane**

**Why CMD before BT?**
- **Control plane availability:** CMD interface must be ready BEFORE subsystems initialize
- **Command interface ready early:** Allows immediate SCAN/PAIR commands when BT becomes ready
- **Prevents confusion:** BT "ready" with no way to control it is misleading
- **Separation of concerns:** Communication infrastructure (CMD) separate from business logic (BT, audio)
- **Test harness friendly:** Commands available from earliest possible moment
- **Human debugging:** If BT init fails, commands still work for diagnostics

**Why NVS before everything?**
- **Platform dependency:** ALL subsystems need NVS (BT pairing data, audio config, command settings)
- **Fail-fast on critical errors:** NVS failure indicates corrupted flash/partition - nothing will work anyway
- **Single initialization:** Avoids race conditions, prevents redundant init calls
- **Configuration loading:** BT and audio need NVS data during their init

**Why Audio last?**
- **Optional subsystem:** Audio can be disabled via autostart flag (BT/CMD remain functional)
- **Resource intensive:** DMA buffers, GPIO pins, interrupts - only allocate if needed
- **Depends on BT:** Streaming audio requires BT connection (though audio can work standalone for test tones)
- **Deployment flexibility:** Headless mode (no audio), diagnostic mode (BT + CMD only), full mode

**Init Order Contradiction Fixed (Phase 2, Task 2.6):**
- **OLD (incorrect):** NVS → **BT** → CMD → Audio
  - Problem: Comment said "BT ready for SCAN/PAIR via commands" but CMD not ready yet - CONFUSING
  - Impact: Misleading state, potential race conditions if BT events trigger before CMD ready
- **NEW (correct):** NVS → **CMD** → BT → Audio
  - Benefit: Control plane available before data plane, clear layering, no race conditions

**Error Handling Philosophy:**
- **Platform services (NVS, UART):** Fail-fast with ESP_ERROR_CHECK
  - Rationale: System cannot operate without these - partial state is worse than clean abort
- **Subsystems (BT, Audio, CMD):** Graceful degradation with error logging
  - Rationale: Device still useful for diagnostics, testing, partial functionality
  - Example: BT fails → audio test tones still work, CMD interface available for diagnosis
  - Example: CMD fails → device continues boot, BT/Audio may still function for auto-connect scenarios

### Policy vs Platform Separation

**Platform Layer (owned by main.c):**
- **What:** Core ESP32 services that ALL components depend on
- **Responsibilities:**
  - Memory management (heap, PSRAM, BLE mem release)
  - Flash storage (NVS initialization)
  - Console I/O (UART driver install)
  - Early diagnostics (DIAG markers for test automation)
- **Characteristics:**
  - Initialized ONCE at boot
  - Fail-fast on errors (ESP_ERROR_CHECK)
  - No retry logic (hardware/partition issues don't self-heal)
  - Owned by main.c app_main()
- **Examples:**
  - `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` - platform memory optimization
  - `uart_driver_install()` - platform I/O service
  - `nvs_storage_init()` - platform persistence service

**Policy Layer (orchestrated by main.c):**
- **What:** Business decisions about WHEN and HOW to initialize subsystems
- **Responsibilities:**
  - Init order sequencing (control plane → data plane)
  - Configuration loading (Kconfig defaults + NVS overrides)
  - Autostart decisions (should audio start at boot?)
  - Resource allocation defaults (sample rate, volume, pins)
- **Characteristics:**
  - Configurable at compile-time (Kconfig) and runtime (NVS)
  - Documents architecture intent (WHY this order?)
  - Thin orchestration (delegates HOW to components)
  - Owned by main.c app_main()
- **Examples:**
  - `load_audio_boot_config()` - centralized audio policy
  - `nvs_storage_get_audio_autostart()` - runtime policy check
  - CMD before BT init - architectural policy decision

**Application Layer (implemented by components):**
- **What:** Actual business logic and subsystem implementations
- **Responsibilities:**
  - Bluetooth lifecycle (bt_manager)
  - Audio pipeline (audio_processor)
  - Command protocol (cmd_handlers)
  - Device-specific logic
- **Characteristics:**
  - Stateful (maintains internal state machines)
  - Complex (hundreds of lines per component)
  - Testable in isolation (unit tests, mocked dependencies)
  - Assumes platform services are ready
  - Graceful degradation on errors (log + continue)
- **Examples:**
  - `bt_manager_init()` - BT stack initialization, state machines, callbacks
  - `audio_processor_init()` - I2S config, DMA buffers, audio queues
  - `cmd_init()` - command parser, dispatcher, task spawning

**Why This Separation Matters:**
1. **Clarity:** Each layer has clear responsibilities - no "God objects"
2. **Testability:** Application layer can be tested with mocked platform
3. **Portability:** Platform layer is ESP32-specific, application layer could be ported
4. **Maintainability:** New developers understand what goes where
5. **Future architecture:** Two-ESP32 split will have separate platform layers, shared application logic
6. **Prevents drift:** Clear rules prevent mixing platform and application code in main.c

**Anti-pattern to Avoid:**
- ❌ **Mixing layers in main.c:**
  - DO NOT put Bluetooth state machines in main.c (application logic → bt_manager)
  - DO NOT put command parsing in main.c (application logic → cmd_handlers)
  - DO NOT put I2S management in main.c (application logic → audio_processor)
- ❌ **Platform calls in application components:**
  - DO NOT call `nvs_flash_init()` in bt_manager (platform → main.c owns)
  - DO NOT reinstall UART in cmd_init (platform → main.c owns)
- ❌ **Policy decisions in platform code:**
  - DO NOT hard-code audio defaults in audio_processor (policy → main.c config)
  - DO NOT decide init order in components (policy → main.c orchestration)

**Enforcement:**
- Code reviews check for layering violations
- CI checks prevent direct BT API calls in main.c
- Comments in main.c explain WHY each init step happens
- ARCH.md documents the separation for new contributors



## Software Architecture on ESP32 #2 (WiFi)

### WiFi Core
- Access Point or client mode
- Connection management
- Network security

### Web Server
- HTML/CSS/JS interface
- Control endpoints (REST API)
- WebSocket for real-time updates

### Audio Processing
- Audio generation or streaming
- I2S master configuration
- Audio effects (optional)

### UART Command Interface
- Command generation
- Status reception
- Error handling

### User Interface
- Display driver (if display is used)
- Button/encoder handling
- User feedback (LEDs, display)

---

## Future Evolution: Single-ESP32 to Dual-ESP32 Architecture

### Current State (Single ESP32)

The current codebase runs all functionality on a single ESP32:
- **Bluetooth Classic** (A2DP source for audio streaming)
- **Command interface** (UART-based control and diagnostics)
- **Audio processor** (I2S management, WAV playback, tone generation)
- **NVS storage** (configuration persistence)

This single-ESP32 architecture is functional but has limitations:
- **Cannot run WiFi + Bluetooth Classic simultaneously** (ESP32 hardware constraint)
- **Limited CPU/memory** for advanced audio processing + network features
- **Single point of failure** - any subsystem crash affects entire device

### Future Architecture: Dual-ESP32 Split

The architecture is designed to support future migration to two cooperating ESP32 devices for enhanced capabilities:

#### Control ESP32 (Primary)
**Responsibilities:**
- **Command interface** (UART control from host or user interface)
- **Minimal Bluetooth** (device discovery, pairing, connection management only)
- **NVS storage** (configuration, pairing database)
- **Inter-ESP32 communication** (command relay, status aggregation)
- **Optional: WiFi stack** (web UI, network streaming, OTA updates)

**Components migrated from current main.c:**
- `cmd_init()` + `cmd_process_task()` - Full command layer
- `bt_manager_init()` - Connection management only (no audio streaming)
- `nvs_storage_init()` - Configuration persistence
- UART driver installation - Early diagnostics and control

**Components NOT on Control ESP32:**
- Audio processing (delegated to Audio ESP32)
- A2DP audio streaming (delegated to Audio ESP32)

#### Audio ESP32 (Secondary)
**Responsibilities:**
- **A2DP audio streaming** (high-bandwidth Bluetooth audio to speakers/headphones)
- **Audio processing** (I2S, WAV playback, tone generation, effects)
- **Real-time audio** (low-latency DMA, interrupt-driven I2S)
- **Receives commands from Control ESP32** (play, stop, volume, etc.)
- **Sends status to Control ESP32** (playback state, errors)

**Components migrated from current main.c:**
- `audio_processor_init()` + `audio_processor_start()` - Full audio stack
- `bt_manager_start_audio()` - A2DP streaming only (no pairing/discovery)
- I2S driver - Audio I/O hardware

**Components NOT on Audio ESP32:**
- User command interface (handled by Control ESP32)
- WiFi stack (if needed, runs on Control ESP32)
- Configuration storage (Control ESP32 is source of truth)

### Communication Protocol (Inter-ESP32)

#### Physical Interface
**UART** (high-speed serial, 921600 baud recommended):
```
Control ESP32          Audio ESP32
-------------          -----------
TX (GPIO17)  -------> RX (GPIO16)
RX (GPIO16)  <------- TX (GPIO17)
GND          -------> GND
```

**Optional I2S** (for audio data from Control ESP32 → Audio ESP32):
```
Control ESP32 (I2S Master)    Audio ESP32 (I2S Slave)
--------------------------    -----------------------
I2S_BCK (GPIO26)  ---------> I2S_BCK (GPIO26)
I2S_WS (GPIO25)   ---------> I2S_WS (GPIO25)
I2S_DO (GPIO22)   ---------> I2S_DI (GPIO22)
GND               ---------> GND
```

#### Command Protocol (Control → Audio ESP32)

**Format:** Same as current command interface (newline-terminated text)

**Examples:**
```
AUDIO_START\n
AUDIO_STOP\n
VOLUME 80\n
PLAY /spiffs/beep.wav\n
BEEP 440 500\n
```

**Rationale:** Reuse existing command parser on Audio ESP32 for consistency. Control ESP32 becomes a command relay.

#### Status Protocol (Audio → Control ESP32)

**Format:** Structured status messages (same as current DIAG output)

**Examples:**
```
DIAG|AUDIO|STATUS|running=1|volume=80\n
DIAG|AUDIO|PLAYBACK|file=/spiffs/beep.wav|playing=1\n
DIAG|BT|AUDIO_STATE|STARTED\n
```

**Rationale:** Control ESP32 aggregates status from Audio ESP32 and exposes to host/UI.

### Migration Path from Current Single-ESP32

The current codebase is architected to support this split with **minimal changes**:

#### Phase 1: Current State (Single ESP32)
- ✅ **Already done:** Clean layering (main.c → bt_manager/audio_processor/cmd_interface)
- ✅ **Already done:** Clear component ownership (no cross-layer violations)
- ✅ **Already done:** Command-driven architecture (all operations via cmd interface)

#### Phase 2: Preparation (Still Single ESP32)
- Create `inter_esp_comm` component (UART protocol abstraction)
- Add command relay mode to `cmd_interface` (forward vs execute locally)
- Add status aggregation to `cmd_interface` (merge local + remote status)
- **No behavioral changes** - still single ESP32, but components are relay-ready

#### Phase 3: Physical Split (Dual ESP32)
- **Control ESP32 firmware:**
  - `main.c` calls: `cmd_init()`, `bt_manager_init()` (discovery only), `nvs_storage_init()`
  - Relay audio commands to Audio ESP32 via UART
  - Aggregate status from Audio ESP32 + local BT manager
  
- **Audio ESP32 firmware:**
  - `main.c` calls: `audio_processor_init()`, `cmd_init()` (receive mode)
  - Execute audio commands locally
  - Send status updates to Control ESP32 via UART

- **Shared code:** Same component libraries (bt_manager, audio_processor, cmd_interface)
  - Configuration at build time (e.g., `CONFIG_ESP_ROLE_CONTROL` vs `CONFIG_ESP_ROLE_AUDIO`)

#### Phase 4: Enhanced Features (Post-Split)
- Add WiFi to Control ESP32 (web UI, network streaming)
- Add advanced audio processing to Audio ESP32 (effects, EQ)
- Add OTA updates via Control ESP32 (update both ESP32s)

### Why Clean Layering Matters NOW

The **current single-ESP32 architecture** enforces clear ownership:

| Component | Owner | Rationale |
|-----------|-------|-----------|
| UART driver install | `main.c` | Platform service - needed for early diagnostics |
| UART usage (read/write) | `cmd_interface` | Control plane - all commands flow through here |
| BT controller init | `bt_manager` | Subsystem - encapsulates all BT operations |
| Audio I2S init | `audio_processor` | Subsystem - encapsulates all audio operations |
| NVS init | `main.c` via `nvs_storage` | Platform service - single source of truth |

**Without this layering**, the dual-ESP32 split would require:
- ❌ Untangling spaghetti code (BT calls in main.c, UART in audio, etc.)
- ❌ Rewriting command parsing (different on each ESP32)
- ❌ Debugging ownership conflicts (who owns what hardware?)

**With clean layering**, the dual-ESP32 split is:
- ✅ **Straightforward** - Each component already knows its boundaries
- ✅ **Low-risk** - No major refactoring, just configuration changes
- ✅ **Testable** - Same components, different deployment topology

### Main.c Component Migration Table

This table shows where each current `main.c` initialization call will move in the dual-ESP32 architecture:

| Current main.c Call | Control ESP32 | Audio ESP32 | Notes |
|---------------------|---------------|-------------|-------|
| `esp_bt_controller_mem_release(BLE)` | ✅ Yes | ✅ Yes | Both need Classic BT, not BLE |
| `uart_driver_install()` | ✅ Yes | ✅ Yes | Both need UART (host control / inter-ESP32 comm) |
| `nvs_storage_init()` | ✅ Yes | ❌ No | Control ESP32 is source of truth for config |
| `cmd_init()` | ✅ Yes (host mode) | ✅ Yes (relay mode) | Control receives host cmds; Audio receives relay cmds |
| `cmd_process_task()` | ✅ Yes | ✅ Yes | Both process commands (different sources) |
| `bt_manager_init()` | ✅ Yes (discovery/pair) | ⚠️ Partial (audio only) | Split BT manager into control + audio aspects |
| `audio_processor_init()` | ❌ No | ✅ Yes | Audio ESP32 owns all audio operations |
| `audio_processor_start()` | ❌ No | ✅ Yes | Audio ESP32 owns all audio operations |
| `load_audio_boot_config()` | ⚠️ Fetch from NVS | ⚠️ Receive from Control | Control ESP32 sends config to Audio ESP32 at boot |

**Legend:**
- ✅ Yes = Component runs on this ESP32
- ❌ No = Component does NOT run on this ESP32
- ⚠️ Partial = Component is split or adapted for this ESP32

### Benefits of This Evolution Plan

1. **Preserves Investment:** Current code remains usable (shared components)
2. **Incremental Migration:** Can develop/test dual-ESP32 without breaking single-ESP32
3. **Clear Boundaries:** Each ESP32 has well-defined responsibilities
4. **Future-Proof:** Architecture supports adding more features (WiFi, advanced audio)
5. **Testable:** Can test Control ESP32 and Audio ESP32 independently with mocks

### Timeline

- **Now (Phase 1):** ✅ Clean layering complete (CODE_REVIEW2 work)
- **Q2 2026 (Phase 2):** Create `inter_esp_comm` component, add relay modes
- **Q3 2026 (Phase 3):** Physical split, dual-ESP32 firmware variants
- **Q4 2026 (Phase 4):** Enhanced features (WiFi on Control, advanced audio processing)