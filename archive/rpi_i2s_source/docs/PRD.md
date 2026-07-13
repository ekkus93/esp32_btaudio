# Raspberry Pi I2S Source — Product Requirements Document (PRD)

## 1. Purpose and Scope

**Primary Goal:** Rapid development test jig for `esp_bt_audio_source` validation using Raspberry Pi + Python.

- **Core Function:** Act as I2S master transmitter delivering 16-bit little-endian PCM, stereo, 48 kHz to `esp_bt_audio_source` via I2S slave interface.
- **Test Focus:** Generate flexible test patterns (tones, sweeps, WAV playback) for audio pipeline validation.
- **Command/Control:** Send commands to `esp_bt_audio_source` via UART and parse responses (inherits protocol from `esp_bt_audio_source`).
- **Web UI:** Lightweight Flask-based web interface for tone control, UART commands, and status monitoring.
- **Development Speed:** Prioritize rapid iteration (edit Python → run) over production features.

**Target Platform:**
- **Hardware:** Raspberry Pi 3, 4, 5, or Zero 2 W (any model with GPIO headers and I2S support)
- **OS:** Raspberry Pi OS (Debian-based, Bookworm or later recommended)
- **Python:** Python 3.9+ (system default on modern Raspberry Pi OS)
- **Network:** Connect to existing WiFi network (no AP mode, no captive portal complexity)

**Rationale:**
- **10-20x faster development** vs. ESP32-S3 (no ESP-IDF build/flash cycle)
- **Testing flexibility:** Easy test pattern generation with NumPy
- **Immediate validation:** Prove `esp_bt_audio_source` I2S slave receiver and Bluetooth pipeline work correctly
- **Parallel development:** Build this in 1-2 days while ESP32-S3 `esp_i2s_source` developed in parallel

**Out of Scope (Deferred to ESP32-S3 `esp_i2s_source`):**
- ❌ Internet radio streaming
- ❌ AP/STA WiFi mode switching
- ❌ NVS persistence
- ❌ Authentication/security
- ❌ Production deployment

---

## 2. Users and Use Cases

**Primary User:** Firmware developer validating `esp_bt_audio_source` audio pipeline.

**Use Cases:**

- **UC1:** Stream 16-bit/48 kHz stereo PCM test tone (1 kHz sine) over I2S to `esp_bt_audio_source` continuously.
- **UC2:** Generate left/right channel identification tones (1 kHz left, 440 Hz right) to verify stereo routing.
- **UC3:** Sweep frequency 20 Hz → 20 kHz to test Bluetooth codec frequency response.
- **UC4:** Play WAV file from Raspberry Pi filesystem via I2S to `esp_bt_audio_source`.
- **UC5:** Send UART commands to `esp_bt_audio_source` (SCAN, CONNECT, PLAY, VOLUME, STATUS) via web UI.
- **UC6:** Monitor `esp_bt_audio_source` status in real-time (connection state, playback state, volume).
- **UC7:** Adjust tone frequency/amplitude/stereo balance on-the-fly via web controls.
- **UC8:** Quick start/stop audio to test Bluetooth connection stability.

---

## 3. Functional Requirements

### 3.1. I2S Master Transmitter (Core)

- **FR1:** Initialize I2S hardware as **master transmitter** (clock generator) using Raspberry Pi I2S peripheral.
  - Output format: **48 kHz, 16-bit, stereo, little-endian PCM**
  - GPIO pins (BCM numbering):
    - BCM GPIO18 = I2S BCLK (bit clock, output)
    - BCM GPIO19 = I2S WS (word select / LR clock, output)
    - BCM GPIO21 = I2S DOUT (data out, output)
  - Implementation: Use **`pigpio`** library for hardware-timed I2S output or Linux ALSA `snd_bcm2835` driver.
  
- **FR2:** Continuously stream PCM samples to I2S DMA buffer with minimal latency (<10 ms buffer refill).
  
- **FR3:** Handle I2S underruns gracefully: zero-fill, log warning, increment telemetry counter.

### 3.2. Audio Source Options

- **FR4:** Tone Generator (primary test mode):
  - Generate sine wave tones at configurable frequency (20 Hz – 20 kHz, default 1 kHz)
  - Configurable amplitude (0-100%, default 75%)
  - Stereo modes:
    - Mono (both channels identical)
    - Left-only (right channel silent)
    - Right-only (left channel silent)
    - Dual-tone (different frequency per channel, e.g., 1 kHz left / 440 Hz right)
  - Phase-locked generation (no clicks/pops when changing parameters)
  
- **FR5:** Frequency Sweep Generator:
  - Logarithmic sweep: 20 Hz → 20 kHz over configurable duration (5s, 10s, 30s, 60s)
  - Linear sweep option (for comparison)
  - Loop or one-shot mode
  
- **FR6:** WAV File Playback:
  - Read 16-bit/48 kHz stereo WAV files from `/home/pi/audio/` directory
  - Auto-resample if WAV file sample rate ≠ 48 kHz (use `scipy.signal.resample` or `librosa`)
  - Web UI file selector showing available WAV files
  
