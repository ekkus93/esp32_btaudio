# Raspberry Pi I2S Source — TODO List

**Project:** rpi_i2s_source (Rapid development test jig for esp_bt_audio_source)  
**Target:** Working I2S audio in <2 days (~16 hours total)  
**Last Updated:** 2026-02-06

---

## Phase 0: Project Setup and Environment

### 0.1. Repository and Directory Structure
- [x] Create directory structure per FS.md Section 9.1
  - [x] Create `rpi_i2s_source/` root directory
  - [x] Create `audio/` subdirectory with `__init__.py`
  - [x] Create `uart/` subdirectory with `__init__.py`
  - [x] Create `config/` subdirectory with `__init__.py`
  - [x] Create `telemetry/` subdirectory with `__init__.py`
  - [x] Create `web/` subdirectory with `__init__.py`
  - [x] Create `web/static/css/` directory
  - [x] Create `web/static/js/` directory
  - [x] Create `web/templates/` directory
  - [x] Create `tests/` subdirectory
  - [x] Create `tests/integration/` subdirectory
  - [x] Create `docs/` subdirectory (already exists)

### 0.2. Configuration Files
- [x] Create `requirements.txt` with Python dependencies
  - [x] Add Flask==3.0.0
  - [x] Add pyserial==3.5
  - [x] Add pigpio==1.78 (or comment out if using ALSA)
  - [x] Add numpy==1.24.0
  - [x] Add scipy==1.11.0
  - [x] Add pyyaml==6.0
  - [x] Add flask-sse==1.0.0 (optional)
  - [x] Add psutil (for telemetry)
  - [x] Add pytest (for testing)
  - [x] Add pytest-mock (for mocking in tests)

- [x] Create `config.yaml` template (default configuration)
  - [x] Define I2S section (gpio_bclk, gpio_ws, gpio_dout, sample_rate, buffer_size)
  - [x] Define UART section (device, baudrate, timeout)
  - [x] Define audio section (default_source, tone_freq, tone_amp, wav_directory)
  - [x] Define web section (port, bind_address, log_level)
  - [x] Define bluetooth section (last_device_mac)

- [x] Create `.gitignore`
  - [x] Ignore `config.yaml` (user-specific)
  - [x] Ignore `__pycache__/` and `*.pyc`
  - [x] Ignore `.pytest_cache/`
  - [x] Ignore `*.log`
  - [x] Ignore `/venv/` and `/env/`

- [x] Create `README.md` with setup instructions
  - [x] Hardware requirements
  - [x] Software dependencies
  - [x] Installation steps
  - [x] Quick start guide
  - [x] Wiring diagram (RPi ↔ ESP32)

### 0.3. Development Environment Setup
**Note:** Preparation complete. Execution deferred until Raspberry Pi hardware is available.

**Preparation (Development Machine):**
- [x] Create automated setup script (`setup_rpi.sh`)
- [x] Document automated setup in README.md
- [x] Document manual setup alternative below

**Execution (Requires Raspberry Pi Hardware):**
- [ ] Install Raspberry Pi OS (Bookworm or later) on target RPi
- [ ] Run automated setup script: `bash setup_rpi.sh`
  - [ ] Script installs system packages (python3, pip, venv, alsa-utils or pigpio)
  - [ ] Script configures UART (adds `dtoverlay=disable-bt` to `/boot/config.txt`)
  - [ ] Script creates Python venv and installs requirements.txt
  - [ ] Script creates `/home/pi/audio` directory
  - [ ] Script adds user to `dialout` group for UART access
  - [ ] Script creates `config.yaml` from template
- [ ] Reboot Raspberry Pi if UART config changed
- [ ] Verify UART device exists: `ls -l /dev/serial0`
- [ ] Verify I2S enabled (optional): Check `/boot/config.txt` for `dtparam=i2s=on`

**Alternative (Manual Setup):**
- [ ] Install system packages manually
  - [ ] `sudo apt update && sudo apt upgrade`
  - [ ] `sudo apt install -y python3-pip python3-venv`
  - [ ] `sudo apt install -y alsa-utils` (recommended for MVP)
  - [ ] OR `sudo apt install -y pigpio && sudo systemctl enable pigpiod` (advanced)
- [ ] Configure UART manually
  - [ ] Edit `/boot/config.txt`: add `dtoverlay=disable-bt`
  - [ ] `sudo reboot`
  - [ ] Verify `/dev/serial0` exists: `ls -l /dev/serial0`
- [ ] Create Python virtual environment manually
  - [ ] `cd /home/pi/esp32_btaudio/rpi_i2s_source`
  - [ ] `python3 -m venv venv`
  - [ ] `source venv/bin/activate`
  - [ ] `pip install -r requirements.txt`
- [ ] Create audio directory manually
  - [ ] `mkdir -p /home/pi/audio`
  - [ ] Add sample WAV file for testing (e.g., `test_tone.wav`)

**Status:** ✅ Preparation complete (script created, documented). Execution deferred until Raspberry Pi hardware is available. **Proceed to Phase 1 (Core Components) on development machine.**

---

## Phase 1: Core Components Implementation

### 1.1. Ring Buffer (`audio/ring_buffer.py`)
**Priority:** HIGH (dependency for audio engine and I2S driver)  
**Estimated Time:** 1-2 hours  
**Status:** ✅ COMPLETE

- [x] Implement `RingBuffer` class (FS.md Section 2.4)
  - [x] `__init__(capacity)`: Initialize buffer, read/write pointers, size, lock, event
  - [x] `write(samples)`: Write samples with overflow handling (drop oldest)
  - [x] `read(num_samples)`: Read samples, return None on underrun
  - [x] `get_fill_percentage()`: Calculate buffer fill (0-100%)
  - [x] `clear()`: Reset read/write pointers
  - [x] Add thread safety with `threading.Lock`
  - [x] Add refill signaling with `threading.Event`
  - [x] Additional: `wait_for_data()`, `get_stats()`, properties for `size` and `capacity`

- [x] Test ring buffer independently
  - [x] Unit test: write/read roundtrip (verify FIFO order) ✅
  - [x] Unit test: overflow handling (verify drop-oldest policy) ✅
  - [x] Unit test: underrun handling (verify None return) ✅
  - [x] Unit test: concurrent access (2 writers + 2 readers, FS.md Section 10.1) ✅
  - [x] Additional tests: wrap-around, clear, fill percentage, wait_for_data, stats (25 tests total, all passing)

### 1.2. Config Manager (`config/manager.py`)
**Priority:** HIGH (needed by all components)  
**Estimated Time:** 1-2 hours  
**Status:** ✅ COMPLETE

