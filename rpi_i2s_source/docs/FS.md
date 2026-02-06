# Raspberry Pi I2S Source — Functional Specification (FS)

## 1. System Architecture

### 1.1. Architecture Overview

**System Context:**

```
┌─────────────────────────────────────────────────────────────────┐
│                    Raspberry Pi (rpi_i2s_source)                │
│                                                                 │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐       │
│  │ Flask Web    │◄─►│ Audio Engine │◄─►│ I2S Driver   │───────┼──► I2S (48kHz/16-bit/stereo)
│  │ Server       │   │ (Tone/WAV/   │   │ (pigpio)     │       │       to esp_bt_audio_source
│  │              │   │  Sweep Gen)  │   │              │       │
│  └──────────────┘   └──────────────┘   └──────────────┘       │
│         │                   │                                  │
│         │            ┌──────▼────────┐                         │
│         │            │ Ring Buffer   │                         │
│         │            │ (8192 samples)│                         │
│         │            └───────────────┘                         │
│         │                                                      │
│  ┌──────▼──────────────────────────────────────┐              │
│  │ UART Command Manager                        │──────────────┼──► UART (115200 8N1)
│  │ (send commands, parse responses, events)    │◄─────────────┼──► to esp_bt_audio_source
│  └─────────────────────────────────────────────┘              │
│                                                                 │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐       │
│  │ Config       │   │ Telemetry    │   │ Logging      │       │
│  │ Manager      │   │ Tracker      │   │ (rotating)   │       │
│  │ (YAML)       │   │              │   │              │       │
│  └──────────────┘   └──────────────┘   └──────────────┘       │
└─────────────────────────────────────────────────────────────────┘
          ▲
          │ HTTP (LAN)
          │
    ┌─────┴────────┐
    │  Web Browser │
    │  (Laptop/PC) │
    └──────────────┘
```

**Component Ownership:**

| Component | Module/File | Responsibilities | External Dependencies |
|-----------|-------------|------------------|----------------------|
| **Flask Web Server** | `web/app.py` | HTTP endpoints, SSE, static files, API routing | Flask, Flask-SSE |
| **Audio Engine** | `audio/engine.py` | Tone/sweep/WAV generation, format conversion | NumPy, SciPy |
| **I2S Driver** | `audio/i2s_driver.py` | I2S master clock/data generation, DMA buffer mgmt | pigpio |
| **Ring Buffer** | `audio/ring_buffer.py` | Thread-safe circular buffer, refill signaling | threading |
| **UART Command Manager** | `uart/command_manager.py` | Command queue, response parsing, event handling | pyserial, threading |
| **Config Manager** | `config/manager.py` | YAML load/save, validation, defaults | PyYAML |
| **Telemetry Tracker** | `telemetry/tracker.py` | Metrics collection, statistics, system monitoring | psutil |
| **Logging** | `utils/logging_config.py` | Structured logging, rotation, formatting | logging |

---

## 2. Component Specifications

### 2.1. Flask Web Server (`web/app.py`)

**Purpose:** Serve web UI, expose REST API, provide real-time status updates via SSE.

**Public API:**

```python
class WebServer:
    """Flask-based web server for control and monitoring."""
    
    def __init__(self, config: Dict[str, Any], 
                 audio_engine: 'AudioEngine',
                 uart_manager: 'UARTCommandManager',
                 telemetry: 'TelemetryTracker'):
        """Initialize web server with references to other components."""
        self.app = Flask(__name__)
        self.sse = SSE(self.app)
        self._register_routes()
    
    def start(self) -> None:
        """Start Flask server (blocking call)."""
        # Runs in main thread or via threading.Thread
        self.app.run(
            host=self.config['web']['bind_address'],
            port=self.config['web']['port'],
            threaded=True
        )
    
    def stop(self) -> None:
        """Gracefully shutdown server."""
        # Trigger shutdown via request.environ.get('werkzeug.server.shutdown')
        pass
    
    # --- API Endpoints (Flask routes) ---
    
    @app.route('/api/status', methods=['GET'])
    def get_status(self) -> flask.Response:
        """
        Get complete system status.
        
        Returns:
            JSON: {
                "i2s": {"active": bool, "sample_rate": int, ...},
                "audio": {"source": str, "tone_freq": int, ...},
                "uart": {"connected": bool, "commands_sent": int, ...},
                "bt": {"connected": bool, "device_mac": str, ...},
                "system": {"uptime": int, "cpu_temp": float, ...}
            }
        """
        pass
    
    @app.route('/api/tone', methods=['POST'])
    def set_tone_params(self) -> flask.Response:
        """
        Update tone generator parameters.
        
        Request Body (JSON):
            {"freq": 1000, "amp": 0.75, "mode": "mono"}
        
        Returns:
            JSON: {"status": "ok"} or {"status": "error", "message": "..."}
        """
        pass
    
    @app.route('/api/sweep', methods=['POST'])
    def start_sweep(self) -> flask.Response:
        """
        Start frequency sweep.
        
        Request Body (JSON):
            {"start": 20, "end": 20000, "duration": 10, "loop": false}
        """
        pass
    
    @app.route('/api/wav', methods=['POST'])
    def play_wav(self) -> flask.Response:
        """
        Play WAV file from /home/pi/audio/.
        
        Request Body (JSON):
            {"file": "test_tone.wav"}
        """
        pass
    
    @app.route('/api/bt/command', methods=['POST'])
    def send_bt_command(self) -> flask.Response:
        """
        Send UART command to esp_bt_audio_source.
        
        Request Body (JSON):
            {"command": "VOLUME", "args": "75"}
        
        Returns:
            JSON: {"status": "ok", "result": "OK|VOLUME|75"}
                  or {"status": "error", "result": "ERR|VOLUME|..."}
        """
        pass
    
    @app.route('/api/stream')
    def sse_stream(self) -> Generator:
        """
        Server-Sent Events stream for real-time status updates.
        
        Publishes every 500ms (2 Hz):
            data: {"i2s": {...}, "audio": {...}, ...}
        """
        pass
```

**Frontend Pages:**

- **`/` (index.html):** Main dashboard with audio controls
- **`/bluetooth` (bluetooth.html):** Bluetooth command panel
- **`/logs` (logs.html):** Real-time log viewer and diagnostics

**Static Files:** `web/static/` (CSS, JavaScript, images)

**Templates:** `web/templates/` (Jinja2 HTML templates)

---

### 2.2. Audio Engine (`audio/engine.py`)

**Purpose:** Generate PCM audio samples (tones, sweeps, WAV playback) and feed to ring buffer.

**Public API:**

