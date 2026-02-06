# Raspberry Pi I2S Source — TODO List

**Project:** rpi_i2s_source (Rapid development test jig for esp_bt_audio_source)  
**Target:** Working I2S audio in <2 days (~16 hours total)  
**Last Updated:** 2026-02-06

---

## Phase 0: Project Setup and Environment

### 0.1. Repository and Directory Structure
- [ ] Create directory structure per FS.md Section 9.1
  - [ ] Create `rpi_i2s_source/` root directory
  - [ ] Create `audio/` subdirectory with `__init__.py`
  - [ ] Create `uart/` subdirectory with `__init__.py`
  - [ ] Create `config/` subdirectory with `__init__.py`
  - [ ] Create `telemetry/` subdirectory with `__init__.py`
  - [ ] Create `web/` subdirectory with `__init__.py`
  - [ ] Create `web/static/css/` directory
  - [ ] Create `web/static/js/` directory
  - [ ] Create `web/templates/` directory
  - [ ] Create `tests/` subdirectory
  - [ ] Create `tests/integration/` subdirectory
  - [ ] Create `docs/` subdirectory (already exists)

### 0.2. Configuration Files
- [ ] Create `requirements.txt` with Python dependencies
  - [ ] Add Flask==3.0.0
  - [ ] Add pyserial==3.5
  - [ ] Add pigpio==1.78 (or comment out if using ALSA)
  - [ ] Add numpy==1.24.0
  - [ ] Add scipy==1.11.0
  - [ ] Add pyyaml==6.0
  - [ ] Add flask-sse==1.0.0 (optional)
  - [ ] Add psutil (for telemetry)
  - [ ] Add pytest (for testing)
  - [ ] Add pytest-mock (for mocking in tests)

- [ ] Create `config.yaml` template (default configuration)
  - [ ] Define I2S section (gpio_bclk, gpio_ws, gpio_dout, sample_rate, buffer_size)
  - [ ] Define UART section (device, baudrate, timeout)
  - [ ] Define audio section (default_source, tone_freq, tone_amp, wav_directory)
  - [ ] Define web section (port, bind_address, log_level)
  - [ ] Define bluetooth section (last_device_mac)

- [ ] Create `.gitignore`
  - [ ] Ignore `config.yaml` (user-specific)
  - [ ] Ignore `__pycache__/` and `*.pyc`
  - [ ] Ignore `.pytest_cache/`
  - [ ] Ignore `*.log`
  - [ ] Ignore `/venv/` and `/env/`

- [ ] Create `README.md` with setup instructions
  - [ ] Hardware requirements
  - [ ] Software dependencies
  - [ ] Installation steps
  - [ ] Quick start guide
  - [ ] Wiring diagram (RPi ↔ ESP32)

### 0.3. Development Environment Setup
- [ ] Install Raspberry Pi OS (Bookworm or later) on target RPi
- [ ] Install system packages
  - [ ] `sudo apt update && sudo apt upgrade`
  - [ ] `sudo apt install -y python3-pip python3-venv`
  - [ ] `sudo apt install -y pigpio` (if using pigpio)
  - [ ] `sudo systemctl enable pigpiod && sudo systemctl start pigpiod`
  - [ ] `sudo apt install -y alsa-utils` (if using ALSA)

- [ ] Configure UART (disable Bluetooth on UART if needed)
  - [ ] Edit `/boot/config.txt`: add `dtoverlay=disable-bt`
  - [ ] Reboot: `sudo reboot`
  - [ ] Verify `/dev/serial0` exists: `ls -l /dev/serial0`

- [ ] Create Python virtual environment
  - [ ] `cd /home/pi/rpi_i2s_source`
  - [ ] `python3 -m venv venv`
  - [ ] `source venv/bin/activate`
  - [ ] `pip install -r requirements.txt`

- [ ] Create audio directory
  - [ ] `mkdir -p /home/pi/audio`
  - [ ] Add sample WAV file for testing (e.g., `test_tone.wav`)

---