- **FR7:** Silence Mode:
  - Output zeros (digital silence) while I2S remains active (for connection stability testing)

### 3.3. UART Command Interface

- **FR8:** UART Physical Interface to `esp_bt_audio_source`:
  - **Baud rate:** 115200, 8N1
  - **GPIO pins (BCM numbering):**
    - BCM GPIO14 (TXD0) = UART TX → connects to `esp_bt_audio_source` RX (GPIO17)
    - BCM GPIO15 (RXD0) = UART RX ← connects from `esp_bt_audio_source` TX (GPIO16)
    - Common ground required
  - **Wiring diagram:**
    ```
    RPi (rpi_i2s_source)        ESP32 (esp_bt_audio_source)
    GPIO14 (TX)  ───────────────> GPIO17 (RX)
    GPIO15 (RX)  <─────────────── GPIO16 (TX)
    GND          ─────────────────  GND
    ```
  - Implementation: Use **`pyserial`** library for UART communication.

- **FR9:** Command Protocol (inherits from `esp_bt_audio_source`):
  - Send commands: `COMMAND [ARGS]\n` (same protocol as ESP32-S3 `esp_i2s_source`)
  - Parse responses: `OK|COMMAND|[DATA]\n` or `ERR|COMMAND|CODE|MESSAGE\n`
  - Handle asynchronous events: `EVENT|TYPE|SUBTYPE|DATA\n`
  - Minimum command set: SCAN, CONNECT, DISCONNECT, START, STOP, PLAY, VOLUME, STATUS, RESET
  - Timeout: 5 seconds per command (configurable)
  - See `esp_bt_audio_source/docs/FS.md` for authoritative protocol specification.

- **FR10:** Command Queueing and Threading:
  - UART send/receive runs in background thread (non-blocking web UI)
  - Command queue with response callbacks
  - Event queue for asynchronous `EVENT|...` messages
  - Thread-safe status updates to web UI

### 3.4. Web User Interface

- **FR11:** Flask Web Server:
  - **Framework:** Flask (Python micro web framework)
  - **Port:** 5000 (configurable via environment variable `PORT`)
  - **Access:** LAN-only (bind to `0.0.0.0` for access from any device on network)
  - **Auto-start:** Launch Flask server on Raspberry Pi boot (systemd service)

- **FR12:** Web UI Pages and Controls:
  - **Main Dashboard (`/`):**
    - Audio source selector: Tone / Sweep / WAV File / Silence
    - Tone controls: Frequency slider (20-20000 Hz), Amplitude slider (0-100%), Stereo mode dropdown
    - Sweep controls: Start/Stop button, Duration selector (5s/10s/30s/60s), Loop checkbox
    - WAV file selector: Dropdown list of files in `/home/pi/audio/`, Play/Stop buttons
    - I2S status: Active/Stopped, Sample rate, Buffer health (underruns counter)
  
  - **Bluetooth Control Panel (`/bluetooth`):**
    - Command buttons: SCAN, CONNECT (with MAC entry), DISCONNECT, START, STOP
    - Status display: Connection state, Paired device MAC, Playback state, Volume
    - Volume slider: 0-100% (sends `VOLUME <pct>` command)
    - Device list: Show results from SCAN command
  
  - **Logs and Diagnostics (`/logs`):**
    - Real-time log viewer (last 200 lines from application log)
    - UART command/response history (last 50 transactions)
    - I2S statistics: Total frames sent, Underruns, Peak amplitude, Uptime
    - System info: Raspberry Pi model, CPU temp, Memory usage

- **FR13:** Real-Time Updates:
  - **Technology:** Server-Sent Events (SSE) or WebSocket for live status updates
  - **Update frequency:** 2 Hz (500 ms interval) for status panel
  - **Implementation:** Flask-SSE extension or lightweight WebSocket

- **FR14:** RESTful API (for programmatic control):
  - `GET /api/status` → JSON status (I2S, Bluetooth, audio source)
  - `POST /api/tone` → Set tone parameters `{"freq": 1000, "amp": 75, "mode": "mono"}`
  - `POST /api/sweep` → Start sweep `{"start": 20, "end": 20000, "duration": 10}`
  - `POST /api/wav` → Play WAV file `{"file": "test_tone.wav"}`
  - `POST /api/bt/command` → Send UART command `{"command": "VOLUME", "args": "75"}`
  - `GET /api/bt/status` → Bluetooth status from `esp_bt_audio_source`

### 3.5. Configuration and Persistence

- **FR15:** Configuration File (YAML or JSON):
  - Location: `/home/pi/rpi_i2s_source/config.yaml`
  - Settings:
    - I2S: GPIO pins, sample rate, buffer size
    - UART: Device path (`/dev/serial0`), baud rate, timeout
    - Web server: Port, bind address, log level
    - Audio: Default tone frequency/amplitude, WAV directory path
    - Bluetooth: Last connected device MAC (for quick reconnect)
  - Reload config without restart (send SIGHUP to process)

- **FR16:** Minimal Persistence (optional):
  - Save last used settings (tone frequency, volume, connected device) to `config.yaml` on clean shutdown
  - Load on startup for convenience
  - **No complex NVS/database** (just YAML file writes)