- [x] Implement `ConfigManager` class (FS.md Section 2.6)
  - [x] Define `DEFAULT_CONFIG` dictionary (all sections with defaults)
  - [x] `__init__(config_path)`: Load or create config file
  - [x] `get(key_path)`: Get value by dot-separated path (e.g., "i2s.gpio_bclk")
  - [x] `set(key_path, value)`: Set value by path
  - [x] `save()`: Write config to YAML file
  - [x] `reload()`: Reload config from file
  - [x] `_load_or_create()`: Load existing or create default YAML
  - [x] `_merge_with_defaults()`: Fill missing keys from defaults
  - [x] `_validate(config)`: Validate GPIO pins, sample rate, buffer size, etc.
  - [x] Additional: `get_all()`, `config_path` property

- [x] Test config manager
  - [x] Unit test: load default config (verify all sections present) ✅
  - [x] Unit test: validation (invalid GPIO pin raises ValueError) ✅
  - [x] Unit test: get/set with dot notation ✅
  - [x] Unit test: save/reload roundtrip (verify persistence) ✅
  - [x] Additional tests: merge defaults, malformed YAML, validation (GPIO duplicates, sample rate, buffer size, baudrate, tone freq/amp, web port, log level), intermediate dict creation (25 tests total, all passing)

### 1.3. Telemetry Tracker (`telemetry/tracker.py`)
**Priority:** MEDIUM (needed for web UI status)  
**Estimated Time:** 1 hour  
**Status:** ✅ COMPLETE

- [x] Implement `TelemetryTracker` class (FS.md Section 2.7)
  - [x] `__init__()`: Initialize all stat dictionaries (i2s, uart, bt, audio, system)
  - [x] `update_i2s(stats)`: Update I2S statistics
  - [x] `update_uart(stats)`: Update UART statistics
  - [x] `update_bt(stats)`: Update Bluetooth statistics
  - [x] `update_audio(state)`: Update audio state
  - [x] `get_full_status()`: Aggregate all stats into single JSON-serializable dict
  - [x] `_get_cpu_temp()`: Read from `/sys/class/thermal/thermal_zone0/temp`
  - [x] `_get_memory_usage()`: Use `psutil.Process().memory_info().rss`
  - [x] Additional: `reset_stats()`, `_refresh_system_stats()`

- [x] Test telemetry tracker
  - [x] Unit test: update and retrieve stats (verify aggregation) ✅
  - [x] Unit test: CPU temperature reading (mock file read) ✅
  - [x] Unit test: memory usage reading (mock psutil) ✅
  - [x] Additional tests: partial updates, incremental counters, multiple audio sources, uptime calculation, deep copy isolation, error handling (24 tests total, all passing)

### 1.4. Audio Engine (`audio/engine.py`)
**Priority:** HIGH (core audio generation)  
**Estimated Time:** 3-4 hours  
**Status:** ✅ COMPLETE

- [x] Implement `AudioEngine` class (FS.md Section 2.2)
  - [x] `__init__(config, ring_buffer)`: Initialize parameters, phase accumulator
  - [x] `start()`: Start audio generation thread
  - [x] `stop()`: Stop audio generation thread (graceful shutdown)
  - [x] `set_source(source, params)`: Switch between tone/sweep/wav/silence
  - [x] `set_tone_params(freq, amp, mode)`: Update tone parameters (atomic, click-free)
  - [x] Additional: `get_state()`
  
  - [x] **Internal Methods:**
    - [x] `_generation_loop()`: Main background thread (check buffer, generate, write)
    - [x] `_generate_next_chunk()`: Dispatch to tone/sweep/wav/silence generator
    - [x] `_generate_tone()`: NumPy sine wave with phase accumulator (FS.md example)
      - [x] Support mono, left-only, right-only, dual-tone modes
      - [x] Interleave stereo: LRLRLR...
    - [x] `_generate_sweep()`: Logarithmic chirp using `scipy.signal.chirp`
      - [x] Track sweep position for continuous playback
      - [x] Support loop mode
    - [x] `_generate_wav()`: Read chunk from loaded WAV buffer
      - [x] Handle EOF (stop or loop)
    - [x] `_load_wav(filename)`: Load WAV file from `/home/pi/audio/`
      - [x] Use `scipy.io.wavfile.read()`
      - [x] Resample to 48 kHz if needed (`scipy.signal.resample`)
      - [x] Convert to 16-bit stereo (mono → duplicate channels)
      - [x] Raise `WAVNotFoundError` if file missing
      - [x] Raise `WAVFormatError` if format unsupported

- [x] Test audio engine
  - [x] Unit test: tone frequency accuracy (FFT peak at expected freq, FS.md Section 10.1) ✅
  - [x] Unit test: tone amplitude (verify ±5% tolerance) ✅
  - [x] Unit test: phase continuity (no clicks when changing frequency) ✅
  - [x] Unit test: stereo modes (mono, left-only, right-only, dual-tone) ✅
  - [x] Unit test: WAV loading and resampling (44.1 kHz → 48 kHz) ✅
  - [x] Unit test: WAV file not found exception ✅
  - [x] Unit test: sweep generation (verify chirp parameters) ✅
  - [x] Additional tests: start/stop thread, set_source, set_tone_params, get_state, thread safety (37 tests total, all passing)

### 1.5. I2S Driver (`audio/i2s_driver.py`)
**Priority:** HIGH (critical for I2S output)  
**Estimated Time:** 4-6 hours (complex, hardware-dependent)  
**Status:** ✅ COMPLETE

**Decision Point:** Choose implementation approach first.

- [x] **Option A: ALSA Driver (Recommended for MVP)** ✅
  - [x] Implement `I2SDriverALSA` class (FS.md Section 2.3, simplified version)
    - [x] `__init__(config, ring_buffer)`: Initialize ALSA device
      - [x] `alsaaudio.PCM(alsaaudio.PCM_PLAYBACK, device='hw:0,0')`
      - [x] Set channels=2, rate=48000, format=S16_LE, period size=1024
    - [x] `start()`: Start DMA thread
    - [x] `stop()`: Stop DMA thread, close ALSA device
    - [x] `get_stats()`: Return frames_sent, underruns, buffer_fill_pct
    - [x] `_dma_loop()`: Read from ring buffer, write to ALSA device
      - [x] Handle underruns with zero-fill and counter increment
      - [x] Block on `self.device.write(samples.tobytes())`
    - [x] Additional: `_init_alsa_device()` for deferred initialization
  
  - [x] Test ALSA I2S driver ✅
    - [x] Unit test: initialization (stores config, ring buffer, sets defaults) ✅
    - [x] Unit test: ALSA device initialization (opens PCM, configures parameters) ✅
    - [x] Unit test: start/stop lifecycle (launches thread, idempotent calls) ✅
    - [x] Unit test: DMA loop (reads from ring buffer, writes to ALSA, updates stats) ✅
    - [x] Unit test: underrun handling (zero-fill, increment counter) ✅
    - [x] Unit test: statistics (get_stats returns all fields, reflects current state) ✅
    - [x] Integration test: continuous transmission (tone generator → ring buffer → ALSA) ✅
    - [x] Integration test: underrun recovery (empty buffer → fill → transmit) ✅
    - [x] Integration test: thread safety (concurrent start/stop calls) ✅
    - [x] Error handling: ALSA init failure, write failure recovery, close failure ✅
    - [x] **26 tests total, all passing (8.86s)**
    - [ ] Hardware verification: I2S GPIO signals with logic analyzer (requires Raspberry Pi)
    - [ ] Hardware verification: 5-minute continuous playback (requires Raspberry Pi)