```python
class AudioEngine:
    """Audio generation engine supporting tones, sweeps, and WAV playback."""
    
    def __init__(self, config: Dict[str, Any], ring_buffer: 'RingBuffer'):
        """Initialize audio engine with configuration and output buffer."""
        self.sample_rate = 48000
        self.buffer_size = config['i2s']['buffer_size']
        self.ring_buffer = ring_buffer
        self._generator_thread = None
        self._stop_event = threading.Event()
        self._current_source = 'silence'
        self._tone_params = {'freq': 1000, 'amp': 0.75, 'mode': 'mono'}
        self._phase_accumulator = 0.0  # For click-free frequency changes
    
    def start(self) -> None:
        """Start audio generation thread."""
        self._stop_event.clear()
        self._generator_thread = threading.Thread(target=self._generation_loop)
        self._generator_thread.start()
    
    def stop(self) -> None:
        """Stop audio generation thread."""
        self._stop_event.set()
        if self._generator_thread:
            self._generator_thread.join(timeout=2.0)
    
    def set_source(self, source: str, params: Optional[Dict] = None) -> None:
        """
        Switch audio source.
        
        Args:
            source: One of ["tone", "sweep", "wav", "silence"]
            params: Source-specific parameters
        """
        with self._lock:
            self._current_source = source
            if source == 'tone' and params:
                self._update_tone_params(params)
            elif source == 'sweep' and params:
                self._setup_sweep(params)
            elif source == 'wav' and params:
                self._load_wav(params['file'])
    
    def set_tone_params(self, freq: int, amp: float, mode: str) -> None:
        """
        Update tone generator parameters without clicks/pops.
        
        Args:
            freq: Frequency in Hz (20-20000)
            amp: Amplitude 0.0-1.0
            mode: "mono", "left", "right", "dual" (different freq per channel)
        """
        # Atomic update using phase accumulator
        with self._lock:
            self._tone_params = {
                'freq': np.clip(freq, 20, 20000),
                'amp': np.clip(amp, 0.0, 1.0),
                'mode': mode
            }
    
    # --- Internal Methods ---
    
    def _generation_loop(self) -> None:
        """Main audio generation loop (runs in background thread)."""
        while not self._stop_event.is_set():
            # Check if ring buffer needs refill (< 50% full)
            if self.ring_buffer.get_fill_percentage() < 50.0:
                samples = self._generate_next_chunk()
                self.ring_buffer.write(samples)
            else:
                time.sleep(0.001)  # 1ms sleep to avoid busy-wait
    
    def _generate_next_chunk(self) -> np.ndarray:
        """
        Generate next buffer of PCM samples based on current source.
        
        Returns:
            np.ndarray: Interleaved stereo int16 samples (LRLRLR...)
        """
        if self._current_source == 'tone':
            return self._generate_tone()
        elif self._current_source == 'sweep':
            return self._generate_sweep()
        elif self._current_source == 'wav':
            return self._generate_wav()
        else:  # silence
            return np.zeros(self.buffer_size, dtype=np.int16)
    
    def _generate_tone(self) -> np.ndarray:
        """
        Generate sine tone with phase continuity (click-free).
        
        Uses phase accumulator to avoid discontinuities when freq changes.
        """
        freq = self._tone_params['freq']
        amp = self._tone_params['amp']
        mode = self._tone_params['mode']
        
        num_frames = self.buffer_size // 2  # Stereo frames
        phase_increment = 2 * np.pi * freq / self.sample_rate
        
        # Generate samples using phase accumulator
        phases = (self._phase_accumulator + 
                  np.arange(num_frames) * phase_increment) % (2 * np.pi)
        self._phase_accumulator = phases[-1] + phase_increment
        
        # Generate left/right channels
        if mode == 'mono':
            left = (amp * 32767 * np.sin(phases)).astype(np.int16)
            right = left.copy()
        elif mode == 'left':
            left = (amp * 32767 * np.sin(phases)).astype(np.int16)
            right = np.zeros(num_frames, dtype=np.int16)
        elif mode == 'right':
            left = np.zeros(num_frames, dtype=np.int16)
            right = (amp * 32767 * np.sin(phases)).astype(np.int16)
        elif mode == 'dual':
            # Example: 1kHz left, 440Hz right (configurable)
            left = (amp * 32767 * np.sin(phases)).astype(np.int16)
            phases_right = (np.arange(num_frames) * 2 * np.pi * 440 / 
                           self.sample_rate) % (2 * np.pi)
            right = (amp * 32767 * np.sin(phases_right)).astype(np.int16)
        
        # Interleave stereo: LRLRLR...
        stereo = np.empty(self.buffer_size, dtype=np.int16)
        stereo[0::2] = left
        stereo[1::2] = right
        
        return stereo
    
    def _generate_sweep(self) -> np.ndarray:
        """Generate logarithmic frequency sweep chunk."""
        # Use scipy.signal.chirp with state tracking for continuous sweep
        # Implementation details omitted for brevity
        pass
    
    def _generate_wav(self) -> np.ndarray:
        """Read next chunk from loaded WAV file buffer."""
        # Read from pre-loaded WAV buffer, handle EOF, loop if needed
        pass
    
    def _load_wav(self, filename: str) -> None:
        """
        Load WAV file from /home/pi/audio/ and resample to 48kHz if needed.
        
        Args:
            filename: WAV file name (e.g., "test_tone.wav")
        
        Raises:
            FileNotFoundError: WAV file not found
            ValueError: WAV format unsupported (not 16-bit PCM)
        """
        import scipy.io.wavfile as wav
        from scipy.signal import resample
        
        filepath = os.path.join('/home/pi/audio', filename)
        sample_rate_wav, audio = wav.read(filepath)
        
        # Resample to 48 kHz if needed
        if sample_rate_wav != 48000:
            num_samples_new = int(len(audio) * 48000 / sample_rate_wav)
            audio = resample(audio, num_samples_new)
        
        # Convert to 16-bit stereo
        audio = audio.astype(np.int16)
        if audio.ndim == 1:  # Mono → duplicate to stereo
            audio = np.column_stack((audio, audio)).flatten()
        
        self._wav_buffer = audio
        self._wav_position = 0
```

---

### 2.3. I2S Driver (`audio/i2s_driver.py`)

**Purpose:** Generate I2S master clocks (BCLK, WS) and transmit PCM data (DOUT) using pigpio.

**Public API:**

```python
class I2SDriver:
    """I2S master transmitter using pigpio library."""
    
    def __init__(self, config: Dict[str, Any], ring_buffer: 'RingBuffer'):
        """
        Initialize I2S driver.
        
        Args:
            config: Dictionary with 'i2s' section (gpio_bclk, gpio_ws, gpio_dout)
            ring_buffer: Source of PCM samples
        """
        self.gpio_bclk = config['i2s']['gpio_bclk']  # BCM 18
        self.gpio_ws = config['i2s']['gpio_ws']      # BCM 19
        self.gpio_dout = config['i2s']['gpio_dout']  # BCM 21
        self.sample_rate = config['i2s']['sample_rate']  # 48000
        self.ring_buffer = ring_buffer
        
        # Connect to pigpiod daemon
        self.pi = pigpio.pi()
        if not self.pi.connected:
            raise RuntimeError("Cannot connect to pigpiod daemon")
        
        self._dma_thread = None
        self._stop_event = threading.Event()
        self._frames_sent = 0
        self._underruns = 0
    
    def start(self) -> None:
        """Start I2S transmission (generate BCLK/WS/DOUT)."""
        self._setup_i2s_waveforms()
        self._stop_event.clear()
        self._dma_thread = threading.Thread(target=self._dma_loop)
        self._dma_thread.start()
    
    def stop(self) -> None:
        """Stop I2S transmission."""
        self._stop_event.set()
        if self._dma_thread:
            self._dma_thread.join(timeout=2.0)
        self.pi.wave_tx_stop()
        self.pi.stop()
    
    def get_stats(self) -> Dict[str, int]:
        """
        Get I2S transmission statistics.
        
        Returns:
            {"frames_sent": int, "underruns": int, "buffer_fill_pct": float}
        """
        return {
            'frames_sent': self._frames_sent,
            'underruns': self._underruns,
            'buffer_fill_pct': self.ring_buffer.get_fill_percentage()
        }
    
    # --- Internal Methods ---
    
    def _setup_i2s_waveforms(self) -> None:
        """
        Configure pigpio to generate I2S clock waveforms.
        
        BCLK: 1.536 MHz (48 kHz × 2 channels × 16 bits)
        WS:   48 kHz (LOW = left channel, HIGH = right channel)
        DOUT: PCM data, MSB-first, left-aligned
        """
        # Set GPIO modes
        self.pi.set_mode(self.gpio_bclk, pigpio.OUTPUT)
        self.pi.set_mode(self.gpio_ws, pigpio.OUTPUT)
        self.pi.set_mode(self.gpio_dout, pigpio.OUTPUT)
        
        # Generate BCLK waveform (1.536 MHz square wave)
        # pigpio.wave_add_generic() to create precise timing
        # Implementation details: use hardware PWM or wave pulses
        # (Simplified here; full implementation uses wave_add_generic)
        
        bclk_period_us = 1.0 / 1536000 * 1e6  # ~0.651 us
        ws_period_us = 1.0 / 48000 * 1e6      # ~20.833 us
        
        # Create waveform for one WS period (32 BCLK cycles)
        # Left channel (16 bits) + Right channel (16 bits)
        # Each bit transmitted on BCLK edge
        
        # Pseudocode (actual implementation uses pigpio wave API):
        # wf = []
        # for bit in range(32):  # 16 bits × 2 channels
        #     wf.append(pigpio.pulse(1<<gpio_bclk, 0, bclk_period_us/2))
        #     wf.append(pigpio.pulse(0, 1<<gpio_bclk, bclk_period_us/2))
        #     # Set DOUT to bit value
        #     # Toggle WS at bit 16
        # self.pi.wave_add_generic(wf)
        # wave_id = self.pi.wave_create()
        # self.pi.wave_send_repeat(wave_id)
    
    def _dma_loop(self) -> None:
        """
        Main DMA loop: read samples from ring buffer, transmit via I2S.
        
        Runs at 48 kHz frame rate (one stereo frame every 20.833 us).
        """
        while not self._stop_event.is_set():
            # Read one stereo frame (2 samples: left + right)
            samples = self.ring_buffer.read(2)  # Returns [L, R] int16
            
            if samples is None:
                # Buffer underrun
                self._underruns += 1
                samples = np.zeros(2, dtype=np.int16)  # Zero-fill
            
            # Transmit samples via I2S (update DOUT GPIO for each bit)
            self._transmit_frame(samples)
            self._frames_sent += 1
    
    def _transmit_frame(self, samples: np.ndarray) -> None:
        """
        Transmit one stereo frame (2 × 16-bit samples) via I2S.
        
        Args:
            samples: [left_sample, right_sample] int16 array
        """
        # Convert samples to bits and toggle DOUT GPIO
        # Synchronized with BCLK/WS waveforms generated by pigpio
        # (Actual implementation uses pigpio wave chaining or direct GPIO bit-banging)
        pass
```