---

## 4. Non-Functional Requirements

- **NFR1:** **Development Speed:** From clean Raspberry Pi OS install to working I2S audio in <2 hours (including dependencies).
- **NFR2:** **Latency:** Tone parameter change (frequency/amplitude) applies within 100 ms (no buffering lag).
- **NFR3:** **Stability:** No crashes during 1-hour continuous sine tone generation and I2S transmission.
- **NFR4:** **I2S Timing:** BCLK and WS clocks accurate within ±50 ppm (hardware I2S peripheral requirement).
- **NFR5:** **Resource Usage:** Total Python process <100 MB RAM, <10% CPU @ idle, <25% CPU during 48 kHz stereo tone generation.
- **NFR6:** **Web UI Responsiveness:** Page loads <500 ms on LAN, status updates <100 ms latency.
- **NFR7:** **UART Reliability:** Handle `esp_bt_audio_source` disconnects/reconnects without crashing (auto-reconnect on serial error).
- **NFR8:** **Code Quality:** PEP8-compliant Python, type hints where practical, docstrings for public functions.
- **NFR9:** **Logging:** Structured logging (INFO level default, DEBUG configurable), log rotation to prevent disk fill.
- **NFR10:** **Portability:** Works on Raspberry Pi 3/4/5 and Zero 2 W without code changes (auto-detect model if needed).

---

## 5. Interfaces and Physical Connections

### 5.1. I2S Physical Interface

**Connection to `esp_bt_audio_source`:**

```
Raspberry Pi (rpi_i2s_source)        ESP32 (esp_bt_audio_source)
[I2S Master / Clock Generator]       [I2S Slave / Clock Follower]

GPIO18 (BCM) - BCLK  ──────────────> GPIO26 - BCLK input
GPIO19 (BCM) - WS    ──────────────> GPIO25 - WS input
GPIO21 (BCM) - DOUT  ──────────────> GPIO22 - DIN input
GND              ──────────────────> GND
```

**Signal Characteristics:**
- **BCLK (Bit Clock):** 48 kHz × 2 channels × 16 bits = 1.536 MHz square wave
- **WS (Word Select / LR Clock):** 48 kHz square wave (LOW = left channel, HIGH = right channel)
- **DOUT (Data Out):** 16-bit PCM samples, MSB-first, left-aligned
- **Voltage Levels:** Raspberry Pi GPIO outputs 3.3V logic (compatible with ESP32 3.3V inputs)
- **Cable Length:** <30 cm recommended for signal integrity (I2S is not designed for long cables)

**GPIO Pin Selection Rationale:**
- BCM GPIO18/19/21 are default I2S pins on Raspberry Pi (hardware I2S peripheral)
- Avoids conflicts with UART (GPIO14/15), SPI, I2C
- Compatible with `pigpio` and ALSA `snd_bcm2835` driver

### 5.2. UART Physical Interface

**Connection to `esp_bt_audio_source`:**

```
Raspberry Pi (rpi_i2s_source)        ESP32 (esp_bt_audio_source)
BCM GPIO14 (TXD0) ───────────────────> GPIO17 (UART RX)
BCM GPIO15 (RXD0) <─────────────────── GPIO16 (UART TX)
GND               ─────────────────────  GND
```

**Serial Configuration:**
- **Baud Rate:** 115200
- **Data Bits:** 8
- **Parity:** None
- **Stop Bits:** 1
- **Flow Control:** None (software buffering only)
- **Device Path:** `/dev/serial0` (primary UART on Raspberry Pi)

**Protocol:** Inherits from `esp_bt_audio_source` (see Section 5.3).

**Raspberry Pi UART Setup:**
- Disable Bluetooth on `/dev/serial0` if using RPi 3/4/5 with onboard Bluetooth:
  ```bash
  # Add to /boot/config.txt:
  dtoverlay=disable-bt
  ```
- Or use `/dev/ttyAMA0` (full UART) instead of `/dev/serial0` (mini UART)

### 5.3. UART Command Protocol

**Inherits protocol from `esp_bt_audio_source` exactly.** See `/esp_bt_audio_source/docs/FS.md` for full specification.

**Command Format:**
```
COMMAND [ARGS]\n
```

**Response Format:**
- Success: `OK|COMMAND|[DATA]\n`
- Error: `ERR|COMMAND|CODE|MESSAGE\n`

**Asynchronous Events:**
```
EVENT|TYPE|SUBTYPE|DATA\n
```

**Minimum Command Set (Python implementation must support):**
- `SCAN` — Trigger Bluetooth scan
- `CONNECT <mac>` — Connect to device by MAC address
- `DISCONNECT` — Disconnect current device
- `START` — Start audio playback
- `STOP` — Stop playback
- `PLAY [file]` — Play file from SPIFFS (if supported)
- `VOLUME <0-100>` — Set volume percentage
- `STATUS` — Query current state
- `RESET` — Soft reset `esp_bt_audio_source`