## Phase 1: Core Components Implementation

### 1.1. Ring Buffer (`audio/ring_buffer.py`)
**Priority:** HIGH (dependency for audio engine and I2S driver)  
**Estimated Time:** 1-2 hours

- [ ] Implement `RingBuffer` class (FS.md Section 2.4)
  - [ ] `__init__(capacity)`: Initialize buffer, read/write pointers, size, lock, event
  - [ ] `write(samples)`: Write samples with overflow handling (drop oldest)
  - [ ] `read(num_samples)`: Read samples, return None on underrun
  - [ ] `get_fill_percentage()`: Calculate buffer fill (0-100%)
  - [ ] `clear()`: Reset read/write pointers
  - [ ] Add thread safety with `threading.Lock`
  - [ ] Add refill signaling with `threading.Event`

- [ ] Test ring buffer independently
  - [ ] Unit test: write/read roundtrip (verify FIFO order)
  - [ ] Unit test: overflow handling (verify drop-oldest policy)
  - [ ] Unit test: underrun handling (verify None return)
  - [ ] Unit test: concurrent access (2 writers + 2 readers, FS.md Section 10.1)

### 1.2. Config Manager (`config/manager.py`)
**Priority:** HIGH (needed by all components)  
**Estimated Time:** 1-2 hours

- [ ] Implement `ConfigManager` class (FS.md Section 2.6)
  - [ ] Define `DEFAULT_CONFIG` dictionary (all sections with defaults)
  - [ ] `__init__(config_path)`: Load or create config file
  - [ ] `get(key_path)`: Get value by dot-separated path (e.g., "i2s.gpio_bclk")
  - [ ] `set(key_path, value)`: Set value by path
  - [ ] `save()`: Write config to YAML file
  - [ ] `reload()`: Reload config from file
  - [ ] `_load_or_create()`: Load existing or create default YAML
  - [ ] `_merge_with_defaults()`: Fill missing keys from defaults
  - [ ] `_validate(config)`: Validate GPIO pins, sample rate, buffer size, etc.

- [ ] Test config manager
  - [ ] Unit test: load default config (verify all sections present)
  - [ ] Unit test: validation (invalid GPIO pin raises ValueError)
  - [ ] Unit test: get/set with dot notation
  - [ ] Unit test: save/reload roundtrip (verify persistence)

### 1.3. Telemetry Tracker (`telemetry/tracker.py`)
**Priority:** MEDIUM (needed for web UI status)  
**Estimated Time:** 1 hour

- [ ] Implement `TelemetryTracker` class (FS.md Section 2.7)
  - [ ] `__init__()`: Initialize all stat dictionaries (i2s, uart, bt, audio, system)
  - [ ] `update_i2s(stats)`: Update I2S statistics
  - [ ] `update_uart(stats)`: Update UART statistics
  - [ ] `update_bt(stats)`: Update Bluetooth statistics
  - [ ] `update_audio(state)`: Update audio state
  - [ ] `get_full_status()`: Aggregate all stats into single JSON-serializable dict
  - [ ] `_get_cpu_temp()`: Read from `/sys/class/thermal/thermal_zone0/temp`
  - [ ] `_get_memory_usage()`: Use `psutil.Process().memory_info().rss`

- [ ] Test telemetry tracker
  - [ ] Unit test: update and retrieve stats (verify aggregation)
  - [ ] Unit test: CPU temperature reading (mock file read)
  - [ ] Unit test: memory usage reading (mock psutil)

### 1.4. Audio Engine (`audio/engine.py`)
**Priority:** HIGH (core audio generation)  
**Estimated Time:** 3-4 hours