**Note:** The I2S driver implementation using `pigpio` is complex and requires precise timing. The above is a simplified API; full implementation uses `pigpio.wave_*` functions or custom bit-banging. See `pigpio` documentation for waveform generation examples.

**Alternative (Simpler):** Use ALSA `snd_bcm2835` driver:

```python
import alsaaudio

class I2SDriverALSA:
    """I2S master transmitter using ALSA driver (simpler but less flexible)."""
    
    def __init__(self, config: Dict[str, Any], ring_buffer: 'RingBuffer'):
        self.device = alsaaudio.PCM(alsaaudio.PCM_PLAYBACK, device='hw:0,0')
        self.device.setchannels(2)      # Stereo
        self.device.setrate(48000)      # 48 kHz
        self.device.setformat(alsaaudio.PCM_FORMAT_S16_LE)  # 16-bit little-endian
        self.device.setperiodsize(1024) # Buffer size
        self.ring_buffer = ring_buffer
    
    def start(self) -> None:
        """Start I2S transmission via ALSA."""
        self._dma_thread = threading.Thread(target=self._dma_loop)
        self._dma_thread.start()
    
    def _dma_loop(self) -> None:
        """Read from ring buffer, write to ALSA device."""
        while not self._stop_event.is_set():
            samples = self.ring_buffer.read(1024)
            if samples is not None:
                self.device.write(samples.tobytes())
            else:
                time.sleep(0.001)
```

**Decision:** Start with ALSA for simplicity; switch to `pigpio` if need more control.

---

### 2.4. Ring Buffer (`audio/ring_buffer.py`)

**Purpose:** Thread-safe circular buffer for audio samples with refill signaling.

**Public API:**

```python
class RingBuffer:
    """Thread-safe circular buffer for audio samples."""
    
    def __init__(self, capacity: int):
        """
        Initialize ring buffer.
        
        Args:
            capacity: Buffer size in samples (e.g., 8192)
        """
        self.capacity = capacity
        self._buffer = np.zeros(capacity, dtype=np.int16)
        self._write_pos = 0
        self._read_pos = 0
        self._size = 0  # Current fill level
        self._lock = threading.Lock()
        self._refill_event = threading.Event()
    
    def write(self, samples: np.ndarray) -> None:
        """
        Write samples to buffer (producer: audio engine).
        
        Args:
            samples: int16 array of samples to write
        
        Raises:
            BufferOverflowError: If buffer full (drop oldest if policy allows)
        """
        with self._lock:
            num_samples = len(samples)
            if self._size + num_samples > self.capacity:
                # Overflow: drop oldest samples
                overflow = (self._size + num_samples) - self.capacity
                self._read_pos = (self._read_pos + overflow) % self.capacity
                self._size -= overflow
            
            # Write samples (handle wrap-around)
            end_pos = self._write_pos + num_samples
            if end_pos <= self.capacity:
                self._buffer[self._write_pos:end_pos] = samples
            else:
                split = self.capacity - self._write_pos
                self._buffer[self._write_pos:] = samples[:split]
                self._buffer[:end_pos % self.capacity] = samples[split:]
            
            self._write_pos = end_pos % self.capacity
            self._size += num_samples
    
    def read(self, num_samples: int) -> Optional[np.ndarray]:
        """
        Read samples from buffer (consumer: I2S driver).
        
        Args:
            num_samples: Number of samples to read
        
        Returns:
            np.ndarray of int16 samples, or None if buffer empty (underrun)
        """
        with self._lock:
            if self._size < num_samples:
                # Underrun
                return None
            
            # Read samples (handle wrap-around)
            end_pos = self._read_pos + num_samples
            if end_pos <= self.capacity:
                result = self._buffer[self._read_pos:end_pos].copy()
            else:
                split = self.capacity - self._read_pos
                result = np.concatenate([
                    self._buffer[self._read_pos:],
                    self._buffer[:end_pos % self.capacity]
                ])
            
            self._read_pos = end_pos % self.capacity
            self._size -= num_samples
            
            # Signal refill if buffer < 50% full
            if self._size < self.capacity / 2:
                self._refill_event.set()
            
            return result
    
    def get_fill_percentage(self) -> float:
        """Get current buffer fill level as percentage (0-100)."""
        with self._lock:
            return (self._size / self.capacity) * 100.0
    
    def clear(self) -> None:
        """Clear buffer (reset read/write pointers)."""
        with self._lock:
            self._read_pos = 0
            self._write_pos = 0
            self._size = 0
```

---

### 2.5. UART Command Manager (`uart/command_manager.py`)

**Purpose:** Send commands to `esp_bt_audio_source` via UART, parse responses, handle events.

**Public API:**

```python
class UARTCommandManager:
    """UART command interface to esp_bt_audio_source."""
    
    def __init__(self, config: Dict[str, Any]):
        """
        Initialize UART manager.
        
        Args:
            config: Dictionary with 'uart' section (device, baudrate, timeout)
        """
        self.device = config['uart']['device']  # "/dev/serial0"
        self.baudrate = config['uart']['baudrate']  # 115200
        self.timeout = config['uart']['timeout']  # 5.0 seconds
        
        self._serial = serial.Serial(
            port=self.device,
            baudrate=self.baudrate,
            timeout=self.timeout
        )
        
        self._rx_thread = None
        self._stop_event = threading.Event()
        self._command_queue = queue.Queue()
        self._response_futures = {}  # cmd_id -> Future
        self._event_callbacks = []   # List of event handler functions
        self._last_status = {}
        self._stats = {'sent': 0, 'ok': 0, 'err': 0}
    
    def start(self) -> None:
        """Start UART receive thread."""
        self._stop_event.clear()
        self._rx_thread = threading.Thread(target=self._rx_loop)
        self._rx_thread.start()
    
    def stop(self) -> None:
        """Stop UART communication."""
        self._stop_event.set()
        if self._rx_thread:
            self._rx_thread.join(timeout=2.0)
        self._serial.close()
    
    def send_command(self, command: str, args: str = "") -> Dict[str, str]:
        """
        Send command to esp_bt_audio_source and wait for response.
        
        Args:
            command: Command name (e.g., "SCAN", "CONNECT", "VOLUME")
            args: Command arguments (e.g., "AA:BB:CC:DD:EE:FF", "75")
        
        Returns:
            {"status": "ok", "result": "OK|COMMAND|DATA"}
            or {"status": "error", "result": "ERR|COMMAND|CODE|MESSAGE"}
        
        Raises:
            TimeoutError: No response within timeout period
            serial.SerialException: UART communication error
        """
        cmd_line = f"{command} {args}\n" if args else f"{command}\n"
        cmd_id = id(cmd_line)  # Unique ID for matching response
        
        # Create future for response
        future = concurrent.futures.Future()
        self._response_futures[cmd_id] = future
        
        # Send command
        try:
            self._serial.write(cmd_line.encode('utf-8'))
            self._stats['sent'] += 1
        except serial.SerialException as e:
            del self._response_futures[cmd_id]
            raise
        
        # Wait for response (blocking with timeout)
        try:
            response = future.result(timeout=self.timeout)
            if response.startswith('OK|'):
                self._stats['ok'] += 1
                return {"status": "ok", "result": response}
            else:
                self._stats['err'] += 1
                return {"status": "error", "result": response}
        except concurrent.futures.TimeoutError:
            del self._response_futures[cmd_id]
            raise TimeoutError(f"Command timeout: {command}")
    
    def send_command_async(self, command: str, args: str = "",
                          callback: Optional[Callable] = None) -> None:
        """
        Send command asynchronously (non-blocking).
        
        Args:
            command: Command name
            args: Command arguments
            callback: Optional callback(response: Dict) when response received
        """
        threading.Thread(target=self._async_send, args=(command, args, callback)).start()
    
    def register_event_callback(self, callback: Callable[[Dict], None]) -> None:
        """
        Register callback for asynchronous events from esp_bt_audio_source.
        
        Args:
            callback: Function called with event dict when EVENT| received
                      Signature: callback({"type": str, "subtype": str, "data": str})
        """
        self._event_callbacks.append(callback)
    
    def get_last_status(self) -> Dict[str, Any]:
        """
        Get last STATUS response (cached).
        
        Returns:
            {"connected": bool, "device": str, "playing": bool, "source": str}
        """
        return self._last_status.copy()
    
    def get_stats(self) -> Dict[str, int]:
        """Get UART statistics (commands sent, ok, errors)."""
        return self._stats.copy()
    
    # --- Internal Methods ---
    
    def _rx_loop(self) -> None:
        """Receive thread: read lines from UART, parse responses/events."""
        line_buffer = ""
        
        while not self._stop_event.is_set():
            try:
                # Read one byte at a time until newline
                byte = self._serial.read(1)
                if not byte:
                    continue
                
                char = byte.decode('utf-8', errors='ignore')
                if char == '\n':
                    # Complete line received
                    self._process_line(line_buffer.strip())
                    line_buffer = ""
                else:
                    line_buffer += char
            
            except serial.SerialException as e:
                logging.error(f"UART read error: {e}")
                time.sleep(1.0)  # Retry after delay
    
    def _process_line(self, line: str) -> None:
        """
        Parse received line (response or event).
        
        Args:
            line: Line received from esp_bt_audio_source (no newline)
        """
        if not line:
            return
        
        parts = line.split('|')
        if len(parts) < 2:
            logging.warning(f"Malformed UART line: {line}")
            return
        
        msg_type = parts[0]
        
        if msg_type in ['OK', 'ERR']:
            # Response to command
            self._handle_response(line)
        elif msg_type == 'EVENT':
            # Asynchronous event
            self._handle_event(parts)
        else:
            logging.warning(f"Unknown UART message type: {msg_type}")
    
    def _handle_response(self, line: str) -> None:
        """
        Match response to pending command and resolve future.
        
        Args:
            line: Full response line (e.g., "OK|SCAN|2")
        """
        # Simple matching: resolve first pending future (FIFO order)
        # More robust: match by command name in response
        if self._response_futures:
            cmd_id, future = self._response_futures.popitem()
            future.set_result(line)
        
        # Update cached status if this was a STATUS response
        if 'STATUS' in line:
            self._parse_status_response(line)
    
    def _handle_event(self, parts: List[str]) -> None:
        """
        Process asynchronous EVENT| message.
        
        Args:
            parts: Split event line (e.g., ["EVENT", "BT", "CONNECTED", "AA:BB:..."])
        """
        event = {
            'type': parts[1] if len(parts) > 1 else '',
            'subtype': parts[2] if len(parts) > 2 else '',
            'data': parts[3] if len(parts) > 3 else ''
        }
        
        # Call all registered callbacks
        for callback in self._event_callbacks:
            try:
                callback(event)
            except Exception as e:
                logging.error(f"Event callback error: {e}")
    
    def _parse_status_response(self, line: str) -> None:
        """Parse STATUS response and cache result."""
        # Example: "OK|STATUS|connected:true,device:AA:BB:...,playing:true,source:i2s"
        # Parse into dictionary and cache
        # (Implementation details omitted for brevity)
        pass
    
    def _async_send(self, command: str, args: str, callback: Optional[Callable]) -> None:
        """Helper for async command sending."""
        try:
            response = self.send_command(command, args)
            if callback:
                callback(response)
        except Exception as e:
            logging.error(f"Async command error: {e}")
```