**Python Implementation Notes:**
- Use `pyserial` library for UART I/O
- Line-buffered reading: accumulate bytes until `\n`, then parse
- Tokenize on `|` delimiter
- Match responses to commands (track pending command queue)
- Timeout: 5 seconds per command (configurable)
- Handle asynchronous events in separate thread/queue

---

## 6. Audio and Data Formats

### 6.1. Output Format (Fixed)

**I2S Output:** 16-bit little-endian PCM, stereo, 48 kHz

- **Sample Rate:** 48,000 Hz (fixed, matches `esp_bt_audio_source` requirement)
- **Bit Depth:** 16-bit signed integer
- **Channels:** 2 (stereo)
- **Byte Order:** Little-endian (LSB first)
- **Alignment:** Left-aligned in I2S frame
- **Interleaving:** LRLRLR... (left sample, right sample, alternating)

### 6.2. Internal Audio Generation (Python)

**Tone Generation (NumPy):**
```python
import numpy as np

sample_rate = 48000
frequency = 1000  # Hz
amplitude = 0.75  # 75% of full scale
duration = 1.0    # seconds

t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)
tone_left = (amplitude * 32767 * np.sin(2 * np.pi * frequency * t)).astype(np.int16)
tone_right = tone_left.copy()  # Mono (same on both channels)

# Interleave stereo: LRLRLR...
stereo = np.empty(len(tone_left) * 2, dtype=np.int16)
stereo[0::2] = tone_left   # Even indices = left
stereo[1::2] = tone_right  # Odd indices = right
```

**WAV File Playback:**
```python
import scipy.io.wavfile as wav

sample_rate_wav, audio = wav.read('/home/pi/audio/test.wav')
# Resample if needed:
if sample_rate_wav != 48000:
    from scipy.signal import resample
    audio = resample(audio, int(len(audio) * 48000 / sample_rate_wav))
# Ensure 16-bit stereo:
audio = audio.astype(np.int16)
if audio.ndim == 1:  # Mono → duplicate to stereo
    audio = np.column_stack((audio, audio))
```

**Frequency Sweep (Chirp):**
```python
from scipy.signal import chirp

t = np.linspace(0, duration, int(sample_rate * duration))
sweep = chirp(t, f0=20, f1=20000, t1=duration, method='logarithmic')
sweep = (amplitude * 32767 * sweep).astype(np.int16)
```

---

## 7. Performance and Buffering

### 7.1. I2S DMA Buffering Strategy

**Goal:** Maintain continuous I2S output with zero underruns during steady-state operation.

**Buffer Architecture:**
- **Ring Buffer:** Circular buffer filled by audio generation thread, consumed by I2S hardware DMA
- **Buffer Size:** 8192 samples (4096 frames @ stereo) = ~85 ms @ 48 kHz
  - Configurable via `config.yaml`: `i2s.buffer_size` (range: 2048-16384 samples)
- **Refill Threshold:** Refill when buffer <50% full (trigger audio generation callback)
- **Overflow Handling:** Drop oldest samples if generation faster than consumption (log warning)

**Threading Model:**
- **Audio Generation Thread:** Generates PCM samples (tone/sweep/WAV), writes to ring buffer
- **I2S DMA Thread:** Reads from ring buffer, feeds to I2S hardware via `pigpio` or ALSA
- **Synchronization:** `threading.Lock` on buffer access, `threading.Event` for refill signaling

### 7.2. Latency Budget

| Stage | Latency | Notes |
|-------|---------|-------|
| Tone parameter change (web UI) | <50 ms | Flask endpoint → audio thread signal |
| Audio generation (NumPy sine) | <5 ms | 8192 samples @ 48 kHz |
| Ring buffer write | <1 ms | Mutex lock overhead |
| I2S DMA transmission | 85 ms | Buffer size / sample rate |
| **Total (parameter to I2S output)** | **<150 ms** | Acceptable for test jig |

**Optimization Note:** Use phase accumulator for click-free tone frequency changes (avoid regenerating entire buffer).

---

## 8. Reliability, Errors, and Telemetry

### 8.1. Error Handling

**I2S Errors:**
- **Underrun:** Buffer empty when I2S DMA requests samples
  - Action: Zero-fill missing samples, increment `underrun_count` telemetry, log WARNING
  - Recovery: Resume normal operation when buffer refills
- **Hardware Failure:** I2S peripheral initialization fails
  - Action: Log CRITICAL error, display error in web UI, halt audio (graceful degradation)
  - User Action: Check GPIO pins, restart application

**UART Errors:**
- **Timeout:** No response from `esp_bt_audio_source` within 5 seconds
  - Action: Return error to web UI, log WARNING, mark command as failed
- **Serial Disconnect:** `/dev/serial0` disappears or read/write fails
  - Action: Close serial port, attempt reconnect every 5 seconds (max 10 attempts), log ERROR
  - Web UI: Display "UART disconnected" status
- **Parse Error:** Invalid response format from `esp_bt_audio_source`
  - Action: Log malformed response, return error to caller, continue operation

**Audio Generation Errors:**
- **WAV File Not Found:** `/home/pi/audio/<file>` doesn't exist
  - Action: Return 404 to web UI, suggest file upload