- [ ] Implement `AudioEngine` class (FS.md Section 2.2)
  - [ ] `__init__(config, ring_buffer)`: Initialize parameters, phase accumulator
  - [ ] `start()`: Start audio generation thread
  - [ ] `stop()`: Stop audio generation thread (graceful shutdown)
  - [ ] `set_source(source, params)`: Switch between tone/sweep/wav/silence
  - [ ] `set_tone_params(freq, amp, mode)`: Update tone parameters (atomic, click-free)
  
  - [ ] **Internal Methods:**
    - [ ] `_generation_loop()`: Main background thread (check buffer, generate, write)
    - [ ] `_generate_next_chunk()`: Dispatch to tone/sweep/wav/silence generator
    - [ ] `_generate_tone()`: NumPy sine wave with phase accumulator (FS.md example)
      - [ ] Support mono, left-only, right-only, dual-tone modes
      - [ ] Interleave stereo: LRLRLR...
    - [ ] `_generate_sweep()`: Logarithmic chirp using `scipy.signal.chirp`
      - [ ] Track sweep position for continuous playback
      - [ ] Support loop mode
    - [ ] `_generate_wav()`: Read chunk from loaded WAV buffer
      - [ ] Handle EOF (stop or loop)
    - [ ] `_load_wav(filename)`: Load WAV file from `/home/pi/audio/`
      - [ ] Use `scipy.io.wavfile.read()`
      - [ ] Resample to 48 kHz if needed (`scipy.signal.resample`)
      - [ ] Convert to 16-bit stereo (mono → duplicate channels)
      - [ ] Raise `WAVNotFoundError` if file missing
      - [ ] Raise `WAVFormatError` if format unsupported

- [ ] Test audio engine
  - [ ] Unit test: tone frequency accuracy (FFT peak at expected freq, FS.md Section 10.1)
  - [ ] Unit test: tone amplitude (verify ±5% tolerance)
  - [ ] Unit test: phase continuity (no clicks when changing frequency)
  - [ ] Unit test: stereo modes (mono, left-only, right-only, dual-tone)
  - [ ] Unit test: WAV loading and resampling (44.1 kHz → 48 kHz)
  - [ ] Unit test: WAV file not found exception
  - [ ] Unit test: sweep generation (verify chirp parameters)

### 1.5. I2S Driver (`audio/i2s_driver.py`)
**Priority:** HIGH (critical for I2S output)  
**Estimated Time:** 4-6 hours (complex, hardware-dependent)

**Decision Point:** Choose implementation approach first.

- [ ] **Option A: ALSA Driver (Recommended for MVP)**
  - [ ] Implement `I2SDriverALSA` class (FS.md Section 2.3, simplified version)
    - [ ] `__init__(config, ring_buffer)`: Initialize ALSA device
      - [ ] `alsaaudio.PCM(alsaaudio.PCM_PLAYBACK, device='hw:0,0')`
      - [ ] Set channels=2, rate=48000, format=S16_LE, period size=1024
    - [ ] `start()`: Start DMA thread
    - [ ] `stop()`: Stop DMA thread, close ALSA device
    - [ ] `get_stats()`: Return frames_sent, underruns, buffer_fill_pct
    - [ ] `_dma_loop()`: Read from ring buffer, write to ALSA device
      - [ ] Handle underruns with zero-fill and counter increment
      - [ ] Block on `self.device.write(samples.tobytes())`
  
  - [ ] Test ALSA I2S driver
    - [ ] Integration test: generate tone → ring buffer → ALSA output
    - [ ] Verify I2S GPIO signals with logic analyzer (BCLK=1.536 MHz, WS=48 kHz)
    - [ ] Measure underruns during 5-minute continuous playback

- [ ] **Option B: pigpio Driver (Advanced, if ALSA insufficient)**
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