---

### 2.6. Config Manager (`config/manager.py`)

**Purpose:** Load/save YAML configuration, validate values, provide defaults.

**Public API:**

```python
class ConfigManager:
    """Configuration manager for YAML settings."""
    
    DEFAULT_CONFIG = {
        'i2s': {
            'gpio_bclk': 18,
            'gpio_ws': 19,
            'gpio_dout': 21,
            'sample_rate': 48000,
            'buffer_size': 8192
        },
        'uart': {
            'device': '/dev/serial0',
            'baudrate': 115200,
            'timeout': 5.0
        },
        'audio': {
            'default_source': 'tone',
            'tone_freq': 1000,
            'tone_amp': 0.75,
            'wav_directory': '/home/pi/audio'
        },
        'web': {
            'port': 5000,
            'bind_address': '0.0.0.0',
            'log_level': 'INFO'
        },
        'bluetooth': {
            'last_device_mac': ''
        }
    }
    
    def __init__(self, config_path: str = '/home/pi/rpi_i2s_source/config.yaml'):
        """Initialize config manager."""
        self.config_path = config_path
        self.config = self._load_or_create()
    
    def get(self, key_path: str = None) -> Any:
        """
        Get configuration value by dot-separated path.
        
        Args:
            key_path: Path like "i2s.gpio_bclk" or None for full config
        
        Returns:
            Configuration value or dict
        """
        if key_path is None:
            return self.config.copy()
        
        keys = key_path.split('.')
        value = self.config
        for key in keys:
            value = value[key]
        return value
    
    def set(self, key_path: str, value: Any) -> None:
        """
        Set configuration value by dot-separated path.
        
        Args:
            key_path: Path like "audio.tone_freq"
            value: New value
        """
        keys = key_path.split('.')
        config = self.config
        for key in keys[:-1]:
            config = config[key]
        config[keys[-1]] = value
    
    def save(self) -> None:
        """Save configuration to YAML file."""
        with open(self.config_path, 'w') as f:
            yaml.dump(self.config, f, default_flow_style=False)
    
    def reload(self) -> None:
        """Reload configuration from file."""
        self.config = self._load_or_create()
    
    # --- Internal Methods ---
    
    def _load_or_create(self) -> Dict[str, Any]:
        """Load config from file or create default if not exists."""
        if os.path.exists(self.config_path):
            with open(self.config_path, 'r') as f:
                config = yaml.safe_load(f)
            
            # Merge with defaults (fill missing keys)
            config = self._merge_with_defaults(config)
            
            # Validate
            self._validate(config)
            
            return config
        else:
            # Create default config
            os.makedirs(os.path.dirname(self.config_path), exist_ok=True)
            with open(self.config_path, 'w') as f:
                yaml.dump(self.DEFAULT_CONFIG, f, default_flow_style=False)
            return copy.deepcopy(self.DEFAULT_CONFIG)
    
    def _merge_with_defaults(self, config: Dict) -> Dict:
        """Merge user config with defaults (fill missing keys)."""
        def merge(user, default):
            for key, value in default.items():
                if key not in user:
                    user[key] = value
                elif isinstance(value, dict):
                    merge(user[key], value)
            return user
        return merge(config, copy.deepcopy(self.DEFAULT_CONFIG))
    
    def _validate(self, config: Dict) -> None:
        """
        Validate configuration values.
        
        Raises:
            ValueError: If invalid values detected
        """
        # GPIO pins in valid range (0-27 for RPi)
        for gpio_key in ['gpio_bclk', 'gpio_ws', 'gpio_dout']:
            gpio = config['i2s'][gpio_key]
            if not (0 <= gpio <= 27):
                raise ValueError(f"Invalid GPIO pin: {gpio_key}={gpio}")
        
        # Sample rate must be 48000
        if config['i2s']['sample_rate'] != 48000:
            raise ValueError("sample_rate must be 48000")
        
        # Buffer size > 1024
        if config['i2s']['buffer_size'] < 1024:
            raise ValueError("buffer_size must be >= 1024")
        
        # Tone frequency in range 20-20000 Hz
        if not (20 <= config['audio']['tone_freq'] <= 20000):
            logging.warning(f"Clamping tone_freq to 20-20000 Hz")
            config['audio']['tone_freq'] = np.clip(config['audio']['tone_freq'], 20, 20000)
        
        # Tone amplitude 0.0-1.0
        if not (0.0 <= config['audio']['tone_amp'] <= 1.0):
            logging.warning(f"Clamping tone_amp to 0.0-1.0")
            config['audio']['tone_amp'] = np.clip(config['audio']['tone_amp'], 0.0, 1.0)
```

---

### 2.7. Telemetry Tracker (`telemetry/tracker.py`)

**Purpose:** Collect metrics, system stats, provide aggregated status for web UI.

**Public API:**

```python
class TelemetryTracker:
    """System telemetry and statistics tracker."""
    
    def __init__(self):
        """Initialize telemetry tracker."""
        self._start_time = time.time()
        self._i2s_stats = {'frames_sent': 0, 'underruns': 0, 'buffer_fill_pct': 0.0}
        self._uart_stats = {'sent': 0, 'ok': 0, 'err': 0, 'connected': False}
        self._bt_stats = {'connected': False, 'device_mac': '', 'playing': False}
        self._audio_state = {'source': 'silence', 'tone_freq': 0, 'tone_amp': 0.0}
    
    def update_i2s(self, stats: Dict[str, Any]) -> None:
        """Update I2S statistics."""
        self._i2s_stats.update(stats)
    
    def update_uart(self, stats: Dict[str, Any]) -> None:
        """Update UART statistics."""
        self._uart_stats.update(stats)
    
    def update_bt(self, stats: Dict[str, Any]) -> None:
        """Update Bluetooth statistics."""
        self._bt_stats.update(stats)
    
    def update_audio(self, state: Dict[str, Any]) -> None:
        """Update audio state."""
        self._audio_state.update(state)
    
    def get_full_status(self) -> Dict[str, Any]:
        """
        Get complete system status for web UI.
        
        Returns:
            {
                "i2s": {...},
                "audio": {...},
                "uart": {...},
                "bt": {...},
                "system": {"uptime": int, "cpu_temp": float, "mem_usage_mb": float}
            }
        """
        return {
            'i2s': {
                'active': self._i2s_stats['frames_sent'] > 0,
                'sample_rate': 48000,
                'frames_sent': self._i2s_stats['frames_sent'],
                'underruns': self._i2s_stats['underruns'],
                'buffer_fill_pct': self._i2s_stats['buffer_fill_pct']
            },
            'audio': {
                'source': self._audio_state['source'],
                'tone_freq': self._audio_state['tone_freq'],
                'tone_amp': self._audio_state['tone_amp']
            },
            'uart': {
                'connected': self._uart_stats['connected'],
                'commands_sent': self._uart_stats['sent'],
                'commands_ok': self._uart_stats['ok'],
                'commands_err': self._uart_stats['err']
            },
            'bt': {
                'connected': self._bt_stats['connected'],
                'device_mac': self._bt_stats['device_mac'],
                'playing': self._bt_stats['playing']
            },
            'system': {
                'uptime': int(time.time() - self._start_time),
                'cpu_temp': self._get_cpu_temp(),
                'mem_usage_mb': self._get_memory_usage()
            }
        }
    
    def _get_cpu_temp(self) -> float:
        """Get Raspberry Pi CPU temperature in °C."""
        try:
            with open('/sys/class/thermal/thermal_zone0/temp', 'r') as f:
                temp = int(f.read().strip()) / 1000.0
            return temp
        except:
            return 0.0
    
    def _get_memory_usage(self) -> float:
        """Get Python process memory usage in MB."""
        import psutil
        process = psutil.Process(os.getpid())
        return process.memory_info().rss / 1024 / 1024
```