- **WAV Format Unsupported:** File not 16-bit PCM
  - Action: Attempt conversion with `scipy.io.wavfile`, or return error
- **NumPy Exception:** Tone generation math error (invalid frequency, overflow)
  - Action: Clamp to valid range, log WARNING, use safe defaults

### 8.2. Telemetry and Monitoring

**Tracked Metrics (available via `/api/status` and web UI):**

| Metric | Type | Description |
|--------|------|-------------|
| `i2s.active` | bool | I2S transmission active |
| `i2s.sample_rate` | int | Current sample rate (always 48000) |
| `i2s.frames_sent` | int | Total stereo frames transmitted since start |
| `i2s.underruns` | int | Count of buffer underruns |
| `i2s.buffer_fill_pct` | float | Current ring buffer fill percentage (0-100) |
| `audio.source` | str | Active source: "tone", "sweep", "wav", "silence" |
| `audio.tone_freq` | int | Current tone frequency (Hz, if active) |
| `audio.tone_amp` | float | Current tone amplitude (0-1.0) |
| `uart.connected` | bool | Serial port open and responsive |
| `uart.commands_sent` | int | Total commands sent to `esp_bt_audio_source` |
| `uart.commands_ok` | int | Successful responses received |
| `uart.commands_err` | int | Error responses received |
| `uart.last_status` | dict | Latest `STATUS` response from `esp_bt_audio_source` |
| `bt.connected` | bool | Bluetooth device connected (from `esp_bt_audio_source`) |
| `bt.device_mac` | str | Connected device MAC address |
| `bt.playing` | bool | Audio playback active on BT device |
| `system.uptime` | int | Application uptime in seconds |
| `system.cpu_temp` | float | Raspberry Pi CPU temperature (°C) |
| `system.mem_usage_mb` | float | Python process memory usage (MB) |

**Logging:**
- **Library:** Python `logging` module
- **Levels:** DEBUG, INFO, WARNING, ERROR, CRITICAL
- **Default:** INFO to console and `/var/log/rpi_i2s_source.log`
- **Rotation:** `RotatingFileHandler`, max 10 MB per file, keep 5 backups
- **Format:** `%(asctime)s [%(levelname)s] %(name)s: %(message)s`

---

## 9. Configuration and Deployment

### 9.1. Configuration File (YAML)

**Location:** `/home/pi/rpi_i2s_source/config.yaml`

**Example:**
```yaml
i2s:
  gpio_bclk: 18      # BCM GPIO numbering
  gpio_ws: 19
  gpio_dout: 21
  sample_rate: 48000
  buffer_size: 8192  # samples (stereo frames × 2)

uart:
  device: /dev/serial0
  baudrate: 115200
  timeout: 5.0       # seconds

audio:
  default_source: tone  # tone, sweep, wav, silence
  tone_freq: 1000       # Hz
  tone_amp: 0.75        # 0.0-1.0
  wav_directory: /home/pi/audio

web:
  port: 5000
  bind_address: 0.0.0.0  # LAN access
  log_level: INFO        # DEBUG, INFO, WARNING, ERROR

bluetooth:
  last_device_mac: ""    # Auto-fill after successful connect
```

**Validation:**
- On load: Check GPIO pins in valid range (0-27 for RPi), sample rate = 48000, buffer size >1024
- Invalid values: Log ERROR, use hardcoded defaults, continue startup

### 9.2. Installation and Dependencies

**System Requirements:**
- Raspberry Pi 3, 4, 5, or Zero 2 W
- Raspberry Pi OS (Debian 12 Bookworm or later)
- Python 3.9+ (system default)
- Network connection (for Flask web UI access)

**Python Dependencies (`requirements.txt`):**
```txt
# Core
flask==3.0.0
pyserial==3.5
pigpio==1.78          # I2S hardware control
numpy==1.24.0
scipy==1.11.0         # WAV resample, chirp generation
pyyaml==6.0

# Optional
flask-sse==1.0.0      # Server-Sent Events for real-time updates
librosa==0.10.0       # Advanced audio processing (optional)
```

**System Packages (apt):**
```bash
sudo apt update
sudo apt install -y python3-pip python3-venv pigpio
sudo systemctl enable pigpiod
sudo systemctl start pigpiod
```

**Installation Steps:**
```bash
# 1. Clone repository
cd /home/pi
git clone <repo_url> rpi_i2s_source
cd rpi_i2s_source

# 2. Create virtual environment
python3 -m venv venv
source venv/bin/activate

# 3. Install dependencies
pip install -r requirements.txt

# 4. Configure UART (disable Bluetooth on UART if needed)
# Edit /boot/config.txt:
# Add: dtoverlay=disable-bt
# Reboot: sudo reboot

# 5. Create audio directory
mkdir -p /home/pi/audio

# 6. Run application
python main.py
# Access web UI: http://<raspberry-pi-ip>:5000
```

### 9.3. Systemd Service (Auto-Start on Boot)

**File:** `/etc/systemd/system/rpi-i2s-source.service`