- [ ] **Option B: pigpio Driver (Advanced, if ALSA insufficient)** (deferred)
  - [ ] Implement `I2SDriver` class with pigpio (FS.md Section 2.3, full version)
    - [ ] `__init__(config, ring_buffer)`: Connect to pigpiod daemon
    - [ ] `_setup_i2s_waveforms()`: Generate BCLK/WS waveforms with `pigpio.wave_*`
      - [ ] BCLK: 1.536 MHz square wave (32 cycles per WS period)
      - [ ] WS: 48 kHz square wave (LOW=left, HIGH=right)
      - [ ] Research pigpio waveform API for precise timing
    - [ ] `_transmit_frame(samples)`: Bit-bang DOUT GPIO for each sample bit
    - [ ] `_dma_loop()`: Transmit stereo frames at 48 kHz rate
  
  - [ ] Test pigpio I2S driver
    - [ ] Verify BCLK timing accuracy (±50 ppm with logic analyzer)
    - [ ] Verify WS phase alignment
    - [ ] Verify DOUT data integrity (compare to expected PCM values)

**Note:** Start with ALSA (simpler, 2-3 hours). Switch to pigpio only if ALSA proves inadequate.

### 1.6. UART Command Manager (`uart/command_manager.py`)
**Priority:** HIGH (needed for Bluetooth control)  
**Estimated Time:** 3-4 hours
**Status:** ✅ COMPLETE