---

## 3. Sequence Diagrams

### 3.1. Application Startup

```
User          Main.py      ConfigMgr   AudioEngine   I2SDriver   UARTMgr   WebServer
 │               │             │            │            │          │          │
 │  python main.py             │            │            │          │          │
 │──────────────►│             │            │            │          │          │
 │               │  load()     │            │            │          │          │
 │               ├────────────►│            │            │          │          │
 │               │◄────────────┤ config.yaml             │          │          │
 │               │             │            │            │          │          │
 │               │  __init__(config)        │            │          │          │
 │               ├─────────────────────────►│            │          │          │
 │               │             │            │            │          │          │
 │               │  __init__(config, ring_buffer)        │          │          │
 │               ├───────────────────────────────────────►│          │          │
 │               │             │            │            │          │          │
 │               │  __init__(config)                     │          │          │
 │               ├───────────────────────────────────────────────────►│          │
 │               │             │            │            │          │          │
 │               │  start()    │            │            │          │          │
 │               ├─────────────────────────►│            │          │          │
 │               │             │  [Audio generation thread starts]  │          │
 │               │             │            │            │          │          │
 │               │  start()    │            │            │          │          │
 │               ├───────────────────────────────────────►│          │          │
 │               │             │            │ [I2S DMA thread starts]          │
 │               │             │            │            │          │          │
 │               │  start()    │            │            │          │          │
 │               ├───────────────────────────────────────────────────►│          │
 │               │             │            │            │ [UART RX thread]    │
 │               │             │            │            │          │          │
 │               │  __init__(config, audio, uart, telemetry)        │          │
 │               ├─────────────────────────────────────────────────────────────►│
 │               │             │            │            │          │          │
 │               │  start()    │            │            │          │          │
 │               ├─────────────────────────────────────────────────────────────►│
 │               │             │            │            │          │ [Flask server running]
 │               │             │            │            │          │          │
 │  http://rpi:5000/          │            │            │          │          │
 │◄─────────────────────────────────────────────────────────────────────────────┤
 │  [Web UI dashboard]        │            │            │          │          │
```

**Timeline:** ~2-3 seconds from `python main.py` to web UI accessible.

---

### 3.2. Tone Generation and I2S Transmission

```
WebUI      Flask      AudioEngine   RingBuffer   I2SDriver   esp_bt_audio_source
 │            │            │            │            │              │
 │ POST /api/tone          │            │            │              │
 │  {"freq":1000,"amp":0.75}            │            │              │
 │───────────►│            │            │            │              │
 │            │ set_tone_params()       │            │              │
 │            ├───────────►│            │            │              │
 │            │◄───────────┤ [phase accumulator updated]            │
 │            │            │            │            │              │
 │◄───────────┤ 200 OK     │            │            │              │
 │            │            │            │            │              │
 │            │ [Audio generation loop] │            │              │
 │            │            │ _generate_tone()        │              │
 │            │            ├──────────► │            │              │
 │            │            │  [NumPy sin()] │        │              │
 │            │            │            │            │              │
 │            │            │ write(samples)          │              │
 │            │            ├───────────►│            │              │
 │            │            │            │ [stereo LRLRLR...]        │
 │            │            │            │            │              │
 │            │ [I2S DMA loop]          │            │              │
 │            │            │            │ read(2)    │              │
 │            │            │            │◄───────────┤              │
 │            │            │            │────────────►│ [L, R samples]
 │            │            │            │            │              │
 │            │            │            │ _transmit_frame()         │
 │            │            │            │            ├─────────────►│ BCLK/WS/DOUT
 │            │            │            │            │              │ (I2S GPIO)
 │            │            │            │            │              │
 │            │ [Repeat at 48 kHz frame rate]        │              │
```

**Latency:** Tone parameter change → I2S output: <150 ms (85 ms buffer + 50 ms processing + 15 ms Flask overhead).

---

### 3.3. UART Command: Bluetooth Connect

```
WebUI      Flask      UARTMgr       UART       esp_bt_audio_source
 │            │          │           │                │
 │ POST /api/bt/command │           │                │
 │  {"command":"CONNECT","args":"AA:BB:CC:DD:EE:FF"} │
 │───────────►│          │           │                │
 │            │ send_command("CONNECT", "AA:BB...")  │
 │            ├─────────►│           │                │
 │            │          │ "CONNECT AA:BB...\n"       │
 │            │          ├──────────►│───────────────►│
 │            │          │           │                │ [Bluetooth connect attempt]
 │            │          │           │                │
 │            │          │ [RX thread blocked waiting response]
 │            │          │           │◄───────────────┤
 │            │          │◄──────────┤ "OK|CONNECT|AA:BB...\n"
 │            │          │ [future.set_result()]      │
 │            │◄─────────┤ {"status":"ok","result":"OK|CONNECT|..."}
 │            │          │           │                │
 │◄───────────┤ 200 OK   │           │                │
 │ {"status":"ok",...}   │           │                │
 │            │          │           │                │
 │            │ [Later: async event] │                │
 │            │          │           │◄───────────────┤
 │            │          │◄──────────┤ "EVENT|BT|CONNECTED|AA:BB...\n"
 │            │          │ _handle_event()            │
 │            │          │ [callback(event)]          │
 │            │          │ [update telemetry]         │
 │            │          │           │                │
 │ [SSE stream updates BT status]   │                │
 │◄──────────────────────────────────────────────────────────
 │ data: {"bt":{"connected":true,"device_mac":"AA:BB:..."}}
```

**Timeout:** 5 seconds for command response.

---

### 3.4. WAV File Playback

```
User       WebUI      Flask      AudioEngine   RingBuffer   I2SDriver
 │            │          │            │            │            │
 │ [Upload test.wav to /home/pi/audio/]            │            │
 │            │          │            │            │            │
 │ Select WAV file      │            │            │            │
 │ Click "Play"         │            │            │            │
 │───────────►│          │            │            │            │
 │            │ POST /api/wav {"file":"test.wav"}  │            │
 │            ├─────────►│            │            │            │
 │            │          │ set_source('wav', {'file':'test.wav'})
 │            │          ├───────────►│            │            │
 │            │          │            │ _load_wav('test.wav')   │
 │            │          │            ├──────────► │            │
 │            │          │            │ [scipy.io.wavfile.read()]
 │            │          │            │ [resample if needed]    │
 │            │          │            │ [convert to 16-bit stereo]
 │            │          │            │ [store in _wav_buffer]  │
 │            │          │            │            │            │
 │            │◄─────────┤ 200 OK     │            │            │
 │◄───────────┤          │            │            │            │
 │            │          │            │            │            │
 │            │          │ [Audio generation loop] │            │
 │            │          │            │ _generate_wav()         │
 │            │          │            ├──────────► │            │
 │            │          │            │ [read chunk from _wav_buffer]
 │            │          │            │            │            │
 │            │          │            │ write(samples)          │
 │            │          │            ├───────────►│            │
 │            │          │            │            │            │
 │            │          │            │ [I2S DMA transmits WAV] │
 │            │          │            │            ├───────────►│ I2S GPIO
 │            │          │            │            │            │
 │            │ [WAV plays until EOF or stopped]   │            │
```

**WAV Resample:** If 44.1 kHz → 48 kHz conversion needed, uses `scipy.signal.resample`.

---

## 4. State Machines

### 4.1. Audio Source State Machine