```ini
[Unit]
Description=Raspberry Pi I2S Audio Source for esp_bt_audio_source
After=network.target pigpiod.service
Requires=pigpiod.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/rpi_i2s_source
Environment="PATH=/home/pi/rpi_i2s_source/venv/bin"
ExecStart=/home/pi/rpi_i2s_source/venv/bin/python main.py
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
```

**Enable and Start:**
```bash
sudo systemctl daemon-reload
sudo systemctl enable rpi-i2s-source
sudo systemctl start rpi-i2s-source
sudo systemctl status rpi-i2s-source
```

---

## 10. Library and Technology Selection

### 10.1. I2S Generation

**Option 1: `pigpio` (Recommended)**
- **Pros:** Hardware-timed bit-banging, precise I2S clock generation, mature library
- **Cons:** Requires `pigpiod` daemon running
- **Implementation:** Use `pigpio.wave_*` functions to generate BCLK/WS/DOUT waveforms
- **Complexity:** ~100 lines Python to set up I2S waveforms

**Option 2: ALSA `snd_bcm2835` driver**
- **Pros:** Native Linux audio driver, uses hardware I2S peripheral directly
- **Cons:** Requires kernel module configuration, less flexibility for test patterns
- **Implementation:** Write PCM to `/dev/snd/pcmC0D0p` ALSA device
- **Complexity:** ~50 lines Python but requires ALSA config

**Decision:** Use **`pigpio`** for maximum control over I2S timing and test pattern flexibility.

### 10.2. Web Framework

**Flask (Recommended)**
- **Pros:** Lightweight, widely used, simple API, SSE support via extensions
- **Cons:** Single-threaded (use `threading` for background tasks)
- **Alternatives:** FastAPI (modern but heavier), aiohttp (async but more complex)

**Frontend:**
- **HTML/CSS/JavaScript:** Vanilla JS for controls (no framework bloat)
- **Real-time updates:** Server-Sent Events (SSE) for status polling
- **UI Library (optional):** Bootstrap 5 for responsive layout

### 10.3. UART Communication

**`pyserial` (Standard)**
- **Pros:** De-facto standard for serial I/O in Python, cross-platform, well-documented
- **Cons:** None for this use case
- **Implementation:** Open `/dev/serial0`, configure 115200 8N1, line-buffered read/write

---

## 11. Testing and Validation

### 11.1. Unit Tests (Pytest)

**Test Coverage:**
- I2S tone generation: Verify NumPy sine wave amplitude, frequency, phase continuity
- UART protocol parser: Test command/response tokenization, error handling, timeouts
- Audio resampling: Verify WAV files convert correctly to 48 kHz
- Configuration loader: Test YAML validation, default fallbacks

**Example Test:**
```python
import pytest
import numpy as np
from audio_generator import generate_sine_tone

def test_sine_tone_frequency():
    """Verify 1 kHz tone has correct frequency spectrum."""
    samples = generate_sine_tone(freq=1000, duration=1.0, sample_rate=48000)
    fft = np.fft.rfft(samples)
    freqs = np.fft.rfftfreq(len(samples), 1/48000)
    peak_freq = freqs[np.argmax(np.abs(fft))]
    assert 995 < peak_freq < 1005, f"Peak at {peak_freq} Hz, expected 1000 Hz"
```

### 11.2. Integration Tests

**Test Scenarios:**
1. **I2S to `esp_bt_audio_source` Pipeline:**
   - Start 1 kHz tone on Raspberry Pi
   - Send `START` command to `esp_bt_audio_source` via UART
   - Verify Bluetooth speaker plays tone (manual verification initially)
   - Check `STATUS` response confirms playback active

2. **Frequency Sweep End-to-End:**
   - Trigger 20 Hz → 20 kHz sweep via web UI
   - Monitor Bluetooth audio for smooth frequency transition
   - Verify no dropouts/clicks (logic analyzer on I2S lines)

3. **WAV File Playback:**
   - Upload test WAV (44.1 kHz stereo) to `/home/pi/audio/`
   - Trigger playback via web UI
   - Verify resampled to 48 kHz and transmitted over I2S

4. **UART Resilience:**
   - Disconnect ESP32, verify web UI shows "UART disconnected"
   - Reconnect ESP32, verify auto-reconnect within 5 seconds

5. **Long-Duration Stability:**
   - Run 1 kHz tone for 1 hour continuously
   - Check telemetry: zero underruns, constant buffer fill, no memory leaks

### 11.3. Performance Validation

**Metrics to Measure:**
- **CPU Usage:** `top` or `psutil.cpu_percent()` → target <25% during tone generation
- **Memory Usage:** `psutil.Process().memory_info().rss` → target <100 MB
- **I2S Timing Accuracy:** Logic analyzer on GPIO18 BCLK → verify 1.536 MHz ±50 ppm
- **Latency:** Tone frequency change to I2S output → measure with oscilloscope trigger

**Tools:**
- **Logic Analyzer:** Saleae Logic 8 or similar (verify I2S timing, protocol compliance)
- **Oscilloscope:** Measure BCLK/WS jitter, DOUT signal integrity
- **Audio Analyzer:** Record Bluetooth output to PC, FFT analysis (verify tone purity, SNR)