- [ ] Implement `UARTCommandManager` class (FS.md Section 2.5)
  - [ ] `__init__(config)`: Open serial port with pyserial
    - [ ] `serial.Serial(port, baudrate, timeout)`
    - [ ] Initialize command queue, response futures dict, event callbacks list
    - [ ] Initialize stats counters (sent, ok, err)
  
  - [ ] `start()`: Start UART receive thread
  - [ ] `stop()`: Stop RX thread, close serial port
  
  - [ ] `send_command(command, args)`: Send command, wait for response (blocking)
    - [ ] Create `concurrent.futures.Future` for response
    - [ ] Write command line: `f"{command} {args}\n"`
    - [ ] Wait for future result with timeout (5 seconds)
    - [ ] Return `{"status": "ok", "result": ...}` or `{"status": "error", ...}`
    - [ ] Raise `TimeoutError` if no response
  
  - [ ] `send_command_async(command, args, callback)`: Non-blocking send
  
  - [ ] `register_event_callback(callback)`: Add event handler function
  
  - [ ] `get_last_status()`: Return cached STATUS response
  
  - [ ] `get_stats()`: Return command statistics
  
  - [ ] **Internal Methods:**
    - [ ] `_rx_loop()`: Read bytes from serial port, accumulate until `\n`, parse line
      - [ ] Handle `serial.SerialException` with reconnect logic
    - [ ] `_process_line(line)`: Tokenize on `|`, dispatch to response or event handler
    - [ ] `_handle_response(line)`: Match response to pending future, resolve it
    - [ ] `_handle_event(parts)`: Parse EVENT|TYPE|SUBTYPE|DATA, call all callbacks
    - [ ] `_parse_status_response(line)`: Cache STATUS response for quick retrieval
    - [ ] `_reconnect()`: Attempt serial port reconnect (max 10 attempts, 5s delay)

- [ ] Test UART command manager
  - [ ] Unit test: command parsing (OK|SCAN|2 → status="ok", result=...)
  - [ ] Unit test: error parsing (ERR|CONNECT|NOT_FOUND → status="error")
  - [ ] Unit test: event parsing (EVENT|BT|CONNECTED|MAC → callback invoked)
  - [ ] Unit test: timeout handling (mock serial read timeout)
  - [ ] Integration test: send STATUS command to real esp_bt_audio_source
  - [ ] Integration test: async event handling (disconnect ESP32, verify event)

### 1.7. Flask Web Server (`web/app.py`)
**Priority:** MEDIUM (needed for UI control)  
**Estimated Time:** 4-5 hours

- [ ] Implement `WebServer` class (FS.md Section 2.1)
  - [ ] `__init__(config, audio_engine, uart_manager, telemetry)`: Store component refs
    - [ ] Create Flask app instance
    - [ ] Optionally create Flask-SSE instance (or use polling fallback)
    - [ ] Register all routes
  
  - [ ] `start()`: Start Flask server (blocking call)
    - [ ] `self.app.run(host=..., port=..., threaded=True)`
  
  - [ ] `stop()`: Gracefully shutdown server
  
  - [ ] **API Endpoints (Flask routes):**
    - [ ] `GET /api/status`: Return `telemetry.get_full_status()` as JSON
    - [ ] `POST /api/tone`: Parse JSON body, call `audio_engine.set_tone_params()`
      - [ ] Validate freq (20-20000), amp (0.0-1.0), mode (mono/left/right/dual)
      - [ ] Return `{"status": "ok"}` or error message
    - [ ] `POST /api/sweep`: Parse JSON, call `audio_engine.set_source('sweep', params)`
    - [ ] `POST /api/wav`: Parse JSON, call `audio_engine.set_source('wav', {'file': ...})`
      - [ ] Catch `WAVNotFoundError` → return 404 with message
      - [ ] Catch `WAVFormatError` → return 400 with message
    - [ ] `POST /api/bt/command`: Parse JSON, call `uart_manager.send_command()`
      - [ ] Return UART response or timeout error
    - [ ] `GET /api/bt/status`: Return `uart_manager.get_last_status()`
    - [ ] `GET /api/stream`: Server-Sent Events stream (or polling fallback)
      - [ ] Publish status updates every 500 ms (2 Hz)
      - [ ] Format: `data: <JSON>\n\n`
  
  - [ ] **Error Handling:**
    - [ ] Wrap all endpoints in try/except
    - [ ] Return meaningful JSON error messages
    - [ ] Log all exceptions with `logging.error(..., exc_info=True)`