```
         ┌─────────────────────────────────────────┐
         │                                         │
    ┌────▼────┐                              ┌─────┴─────┐
    │ SILENCE │                              │   TONE    │
    │ (zeros) │◄─────────────────────────────┤ (sine gen)│
    └────┬────┘  stop() or set_source()      └─────┬─────┘
         │                                         │
         │  set_source('tone')                     │ set_source('sweep')
         │                                         │
         │                              ┌──────────▼────────┐
         │                              │      SWEEP        │
         │◄─────────────────────────────┤  (chirp 20-20k)   │
         │  stop() or set_source()      └──────────┬────────┘
         │                                         │
         │  set_source('wav')                      │ set_source('wav')
         │                                         │
    ┌────▼────┐                              ┌─────┴─────┐
    │   WAV   │◄─────────────────────────────┤    WAV    │
    │(playback)                              │ (playback)│
    └────┬────┘  EOF or stop()               └───────────┘
         │
         │  [EOF reached]
         │
         └──────► SILENCE
```

**Transitions:**
- Any state → SILENCE: `stop()` or `set_source('silence')`
- SILENCE → TONE: `set_source('tone', params)`
- TONE → SWEEP: `set_source('sweep', params)`
- Any state → WAV: `set_source('wav', {'file': 'test.wav'})`
- WAV → SILENCE: End-of-file reached or `stop()`

**Concurrency:** State changes are thread-safe (protected by mutex in AudioEngine).

---

### 4.2. UART Connection State Machine

```
    ┌──────────────┐
    │ DISCONNECTED │
    └──────┬───────┘
           │
           │ start()
           │ [open serial port]
           │
    ┌──────▼───────┐
    │  CONNECTING  │
    │ [send STATUS]│
    └──────┬───────┘
           │
           │ timeout (5s)
           │
    ┌──────▼───────┐         ┌────────────┐
    │    ERROR     │◄────────┤  CONNECTED │
    │ [retry 3x]   │  serial │ [RX thread │
    └──────┬───────┘  error  │  running]  │
           │                  └─────┬──────┘
           │ [max retries]          │
           │                        │ send_command()
           │                        │ [wait response]
           │                        │
           └────────────────────────┘
                [reconnect every 5s]
```

**Error Recovery:**
- Serial exception → close port, wait 5 seconds, retry (max 10 attempts)
- Command timeout → log warning, return error to caller, keep connection open
- Parse error → log malformed response, continue operation

---

### 4.3. Web Server Request Handling

```
    Browser              Flask                Component
       │                   │                     │
       │  GET /api/status  │                     │
       ├──────────────────►│                     │
       │                   │ get_full_status()   │
       │                   ├────────────────────►│
       │                   │◄────────────────────┤ JSON status
       │◄──────────────────┤ 200 OK              │
       │  {i2s:{...}, ...} │                     │
       │                   │                     │
       │  POST /api/tone   │                     │
       │  {"freq":1000}    │                     │
       ├──────────────────►│                     │
       │                   │ set_tone_params()   │
       │                   ├────────────────────►│
       │                   │ [validate freq]     │
       │                   │ [update phase acc]  │
       │                   │◄────────────────────┤
       │◄──────────────────┤ 200 OK              │
       │  {"status":"ok"}  │                     │
       │                   │                     │
       │  GET /api/stream  │                     │
       │  [SSE connect]    │                     │
       ├──────────────────►│                     │
       │◄──────────────────┤ event: status       │
       │  data: {...}      │ [every 500ms]       │
       │◄──────────────────┤ event: status       │
       │  data: {...}      │                     │
       │                   │                     │
```

**SSE Stream:** Publishes status updates at 2 Hz (500 ms interval) using Flask-SSE extension.

---

## 5. API Specifications

### 5.1. Main Application (`main.py`)

**Entry Point:**

```python
#!/usr/bin/env python3
"""
Raspberry Pi I2S Audio Source - Main Application
"""

import signal
import sys
import logging
from config.manager import ConfigManager
from audio.engine import AudioEngine
from audio.i2s_driver import I2SDriver
from audio.ring_buffer import RingBuffer
from uart.command_manager import UARTCommandManager
from telemetry.tracker import TelemetryTracker
from web.app import WebServer

def main():
    """Main application entry point."""
    # Setup logging
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s [%(levelname)s] %(name)s: %(message)s'
    )
    
    # Load configuration
    config_mgr = ConfigManager('/home/pi/rpi_i2s_source/config.yaml')
    config = config_mgr.get()
    
    # Initialize components
    ring_buffer = RingBuffer(capacity=config['i2s']['buffer_size'])
    audio_engine = AudioEngine(config, ring_buffer)
    i2s_driver = I2SDriver(config, ring_buffer)
    uart_mgr = UARTCommandManager(config)
    telemetry = TelemetryTracker()
    web_server = WebServer(config, audio_engine, uart_mgr, telemetry)
    
    # Start background threads
    audio_engine.start()
    i2s_driver.start()
    uart_mgr.start()
    
    # Register UART event callback to update telemetry
    def on_bt_event(event):
        if event['type'] == 'BT' and event['subtype'] == 'CONNECTED':
            telemetry.update_bt({'connected': True, 'device_mac': event['data']})
        elif event['type'] == 'BT' and event['subtype'] == 'DISCONNECTED':
            telemetry.update_bt({'connected': False, 'device_mac': ''})
    uart_mgr.register_event_callback(on_bt_event)
    
    # Setup signal handlers for graceful shutdown
    def signal_handler(sig, frame):
        logging.info("Shutting down...")
        audio_engine.stop()
        i2s_driver.stop()
        uart_mgr.stop()
        web_server.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Start web server (blocking)
    logging.info("Starting web server on http://0.0.0.0:5000")
    web_server.start()

if __name__ == '__main__':
    main()
```

---

### 5.2. Error Codes

**Custom Exception Classes:**

```python
class I2SError(Exception):
    """Base exception for I2S driver errors."""
    pass

class I2SUnderrunError(I2SError):
    """Buffer underrun occurred."""
    pass

class I2SHardwareError(I2SError):
    """I2S hardware initialization failed."""
    pass

class UARTError(Exception):
    """Base exception for UART errors."""
    pass

class UARTTimeoutError(UARTError):
    """Command timeout (no response within timeout period)."""
    pass

class UARTDisconnectedError(UARTError):
    """Serial port disconnected or unreachable."""
    pass

class AudioError(Exception):
    """Base exception for audio generation errors."""
    pass

class WAVNotFoundError(AudioError):
    """WAV file not found in /home/pi/audio/."""
    pass

class WAVFormatError(AudioError):
    """WAV file format unsupported (not 16-bit PCM)."""
    pass
```

---

## 6. Memory and Resource Management

### 6.1. Memory Footprint Estimate

**Python Process (Typical):**

| Component | Heap Usage | Notes |
|-----------|------------|-------|
| NumPy arrays (audio buffers) | ~30 MB | 8192-sample buffer × multiple (tone, WAV, ring) |
| Flask application | ~20 MB | Flask framework, templates, routes |
| pyserial + threading | ~5 MB | UART RX/TX threads, line buffers |
| pigpio client | ~10 MB | pigpio library, waveform buffers |
| SciPy (WAV resample) | ~15 MB | Loaded on-demand for WAV playback |
| Logging, config, misc | ~5 MB | YAML, log handlers, telemetry |
| **Total** | **~85 MB** | Within NFR5 target (<100 MB) |

**Raspberry Pi Resources:**

| Model | RAM Available | rpi_i2s_source Usage | Headroom |
|-------|---------------|----------------------|----------|
| RPi 4 (4 GB) | ~3.5 GB free (after OS) | ~85 MB | Excellent (97% free) |
| RPi 3 B+ (1 GB) | ~700 MB free | ~85 MB | Good (88% free) |
| RPi Zero 2 W (512 MB) | ~300 MB free | ~85 MB | Acceptable (72% free) |

**Conclusion:** Memory usage well within limits for all target platforms (NFR5 met).

---

### 6.2. CPU Usage Estimate

**Per-Component CPU Load (@ 48 kHz stereo):**

| Component | Idle CPU | Active CPU | Notes |
|-----------|----------|------------|-------|
| Audio Engine (tone gen) | 0% | ~8% | NumPy `sin()` optimized (vectorized) |
| I2S Driver (ALSA) | 0% | ~5% | Kernel driver handles DMA |
| Ring Buffer (locks) | <1% | <1% | Minimal mutex overhead |
| UART Manager (RX thread) | <1% | ~2% | Serial I/O, line parsing |
| Flask Web Server | ~3% | ~5% | SSE stream, HTTP requests |
| **Total (idle)** | **~4%** | | All threads sleeping |
| **Total (active tone)** | | **~20%** | Tone generation + I2S + web UI |

**Peak Load Scenarios:**

- WAV file playback (44.1 kHz → 48 kHz resample): +5% CPU → **~25%**
- Frequency sweep (continuous chirp): +3% CPU → **~23%**

**Conclusion:** CPU usage well within NFR5 target (<25% during tone generation, <10% idle).

---

### 6.3. Thread Architecture

**Thread Inventory:**