---

## 12. Open Questions and Future Enhancements

### 12.1. Open Questions

1. **I2S Library Choice:** `pigpio` vs ALSA `snd_bcm2835`?
   - **Recommendation:** Start with `pigpio` for flexibility; switch to ALSA if performance insufficient.

2. **UART Auto-Discovery:** How to handle `/dev/serial0` vs `/dev/ttyAMA0` across RPi models?
   - **Recommendation:** Try `/dev/serial0` first (symlink on most models), fallback to `/dev/ttyAMA0`, make configurable in YAML.

3. **WAV Directory Management:** File upload via web UI or manual copy only?
   - **Recommendation:** Manual copy for MVP (SCP or USB), add web upload in future enhancement.

4. **Real-Time Status Updates:** SSE or WebSocket?
   - **Recommendation:** SSE (simpler, uni-directional status push), fallback to polling if issues.

### 12.2. Future Enhancements (Post-MVP)

**Phase 1 (Nice-to-Have):**
- **Multi-Tone Generator:** Play up to 4 simultaneous sine tones (chord generation)
- **WAV File Upload:** Drag-and-drop web UI for WAV file management
- **Bluetooth Device Auto-Connect:** Save last connected MAC, auto-connect on `esp_bt_audio_source` boot
- **Audio Visualization:** Real-time waveform/FFT display in web UI (WebGL or Canvas)

**Phase 2 (Advanced Testing):**
- **THD+N Measurement:** Inject test signal, capture Bluetooth output via USB audio, compute THD+N
- **Latency Measurement:** Loopback test (Bluetooth speaker → USB microphone → Raspberry Pi), measure round-trip
- **Automated Test Suite:** Python script runs all test scenarios, generates pass/fail report

**Phase 3 (Production Features - Migrate to ESP32-S3):**
- **Internet Radio Streaming:** (Defer to `esp_i2s_source` ESP32-S3 implementation)
- **Authentication/Security:** (Not needed for test jig)
- **AP Mode WiFi:** (Not needed - Raspberry Pi always connects to existing network)

---

## 13. Success Criteria and Milestones

### Milestone 1: Basic I2S Tone Generation (Target: Day 1, 4 hours)

**Deliverables:**
- Python script generates 1 kHz sine tone using NumPy
- I2S master transmitter outputs tone to GPIO18/19/21 via `pigpio`
- Logic analyzer confirms BCLK = 1.536 MHz, WS = 48 kHz, valid PCM data on DOUT
- `esp_bt_audio_source` receives I2S stream and plays via Bluetooth (manual verification)

**Success Criteria:**
- Tone audible on Bluetooth speaker
- Zero I2S protocol errors on logic analyzer
- Continuous playback for 5 minutes without dropouts

### Milestone 2: UART Command Interface (Target: Day 1-2, 4 hours)

**Deliverables:**
- `pyserial` UART communication to `esp_bt_audio_source`
- Python class `UARTCommandInterface` with methods: `send_command()`, `parse_response()`, `wait_for_event()`
- Command queue with timeout handling
- Simple CLI test: send `STATUS` command, print response

**Success Criteria:**
- `STATUS` command returns valid JSON response
- `VOLUME 75` command changes volume on Bluetooth speaker
- Timeout handling works (unplug ESP32, verify 5-second timeout logged)

### Milestone 3: Flask Web UI (Target: Day 2, 6 hours)

**Deliverables:**
- Flask app with 3 pages: Dashboard, Bluetooth Control, Logs
- Dashboard: Tone frequency/amplitude sliders, Start/Stop buttons
- Bluetooth Control: SCAN/CONNECT/DISCONNECT buttons, device list
- Real-time status updates via SSE (I2S active, buffer health, BT connection state)

**Success Criteria:**
- Web UI accessible from laptop on same LAN
- Tone frequency slider changes audio in <200 ms
- `SCAN` button triggers scan, results appear in device list within 10 seconds
- Status panel updates connection state when Bluetooth device connects/disconnects

### Milestone 4: Advanced Audio Sources (Target: Day 2-3, 4 hours)

**Deliverables:**
- Frequency sweep generator (20 Hz → 20 kHz logarithmic chirp)
- WAV file playback from `/home/pi/audio/` directory
- Left/right channel identification mode (1 kHz left, 440 Hz right)
- Web UI selectors for audio source, file picker for WAV files

**Success Criteria:**
- Frequency sweep plays smoothly from 20 Hz to 20 kHz over 10 seconds
- WAV file (44.1 kHz) resampled to 48 kHz and plays correctly
- Channel ID tones verify stereo routing (left speaker = 1 kHz, right = 440 Hz)

### Milestone 5: Stability and Telemetry (Target: Day 3, 2 hours)

**Deliverables:**
- 1-hour continuous tone test (no crashes, no underruns)
- Telemetry dashboard in web UI (frames sent, underruns, CPU temp, memory usage)
- Log rotation configured (10 MB max, 5 backups)
- Systemd service for auto-start on boot