- [x] Implement `UARTCommandManager` class (FS.md Section 2.5)
  - [x] `__init__(config)`: Store config, deferred serial port opening
    - [x] Device: `/dev/serial0`, baudrate: 115200, timeout: 1.0
    - [x] Initialize command queue, response futures dict, event callbacks list
    - [x] Initialize stats counters (sent, ok, err, events, reconnects)
  
  - [x] `start()`: Initialize serial port, start UART receive thread
    - [x] Call `_init_serial_port()` to open `serial.Serial(port, baudrate, timeout)`
    - [x] Launch daemon RX thread with `_rx_loop()`
  
  - [x] `stop()`: Stop RX thread, close serial port
    - [x] Set `running = False`
    - [x] Join thread with 1s timeout
    - [x] Close serial port
    - [x] Idempotent (safe to call multiple times)
  
  - [x] `send_command(command, args='', timeout=5.0)`: Send command, wait for response (blocking)
    - [x] Create UUID command_id and `concurrent.futures.Future` for response
    - [x] Write command line: `f"{command} {args}\n"`
    - [x] Wait for future result with timeout (default 5 seconds)
    - [x] Return `{"status": "ok", "command": ..., "result": ...}` or `{"status": "error", ...}`
    - [x] Raise `concurrent.futures.TimeoutError` if no response
    - [x] Raise `RuntimeError` if not running
    - [x] Increment `stats['sent']` counter
  
  - [x] `send_command_async(command, args='', callback=None)`: Non-blocking send
    - [x] Spawn daemon thread to call `send_command()`
    - [x] Call callback(response) on completion
    - [x] Handle exceptions in callback
  
  - [x] `register_event_callback(callback)`: Add event handler function to list
  
  - [x] `get_last_status()`: Return cached STATUS response (or None)
  
  - [x] `get_stats()`: Return copy of statistics dict
  
  - [x] **Internal Methods:**
    - [x] `_init_serial_port()`: Open serial port with pyserial
      - [x] Idempotent (check if already open)
      - [x] Raise SerialException on error
    - [x] `_rx_loop()`: Read bytes from serial port, accumulate until `\n`, parse line
      - [x] Daemon thread (doesn't block shutdown)
      - [x] `while self.running:` loop with `readline()`
      - [x] Decode UTF-8 with errors='ignore'
      - [x] Handle `serial.SerialException` with `_reconnect()` logic
    - [x] `_process_line(line)`: Tokenize on `|`, dispatch to response or event handler
      - [x] Parse OK|COMMAND|result
      - [x] Parse ERR|COMMAND|error_code|message
      - [x] Parse EVENT|TYPE|SUBTYPE|data
      - [x] Log warnings for malformed lines
    - [x] `_handle_response(line, status, parts)`: Match response to pending future, resolve it
      - [x] Extract command, result/error_code/message
      - [x] Cache STATUS responses in `self.last_status`
      - [x] Resolve oldest pending Future (FIFO matching)
      - [x] Increment `stats['ok']` or `stats['err']`
    - [x] `_handle_event(parts)`: Parse EVENT message
      - [x] Create event dict `{"type": ..., "subtype": ..., "data": ...}`
      - [x] Call all registered event callbacks
      - [x] Increment `stats['events']`
      - [x] Handle exceptions in callbacks (log but don't crash)
    - [x] `_reconnect()`: Attempt serial port reconnect (max 10 attempts, 5s delay)
      - [x] Close old serial port
      - [x] Reopen with `_init_serial_port()`
      - [x] Increment `stats['reconnects']`
      - [x] Log reconnect attempts and success/failure
  
  - [x] **Mock Support:**
    - [x] MockSerial class when pyserial unavailable
    - [x] MockSerial.write() logs data, returns len(data)
    - [x] MockSerial.readline() sleeps 0.1s, returns b""
    - [x] Allows development without pyserial or hardware

- [x] Test UART command manager
  - [x] **33 tests total, all passing (10.39s)**
  - [x] Unit test: Initialization (stores config, sets defaults, initializes stats)
  - [x] Unit test: Serial port opening (pyserial.Serial called with correct params, idempotent)
  - [x] Unit test: Start/stop lifecycle (opens serial, launches RX thread, graceful shutdown)
  - [x] Unit test: Command sending (writes to serial, increments sent counter, formats with args)
  - [x] Unit test: Response parsing
    - [x] OK response (status="ok", result extracted, ok counter incremented)
    - [x] ERR response (status="error", error_code/message extracted, err counter incremented)
    - [x] STATUS caching (last_status updated)
  - [x] Unit test: Event parsing (EVENT|BT|CONNECTED → callbacks invoked, events counter)
  - [x] Unit test: Event callbacks (multiple callbacks, exception handling in callbacks)
  - [x] Unit test: Async commands (send_command_async with callback)
  - [x] Unit test: Statistics (get_stats returns all counters)
  - [x] Unit test: get_last_status (None initially, cached after STATUS command)
  - [x] Unit test: Timeout handling (concurrent.futures.TimeoutError raised)
  - [x] Unit test: Not running (RuntimeError if send_command before start)
  - [x] Unit test: Error handling (malformed OK/ERR, unknown message types)
  - [x] Integration test: Multiple sequential commands (sent/ok counters)
  - [x] Integration test: Mixed responses and events (interleaved EVENT and OK messages)
  - [x] **Note:** Hardware integration tests (real ESP32 UART) deferred until Raspberry Pi available

### 1.7. Flask Web Server (`web/app.py`)
**Priority:** MEDIUM (needed for UI control)  
**Estimated Time:** 4-5 hours  
**Status:** ✅ COMPLETE

- [x] Implement `WebServer` class (FS.md Section 2.1)
  - [x] `__init__(config, audio_engine, uart_manager, telemetry)`: Store component refs
    - [x] Create Flask app instance
    - [x] Register all routes
    - [x] SSE stream implemented (no flask-sse dependency needed)
  
  - [x] `start()`: Start Flask server (blocking call)
    - [x] `self.app.run(host=..., port=..., threaded=True)`
  
  - [x] `stop()`: Gracefully shutdown server
  
  - [x] **API Endpoints (Flask routes):**
    - [x] `GET /api/status`: Return `telemetry.get_full_status()` as JSON
    - [x] `POST /api/tone`: Parse JSON body, call `audio_engine.set_tone_params()`
      - [x] Validate freq (20-20000), amp (0.0-1.0), mode (mono/left/right/dual)
      - [x] Return `{"status": "ok"}` or error message
      - [x] Auto-switch to tone source if needed
    - [x] `POST /api/sweep`: Parse JSON, call `audio_engine.set_source('sweep', params)`
    - [x] `POST /api/wav`: Parse JSON, call `audio_engine.set_source('wav', {'file': ...})`
      - [x] Catch `WAVNotFoundError` → return 404 with message
      - [x] Catch `WAVFormatError` → return 400 with message
    - [x] `POST /api/silence`: Set silence mode
    - [x] `POST /api/bt/command`: Parse JSON, call `uart_manager.send_command()`
      - [x] Return UART response or timeout error
      - [x] Return 503 if UART manager not available
    - [x] `GET /api/bt/status`: Return `uart_manager.get_last_status()`
      - [x] Return 503 if UART manager not available
    - [x] `GET /api/stream`: Server-Sent Events stream
      - [x] Publish status updates every 500 ms (2 Hz)
      - [x] Format: `data: <JSON>\n\n`
  
  - [x] **Error Handling:**
    - [x] Wrap all endpoints in try/except
    - [x] Return meaningful JSON error messages
    - [x] Log all exceptions with `logging.error(..., exc_info=True)`
    - [x] Proper HTTP status codes (200, 400, 404, 500, 503, 504)

- [x] Test Flask web server
  - [x] **36 tests total, all passing (0.43s)**
  - [x] Unit test: Initialization (stores dependencies, loads config, creates Flask app)
  - [x] Unit test: Status endpoints (GET /api/status, SSE stream, error handling)
  - [x] Unit test: Audio control endpoints
    - [x] Tone parameters (valid, partial, dual-mode, validation, source switching)
    - [x] Sweep parameters (valid, defaults, validation)
    - [x] WAV playback (valid, missing file, format errors)
    - [x] Silence mode
  - [x] Unit test: Bluetooth endpoints
    - [x] Commands (success, with args, timeout, missing params)
    - [x] Status retrieval
    - [x] UART manager unavailable (503 responses)
  - [x] Unit test: Error handling (audio engine exceptions, UART exceptions)
  - [x] Integration test: Multiple tone changes, source switching, concurrent requests

**Implementation Details:**
- **File:** `web/app.py` (456 lines, production-ready)
- **Test File:** `tests/test_web_server.py` (548 lines)
- **Flask Test Client:** Used for all endpoint testing (no real HTTP server needed)
- **UART Manager:** Optional dependency (server works without it, returns 503 for BT endpoints)
- **JSON Parsing:** Uses `silent=True` to handle missing Content-Type gracefully
- **SSE Stream:** Native implementation using Flask Response generator
- **Logging:** Comprehensive logging for all errors with stack traces
- **Type Hints:** Full type annotations for all methods

### 1.8. Frontend Web UI (`web/templates/` and `web/static/`)
**Priority:** MEDIUM (user interface)  
**Estimated Time:** 3-4 hours
**Status:** ✅ COMPLETE

- [x] Create HTML templates (Jinja2)
  - [x] `templates/base.html`: Base template with common header/footer
    - [x] Include Bootstrap 5 CSS (CDN: bootstrap@5.3.0)
    - [x] Include Bootstrap Icons (CDN: bootstrap-icons@1.10.0)
    - [x] Include custom CSS: `<link href="/static/css/style.css">`
    - [x] Include JavaScript: `<script src="/static/js/dashboard.js">`
    - [x] Navigation bar with connection status indicator
    - [x] Footer with project info
  
  - [x] `templates/index.html`: Main dashboard (extends base.html)
    - [x] Audio source selector (radio buttons: Tone / Sweep / WAV / Silence)
    - [x] Tone controls:
      - [x] Frequency slider (20-20000 Hz with real-time display)
      - [x] Amplitude slider (0-100% with real-time display)
      - [x] Stereo mode dropdown (Mono / Left / Right / Dual)
      - [x] Dual-tone frequency slider (shows when dual mode selected)
      - [x] Apply Tone Settings button
    - [x] Sweep controls:
      - [x] Duration selector (5s / 10s / 30s / 60s)
      - [x] Loop checkbox
      - [x] Start Sweep button
    - [x] WAV file controls:
      - [x] File input field (manual entry)
      - [x] Loop checkbox
      - [x] Play WAV button
    - [x] Bluetooth controls (integrated into main dashboard):
      - [x] MAC address input field
      - [x] Command buttons: SCAN, CONNECT, DISCONNECT, START, STOP
      - [x] Device list placeholder (for scan results)
    - [x] System status panel:
      - [x] I2S driver status (Active/Stopped with color badge)
      - [x] Audio source status
      - [x] Bluetooth connection status
      - [x] CPU temperature display
      - [x] Memory usage display
      - [x] Uptime display
    - [x] I2S statistics panel:
      - [x] Buffer fill percentage (color-coded progress bar)
      - [x] Frames sent counter
      - [x] Underruns counter
      - [x] Sample rate display
    - [x] Current audio info panel:
      - [x] Dynamic display based on source (tone freq/amp/mode, WAV file)
      - [x] Shows/hides relevant fields per source type
    - [x] Alert area for user feedback messages
  
  - [x] **Note:** Separate bluetooth.html and logs.html deferred (integrated into index.html for MVP)
    - [x] Bluetooth controls included in main dashboard
    - [x] Log viewer can be added later as enhancement

- [x] Create CSS (`static/css/style.css`)
  - [x] Custom styling for dashboard layout (responsive grid)
  - [x] Slider styles (frequency/amplitude with custom colors)
  - [x] Status indicator colors (green=connected, red=disconnected, yellow=connecting)
  - [x] Progress bar styling (color-coded buffer fill: green >50%, yellow >25%, red <25%)
  - [x] Badge styling (status badges with semantic colors)
  - [x] Card styling (headers with icons, clean borders)
  - [x] Device list styling (hover effects, selection state)
  - [x] Alert animations (slide-in effect)
  - [x] Responsive adjustments (mobile-friendly layouts)
  - [x] Footer styling (sticky footer)

- [x] Create JavaScript (`static/js/dashboard.js`)
  - [x] Initialize SSE connection to `/api/stream`
    - [x] Auto-reconnect on connection loss (5s delay)
    - [x] Connection status indicator updates (connected/connecting/disconnected)
    - [x] Real-time status updates via SSE messages
  - [x] Update dashboard from status data:
    - [x] I2S status (active/stopped, frames sent, underruns, buffer fill)
    - [x] Audio status (source, tone params, WAV file)
    - [x] Bluetooth status (connected/disconnected)
    - [x] System status (CPU temp, memory, uptime with formatting)
  - [x] Tone controls:
    - [x] Frequency slider with real-time display update
    - [x] Amplitude slider with percentage display
    - [x] Dual-tone controls (show/hide based on mode selection)
    - [x] Apply button: POST to `/api/tone` with all params
    - [x] Dual-tone frequency parameter support
  - [x] Sweep controls:
    - [x] Duration selector
    - [x] Loop checkbox
    - [x] Start button: POST to `/api/sweep`
  - [x] WAV controls:
    - [x] File input field
    - [x] Loop checkbox
    - [x] Play button: POST to `/api/wav` with validation
  - [x] Silence mode:
    - [x] Auto-apply when silence source selected
    - [x] POST to `/api/silence`
  - [x] Bluetooth controls:
    - [x] SCAN button: POST `/api/bt/command` with loading spinner
    - [x] CONNECT button: POST with MAC from input field
    - [x] DISCONNECT button: POST disconnect command
    - [x] START/STOP buttons: POST playback commands
    - [x] Error handling for UART unavailable (503 status)
  - [x] UI utilities:
    - [x] Show/hide controls based on selected audio source
    - [x] Number formatting (thousands separator)
    - [x] Uptime formatting (hours, minutes, seconds)
    - [x] Buffer fill color coding
    - [x] Alert system (auto-dismiss after 5s, Bootstrap alerts)
  - [x] Error handling:
    - [x] Display error messages via alert system
    - [x] Graceful degradation when UART unavailable
    - [x] SSE reconnection on connection loss

- [x] Test web UI (Manual browser verification on Raspberry Pi - deferred until hardware available)
  - [x] **Implementation complete, ready for manual testing:**
  - [ ] Manual test: Open dashboard in browser (http://<rpi-ip>:5000), verify layout renders correctly
  - [ ] Manual test: Adjust tone frequency slider, verify display updates in real-time
  - [ ] Manual test: Click "Apply Tone Settings", verify tone parameters sent to API
  - [ ] Manual test: Switch audio sources, verify correct controls show/hide
  - [ ] Manual test: Verify SSE connection status indicator (should show "Connected")
  - [ ] Manual test: Verify real-time status updates (I2S stats, system info)
  - [ ] Manual test: Click SCAN button, verify Bluetooth scan command sent
  - [ ] Manual test: Enter MAC and click CONNECT, verify connect command sent
  - [ ] Manual test: Verify alert messages appear and auto-dismiss after 5s
  - [ ] Manual test: Test responsive layout on mobile browser
  - [ ] **Note:** Hardware testing requires Raspberry Pi with running Flask server

**Files Created:**
- `web/templates/base.html` (68 lines): Bootstrap 5 base template with navigation, footer, CDN links
- `web/templates/index.html` (330 lines): Comprehensive dashboard with all controls and status panels
- `web/static/css/style.css` (247 lines): Custom styling with responsive design, status colors, animations
- `web/static/js/dashboard.js` (617 lines): Complete JavaScript for SSE, API calls, UI updates, Bluetooth control

**Key Features:**
- **Responsive Design:** Works on desktop, tablet, and mobile browsers
- **Real-Time Updates:** SSE connection with auto-reconnect for live status
- **Audio Control:** Tone (freq, amp, stereo mode, dual-tone), sweep (duration, loop), WAV playback
- **Bluetooth Control:** SCAN, CONNECT, DISCONNECT, START, STOP commands via UART API
- **Status Monitoring:** I2S driver, buffer fill (color-coded progress bar), system metrics
- **User Feedback:** Bootstrap alert system with auto-dismiss, loading spinners on buttons
- **Error Handling:** Graceful degradation when UART unavailable, connection loss recovery

**Technology Stack:**
- Bootstrap 5.3.0 (responsive UI framework)
- Bootstrap Icons 1.10.0 (icon library)
- Vanilla JavaScript (no jQuery or heavy frameworks)
- Server-Sent Events (real-time updates)
- Flask backend (Jinja2 templating)
  - [ ] Manual test: Open dashboard in browser, verify layout renders correctly
  - [ ] Manual test: Adjust tone frequency, verify slider updates and tone changes
  - [ ] Manual test: Click SCAN button, verify device list populates
  - [ ] Manual test: Click Play WAV, verify WAV playback starts

---

## Phase 2: Main Application Integration

### 2.1. Main Application (`main.py`)
**Priority:** HIGH (application entry point)  
**Estimated Time:** 1-2 hours

- [ ] Implement `main()` function (FS.md Section 5.1)
  - [ ] Setup logging (basicConfig with format, level from config)
  - [ ] Load configuration with `ConfigManager`
  - [ ] Initialize components in dependency order:
    - [ ] Create `RingBuffer`
    - [ ] Create `AudioEngine(config, ring_buffer)`
    - [ ] Create `I2SDriver(config, ring_buffer)` (or `I2SDriverALSA`)
    - [ ] Create `UARTCommandManager(config)`
    - [ ] Create `TelemetryTracker()`
    - [ ] Create `WebServer(config, audio_engine, uart_mgr, telemetry)`
  
  - [ ] Start background components:
    - [ ] `audio_engine.start()`
    - [ ] `i2s_driver.start()`
    - [ ] `uart_mgr.start()`
  
  - [ ] Register UART event callbacks:
    - [ ] BT CONNECTED → update telemetry
    - [ ] BT DISCONNECTED → update telemetry
    - [ ] Add callback function `on_bt_event(event)`
  
  - [ ] Setup signal handlers for graceful shutdown:
    - [ ] `signal.signal(signal.SIGINT, signal_handler)`
    - [ ] `signal.signal(signal.SIGTERM, signal_handler)`
    - [ ] In `signal_handler()`: stop all components, call `sys.exit(0)`
  
  - [ ] Start web server (blocking): `web_server.start()`

- [ ] Test main application
  - [ ] Integration test: Run `python main.py`, verify all components start
  - [ ] Integration test: Send SIGINT (Ctrl+C), verify graceful shutdown
  - [ ] Integration test: Check logs for errors during startup

### 2.2. Exception Classes (`utils/exceptions.py` or inline)
**Priority:** LOW (can define in component files initially)  
**Estimated Time:** 15 minutes

- [ ] Define custom exception classes (FS.md Section 5.2)
  - [ ] `I2SError` (base)
  - [ ] `I2SUnderrunError`
  - [ ] `I2SHardwareError`
  - [ ] `UARTError` (base)
  - [ ] `UARTTimeoutError`
  - [ ] `UARTDisconnectedError`
  - [ ] `AudioError` (base)
  - [ ] `WAVNotFoundError`
  - [ ] `WAVFormatError`

---

## Phase 3: Testing

### 3.1. Unit Tests (Pytest)
**Priority:** HIGH (ensure core logic works)  
**Estimated Time:** 3-4 hours

- [ ] Test ring buffer (`tests/test_ring_buffer.py`)
  - [ ] ✅ Test write/read roundtrip (verify FIFO order)
  - [ ] ✅ Test overflow handling (verify drop-oldest policy)
  - [ ] ✅ Test underrun handling (verify None return)
  - [ ] ✅ Test concurrent access (FS.md Section 10.1 example)
  - [ ] Test get_fill_percentage() accuracy
  - [ ] Test clear() resets pointers

- [ ] Test audio engine (`tests/test_audio_engine.py`)
  - [ ] ✅ Test tone frequency accuracy (FFT peak at 1 kHz ±5 Hz, FS.md Section 10.1)
  - [ ] ✅ Test tone amplitude (±5% tolerance)
  - [ ] Test phase continuity (no discontinuities when changing freq)
  - [ ] Test stereo modes (mono, left-only, right-only, dual-tone)
  - [ ] Test WAV loading (verify resample 44.1 kHz → 48 kHz)
  - [ ] Test WAV file not found exception
  - [ ] Test WAV format error (non-PCM file)

- [ ] Test UART manager (`tests/test_uart_manager.py`)
  - [ ] ✅ Test parse OK response (FS.md Section 10.1 example)
  - [ ] ✅ Test parse ERR response
  - [ ] ✅ Test parse EVENT message (verify callback invoked)
  - [ ] Test command timeout (mock serial read timeout)
  - [ ] Test serial disconnect recovery (mock serial exception)

- [ ] Test config manager (`tests/test_config_manager.py`)
  - [ ] Test load default config (verify all sections present)
  - [ ] Test validation (invalid GPIO pin raises ValueError)
  - [ ] Test get/set with dot notation
  - [ ] Test save/reload roundtrip

- [ ] Test telemetry tracker (`tests/test_telemetry.py`)
  - [ ] Test update and retrieve stats (verify aggregation)
  - [ ] Test CPU temperature reading (mock file read)
  - [ ] Test memory usage reading (mock psutil)

- [ ] Run all unit tests
  - [ ] `pytest tests/ -v`
  - [ ] Verify >80% code coverage: `pytest --cov=audio --cov=uart --cov=config --cov=telemetry`

### 3.2. Integration Tests
**Priority:** MEDIUM (validate component interactions)  
**Estimated Time:** 2-3 hours

- [ ] Test I2S pipeline (`tests/integration/test_i2s_pipeline.py`)
  - [ ] ✅ Test end-to-end tone to Bluetooth (FS.md Section 10.2 example)
    - [ ] Prerequisites: esp_bt_audio_source connected, Bluetooth speaker paired
    - [ ] Generate 1 kHz tone via HTTP POST
    - [ ] Send START command via UART
    - [ ] Verify status: I2S active, BT playing, zero underruns
    - [ ] Manual verification: Listen to Bluetooth speaker (1 kHz tone audible)
  
  - [ ] Test frequency sweep end-to-end
    - [ ] Trigger 20 Hz → 20 kHz sweep via web UI
    - [ ] Monitor Bluetooth audio for smooth transition
    - [ ] Use logic analyzer to verify no I2S dropouts

  - [ ] Test WAV file playback
    - [ ] Upload test WAV (44.1 kHz stereo) to `/home/pi/audio/`
    - [ ] Trigger playback via POST /api/wav
    - [ ] Verify resampled to 48 kHz and transmitted over I2S

  - [ ] Test UART resilience
    - [ ] Disconnect ESP32, verify web UI shows "UART disconnected"
    - [ ] Reconnect ESP32, verify auto-reconnect within 10 seconds

  - [ ] Test long-duration stability
    - [ ] Run 1 kHz tone for 1 hour continuously
    - [ ] Check telemetry: zero underruns, constant buffer fill, no memory leaks

- [ ] Run all integration tests
  - [ ] `pytest tests/integration/ -v --tb=short`

### 3.3. Performance Tests
**Priority:** MEDIUM (validate NFRs)  
**Estimated Time:** 1-2 hours

- [ ] Test CPU usage (`tests/performance/test_cpu_usage.py`)
  - [ ] ✅ Monitor CPU during tone generation (FS.md Section 10.3 example)
    - [ ] Target: <25% CPU during active tone generation
    - [ ] Target: <10% CPU idle
  - [ ] Measure CPU during WAV playback with resample
  - [ ] Measure CPU during frequency sweep

- [ ] Test memory usage (`tests/performance/test_memory_usage.py`)
  - [ ] Monitor memory during 5-minute tone generation
    - [ ] Target: <100 MB total Python process RSS
  - [ ] Check for memory leaks (measure at t=0, t=5min, verify stable)

- [ ] Test I2S timing accuracy (requires logic analyzer)
  - [ ] Verify BCLK frequency: 1.536 MHz ±50 ppm
  - [ ] Verify WS frequency: 48 kHz ±50 ppm
  - [ ] Verify BCLK/WS phase alignment

- [ ] Run performance validation script
  - [ ] `python tests/performance/monitor_resources.py --duration=300`

---

## Phase 4: Documentation and Deployment

### 4.1. Documentation
**Priority:** MEDIUM (important for handoff/future maintenance)  
**Estimated Time:** 2-3 hours

- [ ] Update `README.md` with comprehensive setup guide
  - [ ] Hardware requirements (RPi model, ESP32, wiring)
  - [ ] Software dependencies (Python 3.9+, pigpio/ALSA)
  - [ ] Installation steps (clone, venv, pip install, config UART)
  - [ ] Quick start guide (run main.py, access web UI)
  - [ ] Wiring diagram (ASCII art or link to image)
  - [ ] Troubleshooting section (common errors, solutions)

- [ ] Create `SETUP.md` with detailed Raspberry Pi setup
  - [ ] OS installation (Raspberry Pi Imager, Bookworm image)
  - [ ] Network configuration (WiFi setup, static IP)
  - [ ] UART configuration (disable Bluetooth on UART)
  - [ ] I2S configuration (enable I2S peripheral if needed)
  - [ ] GPIO permissions (add user to `gpio` group if needed)
  - [ ] Systemd service setup (auto-start on boot)

- [ ] Create `TESTING.md` with test execution guide
  - [ ] Unit test execution: `pytest tests/`
  - [ ] Integration test prerequisites (esp_bt_audio_source setup)
  - [ ] Performance test execution
  - [ ] Logic analyzer setup for I2S verification

- [ ] Add inline code documentation
  - [ ] Docstrings for all public classes and methods (NumPy style)
  - [ ] Type hints for function signatures
  - [ ] Comments for complex algorithms (phase accumulator, chirp generation)

### 4.2. Systemd Service (Auto-Start on Boot)
**Priority:** LOW (convenience feature)  
**Estimated Time:** 30 minutes

- [ ] Create systemd service file
  - [ ] Create `/etc/systemd/system/rpi-i2s-source.service` (FS.md Section 9.3)
  - [ ] Set `User=pi`, `WorkingDirectory=/home/pi/rpi_i2s_source`
  - [ ] Set `ExecStart=/home/pi/rpi_i2s_source/venv/bin/python main.py`
  - [ ] Set `After=network.target pigpiod.service`
  - [ ] Set `Requires=pigpiod.service` (if using pigpio)

- [ ] Enable and test service
  - [ ] `sudo systemctl daemon-reload`
  - [ ] `sudo systemctl enable rpi-i2s-source`
  - [ ] `sudo systemctl start rpi-i2s-source`
  - [ ] `sudo systemctl status rpi-i2s-source` (verify active)
  - [ ] Test: `sudo reboot`, verify auto-start after reboot
  - [ ] Test: Access web UI after reboot (http://<rpi-ip>:5000)

### 4.3. Deployment and Handoff
**Priority:** LOW (final steps)  
**Estimated Time:** 1 hour

- [ ] Create deployment checklist
  - [ ] Verify all dependencies installed
  - [ ] Verify UART wiring correct (loopback test if needed)
  - [ ] Verify I2S wiring correct (logic analyzer or oscilloscope)
  - [ ] Verify WiFi network connectivity
  - [ ] Verify esp_bt_audio_source powered on and responsive

- [ ] Performance validation on target hardware
  - [ ] Run 1-hour stability test (continuous tone generation)
  - [ ] Verify zero I2S underruns
  - [ ] Verify CPU <25%, memory <100 MB
  - [ ] Verify web UI responsive (<500 ms page loads)

- [ ] Create release archive
  - [ ] `tar -czf rpi_i2s_source_v1.0.tar.gz rpi_i2s_source/`
  - [ ] Include README, setup instructions, sample config.yaml
  - [ ] Tag Git commit: `git tag -a v1.0 -m "MVP release"`

---

## Phase 5: Future Enhancements (Post-MVP)

### 5.1. Nice-to-Have Features (Phase 1)
**Priority:** LOW (defer until MVP validated)  
**Estimated Time:** 2-4 hours each

- [ ] Multi-tone generator (play up to 4 simultaneous sine tones)
  - [ ] Update `_generate_tone()` to support multiple frequencies
  - [ ] Add web UI controls for each tone (freq, amp, phase)
  - [ ] Use case: Chord generation for audio testing

- [ ] WAV file upload via web UI
  - [ ] Add Flask endpoint: `POST /api/wav/upload` (multipart/form-data)
  - [ ] Save uploaded file to `/home/pi/audio/`
  - [ ] Validate file format (16-bit PCM WAV only)
  - [ ] Add drag-and-drop UI in `templates/index.html`

- [ ] Bluetooth device auto-connect
  - [ ] Save last connected MAC in `config.yaml` on successful connect
  - [ ] On boot: send `CONNECT <last_mac>` if `bluetooth.last_device_mac` not empty
  - [ ] Add web UI toggle: "Auto-connect on startup"

- [ ] Audio visualization (real-time waveform/FFT)
  - [ ] Add `/api/audio/samples` endpoint (return last 1024 samples)
  - [ ] Add JavaScript canvas or WebGL waveform renderer
  - [ ] Add FFT display (frequency spectrum, 0-24 kHz)
  - [ ] Update at 10-30 FPS (balance load vs responsiveness)

### 5.2. Advanced Testing Features (Phase 2)
**Priority:** LOW (research/validation tools)  
**Estimated Time:** 4-8 hours each

- [ ] THD+N measurement
  - [ ] Inject test signal (1 kHz tone)
  - [ ] Capture Bluetooth output via USB audio interface
  - [ ] Compute THD+N using NumPy FFT
  - [ ] Display result in web UI diagnostics page

- [ ] Latency measurement (round-trip)
  - [ ] Loopback test: Bluetooth speaker → USB microphone → Raspberry Pi
  - [ ] Detect tone start in microphone input
  - [ ] Measure time delta: I2S transmission → microphone detection
  - [ ] Display latency in web UI (target: <100 ms total)

- [ ] Automated test suite
  - [ ] Python script runs all test scenarios (tone, sweep, WAV, UART)
  - [ ] Generate pass/fail report (HTML or Markdown)
  - [ ] Include screenshots of web UI during tests
  - [ ] Email report on completion (optional)

---

## Milestone Tracking

### Milestone 1: Basic I2S Tone Generation (Target: Day 1, 4 hours)
- [ ] **Deliverables:**
  - [ ] Python script generates 1 kHz sine tone using NumPy
  - [ ] I2S master transmitter outputs tone to GPIO18/19/21 (via ALSA or pigpio)
  - [ ] Logic analyzer confirms BCLK = 1.536 MHz, WS = 48 kHz, valid PCM on DOUT
  - [ ] esp_bt_audio_source receives I2S stream, plays via Bluetooth speaker

- [ ] **Success Criteria:**
  - [ ] Tone audible on Bluetooth speaker (manual verification)
  - [ ] Zero I2S protocol errors on logic analyzer
  - [ ] Continuous playback for 5 minutes without dropouts

### Milestone 2: UART Command Interface (Target: Day 1-2, 4 hours)
- [ ] **Deliverables:**
  - [ ] pyserial UART communication to esp_bt_audio_source
  - [ ] Python class `UARTCommandInterface` with methods: `send_command()`, `parse_response()`, `wait_for_event()`
  - [ ] Command queue with timeout handling
  - [ ] Simple CLI test: send `STATUS` command, print response

- [ ] **Success Criteria:**
  - [ ] `STATUS` command returns valid response (OK|STATUS|...)
  - [ ] `VOLUME 75` command changes volume on Bluetooth speaker
  - [ ] Timeout handling works (unplug ESP32, verify 5-second timeout logged)

### Milestone 3: Flask Web UI (Target: Day 2, 6 hours)
- [ ] **Deliverables:**
  - [ ] Flask app with 3 pages: Dashboard, Bluetooth Control, Logs
  - [ ] Dashboard: Tone frequency/amplitude sliders, Start/Stop buttons
  - [ ] Bluetooth Control: SCAN/CONNECT/DISCONNECT buttons, device list
  - [ ] Real-time status updates via SSE (I2S active, buffer health, BT connection state)

- [ ] **Success Criteria:**
  - [ ] Web UI accessible from laptop on same LAN (http://<rpi-ip>:5000)
  - [ ] Tone frequency slider changes audio in <200 ms (user-perceived latency)
  - [ ] `SCAN` button triggers scan, results appear in device list within 10 seconds
  - [ ] Status panel updates connection state when Bluetooth device connects/disconnects

### Milestone 4: Advanced Audio Sources (Target: Day 2-3, 4 hours)
- [ ] **Deliverables:**
  - [ ] Frequency sweep generator (20 Hz → 20 kHz logarithmic chirp)
  - [ ] WAV file playback from `/home/pi/audio/` directory
  - [ ] Left/right channel identification mode (1 kHz left, 440 Hz right)
  - [ ] Web UI selectors for audio source, file picker for WAV files

- [ ] **Success Criteria:**
  - [ ] Frequency sweep plays smoothly from 20 Hz to 20 kHz over 10 seconds
  - [ ] WAV file (44.1 kHz) resampled to 48 kHz and plays correctly
  - [ ] Channel ID tones verify stereo routing (left speaker = 1 kHz, right = 440 Hz)

### Milestone 5: Stability and Telemetry (Target: Day 3, 2 hours)
- [ ] **Deliverables:**
  - [ ] 1-hour continuous tone test (no crashes, no underruns)
  - [ ] Telemetry dashboard in web UI (frames sent, underruns, CPU temp, memory usage)
  - [ ] Log rotation configured (10 MB max, 5 backups)
  - [ ] Systemd service for auto-start on boot

- [ ] **Success Criteria:**
  - [ ] 1-hour test: zero underruns, <100 MB memory, <25% CPU
  - [ ] Logs rotate correctly when exceeding 10 MB
  - [ ] Raspberry Pi survives reboot and auto-starts application within 30 seconds

### Final Acceptance Criteria
- [ ] **All milestones complete AND:**
  - [ ] I2S timing verified with logic analyzer (BCLK ±50 ppm, WS phase-locked)
  - [ ] esp_bt_audio_source Bluetooth pipeline validated end-to-end (scan, pair, connect, play)
  - [ ] Web UI responsive on LAN (<500 ms page load, <100 ms status updates)
  - [ ] Code documented (docstrings, README.md with setup instructions)
  - [ ] Pytest unit tests pass for core functions (tone generation, UART parser)
  - [ ] **Total development time: <16 hours** (2 days @ 8 hours/day)

---

## Progress Tracking

**Current Status:** 0% complete (0/5 milestones)

**Next Actions:**
1. Set up development environment (Phase 0)
2. Implement Ring Buffer (Phase 1.1)
3. Implement Config Manager (Phase 1.2)
4. Implement Audio Engine (Phase 1.4)
5. Implement I2S Driver - ALSA version (Phase 1.5, Option A)

**Estimated Time Remaining:** ~16 hours (based on Milestone targets)

---

## Notes and Decisions

### Decision Log
- **2026-02-06:** Start with ALSA I2S driver (simpler, faster MVP). Switch to pigpio only if ALSA proves inadequate for test jig needs.
- **2026-02-06:** Flask-SSE optional (can use polling fallback if SSE library issues). Prioritize working functionality over real-time updates.
- **2026-02-06:** Defer internet radio, authentication, AP mode to ESP32-S3 `esp_i2s_source` (out of scope for RPi test jig).

### Known Risks
- **UART reliability:** Serial port may disconnect if ESP32 reboots. Mitigation: Auto-reconnect logic in `UARTCommandManager`.
- **I2S timing:** ALSA driver timing accuracy unknown until tested. Mitigation: Have pigpio implementation as backup plan.
- **WAV resample performance:** 44.1 kHz → 48 kHz conversion may be CPU-intensive. Mitigation: Pre-convert WAV files to 48 kHz if needed.
- **Flask SSE browser compatibility:** SSE may not work in all browsers. Mitigation: Fallback to polling if SSE fails.

### Open Questions
- **Q1:** Which Raspberry Pi model to use for development?
  - **A:** Start with RPi 4 (4 GB) for comfortable headroom. Validate on RPi 3 B+ and Zero 2 W later.

- **Q2:** ALSA or pigpio for I2S?
  - **A:** Start with ALSA (simpler). Switch to pigpio if timing accuracy insufficient.

- **Q3:** Real-time status updates via SSE or polling?
  - **A:** Try SSE first (cleaner). Fallback to 500 ms polling if SSE issues.

- **Q4:** WAV file upload via web UI or manual copy?
  - **A:** Manual copy for MVP (SCP or USB). Add web upload in Phase 5.1 if needed.

---

**END OF TODO LIST**