| Thread | Purpose | Priority | Stack Size | Sleep/Block |
|--------|---------|----------|------------|-------------|
| **Main** | Flask web server (blocking) | Normal | Default | HTTP I/O |
| **Audio Generation** | Generate PCM samples → ring buffer | Normal | Default | 1 ms sleep if buffer full |
| **I2S DMA** | Read ring buffer → I2S GPIO | High | Default | Block on ALSA write |
| **UART RX** | Read serial port, parse responses | Normal | Default | Block on serial read |
| **Flask SSE** | Server-Sent Events stream | Low | Default | 500 ms sleep between updates |

**Synchronization:**
- Ring buffer: `threading.Lock` on read/write
- Audio engine: `threading.Lock` on parameter updates
- UART manager: `queue.Queue` for command queueing, `concurrent.futures.Future` for responses

**No Real-Time Constraints:** Python GIL acceptable; no hard real-time deadlines (audio buffering provides tolerance).

---

## 7. Error Handling and Recovery

### 7.1. I2S Error Handling

**Underrun (Buffer Empty):**

```python
def _dma_loop(self):
    while not self._stop_event.is_set():
        samples = self.ring_buffer.read(1024)
        if samples is None:
            # Underrun: zero-fill
            logging.warning("I2S underrun detected")
            samples = np.zeros(1024, dtype=np.int16)
            self._underruns += 1
        
        self.device.write(samples.tobytes())
```

**Hardware Failure (pigpiod unavailable):**

```python
def __init__(self, config, ring_buffer):
    self.pi = pigpio.pi()
    if not self.pi.connected:
        logging.critical("Cannot connect to pigpiod daemon")
        raise I2SHardwareError("pigpiod not running. Run: sudo systemctl start pigpiod")
```

**Recovery:** Display error in web UI, suggest restarting `pigpiod` or checking GPIO permissions.

---

### 7.2. UART Error Handling

**Timeout (No Response):**

```python
try:
    response = uart_mgr.send_command("VOLUME", "75")
except TimeoutError as e:
    logging.error(f"UART timeout: {e}")
    return flask.jsonify({"status": "error", "message": "Command timeout. Check UART connection."})
```

**Serial Disconnect:**

```python
def _rx_loop(self):
    while not self._stop_event.is_set():
        try:
            byte = self._serial.read(1)
            # ... process byte
        except serial.SerialException as e:
            logging.error(f"UART read error: {e}")
            self._reconnect()
            time.sleep(5.0)  # Retry delay

def _reconnect(self):
    """Attempt to reconnect to serial port."""
    for attempt in range(10):
        try:
            self._serial.close()
            self._serial.open()
            logging.info("UART reconnected")
            return
        except serial.SerialException:
            time.sleep(5.0)
    logging.critical("UART reconnect failed after 10 attempts")
```

---

### 7.3. Audio Generation Errors

**WAV File Not Found:**

```python
def _load_wav(self, filename):
    filepath = os.path.join('/home/pi/audio', filename)
    if not os.path.exists(filepath):
        raise WAVNotFoundError(f"WAV file not found: {filename}")
    # ... load WAV
```

**Web UI Response:**

```python
@app.route('/api/wav', methods=['POST'])
def play_wav():
    try:
        data = request.json
        audio_engine.set_source('wav', {'file': data['file']})
        return jsonify({"status": "ok"})
    except WAVNotFoundError as e:
        return jsonify({"status": "error", "message": str(e)}), 404
    except WAVFormatError as e:
        return jsonify({"status": "error", "message": str(e)}), 400
```

---

## 8. Configuration Management

### 8.1. Configuration File Schema (YAML)

**Full Schema with Comments:**

```yaml
# I2S hardware configuration
i2s:
  gpio_bclk: 18       # BCM GPIO pin for bit clock (output)
  gpio_ws: 19         # BCM GPIO pin for word select (output)
  gpio_dout: 21       # BCM GPIO pin for data out (output)
  sample_rate: 48000  # Must be 48000 (fixed)
  buffer_size: 8192   # Ring buffer size in samples (2048-16384)

# UART configuration
uart:
  device: /dev/serial0  # Serial device path
  baudrate: 115200      # Baud rate (must match esp_bt_audio_source)
  timeout: 5.0          # Command response timeout in seconds

# Audio generation defaults
audio:
  default_source: tone  # Initial source: tone, sweep, wav, silence
  tone_freq: 1000       # Default tone frequency (Hz)
  tone_amp: 0.75        # Default tone amplitude (0.0-1.0)
  wav_directory: /home/pi/audio  # WAV file search path

# Web server configuration
web:
  port: 5000            # HTTP server port
  bind_address: 0.0.0.0 # Bind address (0.0.0.0 = all interfaces)
  log_level: INFO       # Logging level: DEBUG, INFO, WARNING, ERROR

# Bluetooth state (auto-updated)
bluetooth:
  last_device_mac: ""   # Last connected device MAC (saved on clean shutdown)
```

---

### 8.2. Runtime Configuration Updates

**Update via Web UI:**

```python
@app.route('/api/config', methods=['POST'])
def update_config():
    """
    Update configuration at runtime (non-persistent unless saved).
    
    Request Body:
        {"key": "audio.tone_freq", "value": 440}
    """
    data = request.json
    key = data['key']
    value = data['value']
    
    # Update in-memory config
    config_mgr.set(key, value)
    
    # Apply to running components
    if key == 'audio.tone_freq':
        audio_engine.set_tone_params(freq=value, amp=None, mode=None)
    elif key == 'web.log_level':
        logging.getLogger().setLevel(value)
    
    return jsonify({"status": "ok", "message": f"Updated {key} = {value}"})
```

**Persist to Disk:**

```python
@app.route('/api/config/save', methods=['POST'])
def save_config():
    """Save current configuration to YAML file."""
    config_mgr.save()
    return jsonify({"status": "ok", "message": "Configuration saved to disk"})
```

---

## 9. Implementation Guidelines

### 9.1. Directory Structure

```
rpi_i2s_source/
├── main.py                     # Application entry point
├── requirements.txt            # Python dependencies
├── config.yaml                 # User configuration (gitignored)
├── README.md                   # Setup and usage instructions
├── audio/
│   ├── __init__.py
│   ├── engine.py               # AudioEngine class
│   ├── i2s_driver.py           # I2SDriver (ALSA or pigpio)
│   └── ring_buffer.py          # RingBuffer class
├── uart/
│   ├── __init__.py
│   └── command_manager.py      # UARTCommandManager class
├── config/
│   ├── __init__.py
│   └── manager.py              # ConfigManager class
├── telemetry/
│   ├── __init__.py
│   └── tracker.py              # TelemetryTracker class
├── web/
│   ├── __init__.py
│   ├── app.py                  # Flask application
│   ├── static/
│   │   ├── css/
│   │   │   └── style.css
│   │   └── js/
│   │       └── dashboard.js    # JavaScript for web UI
│   └── templates/
│       ├── index.html          # Main dashboard
│       ├── bluetooth.html      # Bluetooth control panel
│       └── logs.html           # Log viewer
├── tests/
│   ├── test_audio_engine.py   # Pytest unit tests
│   ├── test_ring_buffer.py
│   ├── test_uart_manager.py
│   └── integration/
│       └── test_i2s_pipeline.py
└── docs/
    ├── PRD.md                  # Product Requirements Document
    ├── FS.md                   # This Functional Specification
    └── SETUP.md                # Raspberry Pi setup guide
```

---

### 9.2. Coding Standards

**Python Style:**
- Follow **PEP 8** (use `black` formatter, line length 100)
- Type hints for public methods: `def set_tone_params(self, freq: int, amp: float) -> None:`
- Docstrings: NumPy style for classes/methods
- Avoid global variables (use class attributes or config)

**Error Handling:**
- Use custom exception classes (I2SError, UARTError, AudioError)
- Log all exceptions with context: `logging.error(f"UART error: {e}", exc_info=True)`
- Return meaningful error messages to web UI (JSON with "message" field)

**Logging:**
- Module-level loggers: `logger = logging.getLogger(__name__)`
- Levels: DEBUG (development), INFO (production), WARNING (recoverable errors), ERROR (serious issues)
- No `print()` statements (use logging only)

**Concurrency:**
- Use `threading.Lock` for shared data structures (ring buffer, telemetry)
- Prefer `queue.Queue` for producer-consumer patterns (UART command queue)
- Avoid busy-wait loops (use `time.sleep()` or blocking I/O)

---

### 9.3. Testing Requirements

**Unit Tests (Pytest):**

- Coverage target: >80% for core modules (audio, uart, ring_buffer)
- Mock external dependencies: serial port, pigpio, GPIO
- Test edge cases: buffer overflow, underrun, invalid WAV format, UART timeout

**Integration Tests:**

- End-to-end I2S pipeline: tone generation → ring buffer → I2S driver (loopback test with USB audio capture)
- UART round-trip: send command → parse response (requires esp_bt_audio_source connected)
- Web UI API: POST requests → verify component state changes