**Success Criteria:**
- 1-hour test: zero underruns, <100 MB memory, <25% CPU
- Logs rotate correctly when exceeding 10 MB
- Raspberry Pi survives reboot and auto-starts application within 30 seconds

### Final Acceptance Criteria

**All milestones complete AND:**
- I2S timing verified with logic analyzer (BCLK ±50 ppm, WS phase-locked)
- `esp_bt_audio_source` Bluetooth pipeline validated end-to-end (scan, pair, connect, play)
- Web UI responsive on LAN (<500 ms page load, <100 ms status updates)
- Code documented (docstrings, README.md with setup instructions)
- Pytest unit tests pass for core functions (tone generation, UART parser)
- **Total development time: <16 hours** (2 days @ 8 hours/day)

---

## 14. Appendices

### Appendix A: References

- **Raspberry Pi I2S Documentation:** https://www.raspberrypi.org/documentation/computers/raspberry-pi.html#i2s
- **pigpio Library:** http://abyz.me.uk/rpi/pigpio/
- **Flask Documentation:** https://flask.palletsprojects.com/
- **pyserial Documentation:** https://pyserial.readthedocs.io/
- **esp_bt_audio_source Protocol:** `/esp_bt_audio_source/docs/FS.md` (authoritative reference)

### Appendix B: GPIO Pin Reference

**BCM GPIO Numbering (used in this document):**

| BCM GPIO | Physical Pin | Function | Direction | Connected To (ESP32) |
|----------|--------------|----------|-----------|----------------------|
| 14 (TXD0) | Pin 8 | UART TX | Output | GPIO17 (RX) |
| 15 (RXD0) | Pin 10 | UART RX | Input | GPIO16 (TX) |
| 18 | Pin 12 | I2S BCLK | Output | GPIO26 (BCLK) |
| 19 | Pin 35 | I2S WS | Output | GPIO25 (WS) |
| 21 | Pin 40 | I2S DOUT | Output | GPIO22 (DIN) |
| GND | Pin 6, 9, 14, 20, 25, 30, 34, 39 | Ground | — | GND |

**Note:** Use `raspi-gpio get` command to verify pin modes before running application.

### Appendix C: Troubleshooting

**Issue: No audio on Bluetooth speaker**
- Check: Is `esp_bt_audio_source` receiving I2S data? (send `STATUS` command, verify `audio_source: i2s`)
- Check: Is Bluetooth paired and connected? (web UI → Bluetooth Control → connection state)
- Check: Is I2S wiring correct? (BCM GPIO18/19/21 → ESP32 GPIO26/25/22, common ground)
- Test: Use logic analyzer to verify BCLK/WS/DOUT signals present on Raspberry Pi GPIOs

**Issue: UART timeout errors**
- Check: Is `/dev/serial0` accessible? (`ls -l /dev/serial0` should exist)
- Check: Is UART wiring correct? (BCM GPIO14 → ESP32 GPIO17, BCM GPIO15 ← ESP32 GPIO16)
- Check: Is `esp_bt_audio_source` powered on and booted? (look for boot logs on ESP32 serial console)
- Test: Loopback test (connect GPIO14 to GPIO15 on RPi, send command, verify echoed back)

**Issue: I2S underruns (telemetry shows >0 underruns)**
- Solution: Increase buffer size in `config.yaml`: `i2s.buffer_size: 16384` (double default)
- Solution: Reduce audio generation complexity (use pre-generated tone tables instead of NumPy `sin` on-the-fly)
- Check: CPU usage <25%? (if higher, optimize audio generation or reduce Flask polling rate)

**Issue: Web UI not accessible from laptop**
- Check: Raspberry Pi connected to same LAN as laptop?
- Check: Flask bind address = `0.0.0.0` in `config.yaml`?
- Check: Firewall blocking port 5000? (`sudo ufw status` on RPi)
- Test: Access from RPi itself first (`curl http://localhost:5000` from RPi terminal)

**Issue: High latency when changing tone parameters**
- Solution: Use phase accumulator instead of regenerating entire NumPy buffer
- Implementation: Track phase state between buffer fills, adjust phase increment for new frequency
- Expected latency reduction: 100 ms → <10 ms

### Appendix D: Development Roadmap

**Week 1 (Proof-of-Concept):**
- Days 1-2: Milestones 1-3 (I2S, UART, basic web UI)
- Day 3: Milestone 4-5 (advanced audio, stability testing)
- Days 4-5: Bug fixes, documentation, unit tests

**Week 2 (Enhancement and Integration):**
- Polish web UI (CSS styling, responsive layout)
- Add audio visualization (waveform display)
- Automated test suite (Pytest + integration tests)
- Performance tuning (optimize for <10% CPU idle)

**Week 3+ (Parallel with ESP32-S3 Development):**
- Use `rpi_i2s_source` as primary test jig for all `esp_bt_audio_source` development
- Develop ESP32-S3 `esp_i2s_source` in parallel (no urgency)
- Migrate features from RPi → ESP32-S3 incrementally (internet radio, advanced web UI)

---

## Document Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-06 | Phil | Initial PRD creation based on `esp_i2s_source` design |

---

**END OF DOCUMENT**