- [ ] Test Flask web server
  - [ ] Unit test: Mock audio_engine/uart_manager, verify endpoint responses
  - [ ] Integration test: Start server, send POST /api/tone, verify tone params updated
  - [ ] Integration test: POST /api/bt/command, verify UART command sent
  - [ ] Integration test: GET /api/status, verify JSON structure

### 1.8. Frontend Web UI (`web/templates/` and `web/static/`)
**Priority:** MEDIUM (user interface)  
**Estimated Time:** 3-4 hours

- [ ] Create HTML templates (Jinja2)
  - [ ] `templates/base.html`: Base template with common header/footer
    - [ ] Include Bootstrap 5 CSS (CDN or local)
    - [ ] Include custom CSS: `<link href="/static/css/style.css">`
    - [ ] Include JavaScript: `<script src="/static/js/dashboard.js">`
  
  - [ ] `templates/index.html`: Main dashboard (extends base.html)
    - [ ] Audio source selector (radio buttons: Tone / Sweep / WAV / Silence)
    - [ ] Tone controls:
      - [ ] Frequency slider (20-20000 Hz, logarithmic scale)
      - [ ] Amplitude slider (0-100%)
      - [ ] Stereo mode dropdown (Mono / Left / Right / Dual)
      - [ ] Start/Stop buttons
    - [ ] Sweep controls:
      - [ ] Duration selector (5s / 10s / 30s / 60s)
      - [ ] Loop checkbox
      - [ ] Start/Stop buttons
    - [ ] WAV file controls:
      - [ ] File selector dropdown (populate from /home/pi/audio/)
      - [ ] Play/Stop buttons
    - [ ] I2S status panel:
      - [ ] Active/Stopped indicator
      - [ ] Sample rate (48000 Hz)
      - [ ] Buffer fill percentage (progress bar)
      - [ ] Underruns counter
  
  - [ ] `templates/bluetooth.html`: Bluetooth control panel
    - [ ] Command buttons: SCAN, DISCONNECT, START, STOP
    - [ ] Device list (populate from SCAN results)
    - [ ] CONNECT button with MAC address input
    - [ ] Volume slider (0-100%)
    - [ ] Status display: Connection state, device MAC, playback state
  
  - [ ] `templates/logs.html`: Log viewer and diagnostics
    - [ ] Real-time log viewer (last 200 lines, auto-scroll)
    - [ ] UART command/response history (last 50 transactions)
    - [ ] I2S statistics table (frames sent, underruns, peak amplitude, uptime)
    - [ ] System info: RPi model, CPU temp, memory usage

- [ ] Create CSS (`static/css/style.css`)
  - [ ] Custom styling for dashboard layout
  - [ ] Slider styles (frequency/amplitude)
  - [ ] Status indicator colors (green=active, red=stopped, yellow=error)
  - [ ] Progress bar styling (buffer fill)

- [ ] Create JavaScript (`static/js/dashboard.js`)
  - [ ] Initialize SSE connection to `/api/stream` (or use polling fallback)
  - [ ] Update status panel every 500 ms (I2S active, buffer fill, BT connected, etc.)
  - [ ] Tone controls:
    - [ ] Frequency slider: POST to `/api/tone` on change (debounce 200ms)
    - [ ] Amplitude slider: POST to `/api/tone` on change
    - [ ] Start button: POST `/api/tone` with current params
  - [ ] WAV controls:
    - [ ] Populate file dropdown: GET `/home/pi/audio/` file list (or hardcode)
    - [ ] Play button: POST `/api/wav` with selected file
  - [ ] Bluetooth controls:
    - [ ] SCAN button: POST `/api/bt/command {"command":"SCAN"}`
    - [ ] Parse scan results, populate device list
    - [ ] CONNECT button: POST `/api/bt/command {"command":"CONNECT", "args":"<MAC>"}`
    - [ ] Volume slider: POST `/api/bt/command {"command":"VOLUME", "args":"<pct>"}`
  - [ ] Error handling: Display error messages in status bar or alert dialog

- [ ] Test web UI
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