**Performance Tests:**

- CPU usage: measure with `psutil.cpu_percent()` during 1 kHz tone generation (target <25%)
- Memory usage: measure with `psutil.Process().memory_info().rss` (target <100 MB)
- I2S timing: logic analyzer verification of BCLK frequency (1.536 MHz ±50 ppm)

---

## 10. Testing Specifications

### 10.1. Unit Test Examples

**Test: Tone Generation Frequency Accuracy**

```python
# tests/test_audio_engine.py
import pytest
import numpy as np
from audio.engine import AudioEngine
from audio.ring_buffer import RingBuffer

def test_tone_frequency_accuracy():
    """Verify 1 kHz tone has correct frequency spectrum."""
    config = {'i2s': {'buffer_size': 8192, 'sample_rate': 48000}}
    ring_buffer = RingBuffer(8192)
    engine = AudioEngine(config, ring_buffer)
    
    # Set 1 kHz tone
    engine.set_tone_params(freq=1000, amp=0.75, mode='mono')
    
    # Generate one buffer
    samples = engine._generate_tone()
    
    # FFT analysis
    fft = np.fft.rfft(samples[::2])  # Left channel only (de-interleave)
    freqs = np.fft.rfftfreq(len(samples) // 2, 1 / 48000)
    peak_freq = freqs[np.argmax(np.abs(fft))]
    
    # Assert peak within ±5 Hz tolerance
    assert 995 < peak_freq < 1005, f"Peak at {peak_freq} Hz, expected 1000 Hz"
    
    # Assert amplitude
    peak_amp = np.max(np.abs(samples))
    expected_amp = 0.75 * 32767
    assert expected_amp * 0.95 < peak_amp < expected_amp * 1.05
```

**Test: Ring Buffer Thread Safety**

```python
# tests/test_ring_buffer.py
import pytest
import numpy as np
import threading
from audio.ring_buffer import RingBuffer

def test_ring_buffer_concurrent_access():
    """Test concurrent writes and reads don't corrupt buffer."""
    rb = RingBuffer(capacity=1024)
    errors = []
    
    def writer():
        for i in range(100):
            samples = np.full(10, i, dtype=np.int16)
            try:
                rb.write(samples)
            except Exception as e:
                errors.append(e)
    
    def reader():
        for i in range(100):
            try:
                samples = rb.read(10)
                if samples is not None and len(samples) != 10:
                    errors.append(f"Read wrong length: {len(samples)}")
            except Exception as e:
                errors.append(e)
    
    # Start 2 writers and 2 readers
    threads = []
    for _ in range(2):
        threads.append(threading.Thread(target=writer))
        threads.append(threading.Thread(target=reader))
    
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    
    assert len(errors) == 0, f"Concurrent access errors: {errors}"
```

**Test: UART Command Parsing**

```python
# tests/test_uart_manager.py
import pytest
from unittest.mock import Mock, patch
from uart.command_manager import UARTCommandManager

def test_parse_ok_response():
    """Test parsing OK|COMMAND|DATA response."""
    config = {'uart': {'device': '/dev/null', 'baudrate': 115200, 'timeout': 5.0}}
    
    with patch('serial.Serial'):
        mgr = UARTCommandManager(config)
        
        # Simulate received line
        line = "OK|SCAN|3"
        mgr._process_line(line)
        
        # Verify response matched (would set Future in real implementation)
        # (Simplified for unit test; real test uses mock Future)

def test_parse_event():
    """Test parsing EVENT|TYPE|SUBTYPE|DATA."""
    config = {'uart': {'device': '/dev/null', 'baudrate': 115200, 'timeout': 5.0}}
    
    with patch('serial.Serial'):
        mgr = UARTCommandManager(config)
        
        # Register callback
        events = []
        mgr.register_event_callback(lambda e: events.append(e))
        
        # Simulate event
        line = "EVENT|BT|CONNECTED|AA:BB:CC:DD:EE:FF"
        mgr._process_line(line)
        
        # Verify callback invoked
        assert len(events) == 1
        assert events[0]['type'] == 'BT'
        assert events[0]['subtype'] == 'CONNECTED'
        assert events[0]['data'] == 'AA:BB:CC:DD:EE:FF'
```

---

### 10.2. Integration Test Examples

**Test: I2S to esp_bt_audio_source Pipeline**

```python
# tests/integration/test_i2s_pipeline.py
import pytest
import time
from main import main  # Application main function

def test_e2e_tone_to_bluetooth(esp_bt_audio_source_fixture):
    """
    End-to-end test: Generate 1 kHz tone, verify Bluetooth speaker plays.
    
    Prerequisites:
    - esp_bt_audio_source connected via I2S and UART
    - Bluetooth speaker paired and powered on
    - Logic analyzer connected to I2S GPIO (optional, for verification)
    """
    # Start application
    app_thread = threading.Thread(target=main)
    app_thread.start()
    
    time.sleep(2)  # Wait for initialization
    
    # Send tone start command via HTTP
    response = requests.post('http://localhost:5000/api/tone', json={
        'freq': 1000,
        'amp': 0.75,
        'mode': 'mono'
    })
    assert response.status_code == 200
    
    # Send Bluetooth START command
    response = requests.post('http://localhost:5000/api/bt/command', json={
        'command': 'START'
    })
    assert response.status_code == 200
    assert response.json()['status'] == 'ok'
    
    # Wait and verify status
    time.sleep(5)
    response = requests.get('http://localhost:5000/api/status')
    status = response.json()
    
    assert status['i2s']['active'] == True
    assert status['bt']['playing'] == True
    assert status['i2s']['underruns'] == 0  # No underruns
    
    # Manual verification: Listen to Bluetooth speaker (should hear 1 kHz tone)
    # Automated verification: Use logic analyzer to confirm I2S BCLK = 1.536 MHz
```

---

### 10.3. Performance Validation Scripts

**Script: CPU and Memory Monitoring**

```python
# tests/performance/monitor_resources.py
import psutil
import time
import requests

def monitor_resources(duration=60):
    """
    Monitor CPU and memory usage during tone generation.
    
    Args:
        duration: Monitoring duration in seconds
    """
    # Start tone generation
    requests.post('http://localhost:5000/api/tone', json={
        'freq': 1000,
        'amp': 0.75
    })
    
    cpu_samples = []
    mem_samples = []
    
    for i in range(duration):
        cpu = psutil.cpu_percent(interval=1.0)
        mem = psutil.Process().memory_info().rss / 1024 / 1024  # MB
        
        cpu_samples.append(cpu)
        mem_samples.append(mem)
        
        print(f"[{i+1:3d}s] CPU: {cpu:5.1f}%  Memory: {mem:6.1f} MB")
    
    # Statistics
    print(f"\nCPU: avg={sum(cpu_samples)/len(cpu_samples):.1f}%  "
          f"max={max(cpu_samples):.1f}%")
    print(f"Memory: avg={sum(mem_samples)/len(mem_samples):.1f} MB  "
          f"max={max(mem_samples):.1f} MB")
    
    # Assert NFR5
    assert max(cpu_samples) < 25.0, "CPU usage exceeded 25%"
    assert max(mem_samples) < 100.0, "Memory usage exceeded 100 MB"

if __name__ == '__main__':
    monitor_resources(duration=300)  # 5-minute test
```

---

## Appendices

### Appendix A: References

- **Raspberry Pi I2S Documentation:** https://www.raspberrypi.org/documentation/computers/raspberry-pi.html#i2s
- **pigpio Library:** http://abyz.me.uk/rpi/pigpio/
- **ALSA (Advanced Linux Sound Architecture):** https://www.alsa-project.org/
- **Flask Documentation:** https://flask.palletsprojects.com/
- **Flask-SSE:** https://github.com/singingwolfboy/flask-sse
- **pyserial Documentation:** https://pyserial.readthedocs.io/
- **NumPy Documentation:** https://numpy.org/doc/
- **SciPy Signal Processing:** https://docs.scipy.org/doc/scipy/reference/signal.html
- **esp_bt_audio_source Protocol:** `/esp_bt_audio_source/docs/FS.md`

### Appendix B: Acronyms

| Acronym | Definition |
|---------|------------|
| API | Application Programming Interface |
| BCLK | Bit Clock (I2S clock signal) |
| DMA | Direct Memory Access |
| FFT | Fast Fourier Transform |
| GPIO | General Purpose Input/Output |
| I2S | Inter-IC Sound (audio bus protocol) |
| PCM | Pulse Code Modulation |
| SSE | Server-Sent Events |
| THD+N | Total Harmonic Distortion + Noise |
| UART | Universal Asynchronous Receiver-Transmitter |
| WAV | Waveform Audio File Format |
| WS | Word Select (I2S left/right channel select) |
| YAML | YAML Ain't Markup Language (config file format) |

---

**END OF DOCUMENT**
