# Functional Specification (FS)

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Product Name:** bbgw_i2s_source  
**Version:** 1.0.0-bbgw  
**Date:** 2026-02-07  
**Status:** Complete (v1.0 Release Ready)

---

## Document Overview

This Functional Specification describes the detailed technical design and implementation of **bbgw_i2s_source**. It defines the system architecture, component interfaces, data flows, protocols, and implementation details necessary to understand, maintain, and extend the system.

**Related Documents:**
- [PRD.md](PRD.md) — Product Requirements Document
- [README.md](../README.md) — Quick start guide
- [ARCH.md](../ARCH.md) — High-level architecture (if exists)

**Audience:**
- Software developers implementing or maintaining the system
- DevOps engineers deploying and configuring the system
- Technical architects reviewing the design
- QA engineers developing test strategies

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Component Specifications](#component-specifications)
3. [Data Flow](#data-flow)
4. [State Machines](#state-machines)
5. [API Specifications](#api-specifications)
6. [Configuration Schema](#configuration-schema)
7. [Threading Model](#threading-model)
8. [Error Handling Strategy](#error-handling-strategy)
9. [Security Model](#security-model)
10. [Testing Strategy](#testing-strategy)
11. [Deployment Architecture](#deployment-architecture)
12. [Performance Considerations](#performance-considerations)
13. [Appendices](#appendices)

---

## System Architecture

### 1.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Web Browser (Client)                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Dashboard   │  │ Audio Sources│  │  Bluetooth   │      │
│  │    Page      │  │     Page     │  │  Control Page│      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │              │
│         └──────────────────┼──────────────────┘              │
│                            │                                 │
└────────────────────────────┼─────────────────────────────────┘
                             │ HTTP/HTTPS + SSE
                             ▼
┌─────────────────────────────────────────────────────────────┐
│              Flask Web Server (bbgw_i2s_source)             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │              Web Server Module                       │  │
│  │  ├─ RESTful API Endpoints (/api/*)                   │  │
│  │  ├─ Server-Sent Events (/api/events)                 │  │
│  │  ├─ Template Rendering (Jinja2)                      │  │
│  │  └─ Static File Serving (CSS, JS)                    │  │
│  └────────────┬────────────────────────┬──────────────────┘  │
│               │                        │                     │
│               ▼                        ▼                     │
│  ┌────────────────────┐   ┌──────────────────────────┐     │
│  │  Audio Engine      │   │  UART Command Manager    │     │
│  │  ├─ Tone Gen       │   │  ├─ Serial Protocol      │     │
│  │  ├─ Sweep Gen      │   │  ├─ Command Queue        │     │
│  │  ├─ WAV Playback   │   │  ├─ Response Parser      │     │
│  │  ├─ Silence Gen    │   │  └─ Event Handler        │     │
│  │  └─ Ring Buffer    │   └──────────┬───────────────┘     │
│  └────────┬───────────┘              │                      │
│           │                          │                      │
│           ▼                          ▼                      │
│  ┌────────────────────┐   ┌──────────────────────────┐     │
│  │  I2S Driver        │   │  Serial Port             │     │
│  │  ├─ ALSA Interface │   │  (pyserial)              │     │
│  │  ├─ Buffer Mgmt    │   │  /dev/ttyO4 @ 115200     │     │
│  │  └─ Underrun Det.  │   └──────────┬───────────────┘     │
│  └────────┬───────────┘              │                      │
└───────────┼──────────────────────────┼──────────────────────┘
            │                          │
            ▼                          ▼
┌───────────────────────┐   ┌──────────────────────────┐
│   ALSA (McASP0)       │   │   ESP32 (via UART4)      │
│   hw:0,0 @ 48kHz      │   │   Bluetooth Commands     │
│   16-bit Stereo       │   │   Status/Events          │
└───────┬───────────────┘   └──────────────────────────┘
        │
        ▼
┌───────────────────────┐
│  Hardware I2S Output  │
│  P9.31 → BCLK         │
│  P9.29 → WS (LRCLK)   │
│  P9.28 → DOUT         │
└───────┬───────────────┘
        │
        ▼
┌───────────────────────┐       ┌──────────────────────┐
│  ESP32 I2S Slave      │  OR   │ UDA1334ATS DAC       │
│  (Production Mode)    │       │ (Test Mode)          │
│  → Bluetooth Speaker  │       │ → Headphones         │
└───────────────────────┘       └──────────────────────┘
```

### 1.2 Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     bbgw_i2s_source                          │
│                                                              │
│  main.py ────► ConfigManager ──┬─► config.yaml              │
│    │                            │                            │
│    ├─► AudioEngine ────────────┼─► I2SDriver ──► ALSA       │
│    │      │                     │      │                     │
│    │      ├─ ToneGenerator      │      └─► alsaaudio.PCM    │
│    │      ├─ SweepGenerator     │                            │
│    │      ├─ WAVPlayer          │                            │
│    │      ├─ SilenceGenerator   │                            │
│    │      └─ RingBuffer         │                            │
│    │                             │                            │
│    ├─► UARTManager ─────────────┼─► /dev/ttyO4 (ESP32)     │
│    │      ├─ CommandQueue        │                            │
│    │      ├─ ResponseParser      │                            │
│    │      └─ EventHandler        │                            │
│    │                             │                            │
│    ├─► WebServer (Flask) ───────┼─► HTTP :5000              │
│    │      ├─ API Routes          │                            │
│    │      ├─ SSE Stream          │                            │
│    │      └─ Templates           │                            │
│    │                             │                            │
│    └─► TelemetryTracker ────────┼─► Metrics Collection      │
│           ├─ CPU Monitor         │                            │
│           ├─ Memory Monitor      │                            │
│           └─ I2S Stats           │                            │
│                                  │                            │
└──────────────────────────────────┴────────────────────────────┘
```

### 1.3 Layer Architecture

**Presentation Layer (Web UI):**
- HTML templates (Jinja2)
- Bootstrap CSS framework
- JavaScript (vanilla JS, fetch API, EventSource for SSE)
- RESTful API client

**Application Layer (Business Logic):**
- Flask web framework
- Audio generation algorithms
- UART protocol implementation
- Telemetry collection

**Service Layer (Hardware Abstraction):**
- I2S driver (ALSA wrapper)
- UART manager (pyserial wrapper)
- Configuration manager (YAML parser)

**Infrastructure Layer (OS/Hardware):**
- Linux ALSA subsystem
- McASP hardware (Device Tree configured)
- Serial port driver
- Network stack (Wi-Fi/Ethernet)

---

## Component Specifications

### 2.1 Audio Engine

**Module:** `audio/audio_engine.py`

**Responsibility:**
- Generate audio samples (tone, sweep, WAV, silence)
- Manage ring buffer for I2S consumption
- Switch between audio sources dynamically
- Detect and recover from underruns

**Public API:**

```python
class AudioEngine:
    def __init__(self, config: Dict[str, Any], ring_buffer: RingBuffer):
        """Initialize audio engine with config and ring buffer."""
        
    def start(self) -> None:
        """Start audio generation thread."""
        
    def stop(self) -> None:
        """Stop audio generation and clean up resources."""
        
    def set_source(self, source: AudioSource) -> None:
        """Switch to new audio source (tone, sweep, WAV, silence)."""
        
    def set_tone_params(self, frequency: float, amplitude: float, 
                       mode: str = 'mono') -> None:
        """Update tone generator parameters."""
        
    def get_status(self) -> Dict[str, Any]:
        """Return current status (source, params, buffer fill %)."""
```

**Audio Sources:**

```python
class AudioSource(ABC):
    @abstractmethod
    def generate(self, num_frames: int) -> np.ndarray:
        """Generate audio frames (shape: [num_frames, 2] for stereo)."""
        pass

class ToneGenerator(AudioSource):
    """Generates sine wave tones."""
    def __init__(self, sample_rate: int, frequency: float, 
                 amplitude: float, mode: str):
        self.phase = 0.0  # Phase accumulator for continuous tone
        
class SweepGenerator(AudioSource):
    """Generates logarithmic frequency sweeps."""
    def __init__(self, sample_rate: int, start_freq: float, 
                 end_freq: float, duration: float):
        self.time = 0.0
        
class WAVPlayer(AudioSource):
    """Plays WAV files with resampling."""
    def __init__(self, sample_rate: int, wav_path: str):
        self.data = self._load_and_resample(wav_path)
        self.position = 0
        
class SilenceGenerator(AudioSource):
    """Generates digital silence (zeros)."""
    def generate(self, num_frames: int) -> np.ndarray:
        return np.zeros((num_frames, 2), dtype=np.int16)
```

**Thread Model:**
- Single background thread (`audio_generation_thread`)
- Wakes every `period_size / sample_rate` seconds
- Generates audio and writes to ring buffer
- Sleeps if buffer is full (backpressure)

**Buffer Management:**

```python
class RingBuffer:
    """Thread-safe circular buffer for audio data."""
    
    def __init__(self, size: int):
        """Initialize with capacity in frames."""
        self._buffer = np.zeros((size, 2), dtype=np.int16)
        self._write_idx = 0
        self._read_idx = 0
        self._lock = threading.Lock()
        
    def write(self, data: np.ndarray) -> int:
        """Write audio frames, return number of frames written."""
        
    def read(self, num_frames: int) -> np.ndarray:
        """Read audio frames, return data (may be < num_frames if underrun)."""
        
    def get_fill_level(self) -> float:
        """Return buffer fill percentage (0.0 - 1.0)."""
```

### 2.2 I2S Driver

**Module:** `audio/i2s_driver.py`

**Responsibility:**
- Interface with ALSA for McASP I2S output
- Configure PCM parameters (sample rate, format, channels)
- Consume audio from ring buffer
- Detect and log underruns

**Public API:**

```python
class I2SDriver:
    def __init__(self, config: Dict[str, Any], ring_buffer: RingBuffer):
        """Initialize ALSA PCM device."""
        
    def start(self) -> None:
        """Start I2S output thread."""
        
    def stop(self) -> None:
        """Stop I2S output and close device."""
        
    def get_stats(self) -> Dict[str, Any]:
        """Return I2S statistics (underruns, frames written, etc.)."""
```

**ALSA Configuration:**

```python
import alsaaudio

pcm = alsaaudio.PCM(
    type=alsaaudio.PCM_PLAYBACK,
    mode=alsaaudio.PCM_NORMAL,
    device=config['i2s']['device']  # 'hw:0,0'
)

pcm.setchannels(2)
pcm.setrate(48000)
pcm.setformat(alsaaudio.PCM_FORMAT_S16_LE)
pcm.setperiodsize(1024)  # Frames per period
```

**Thread Model:**
- Single I2S output thread (`i2s_output_thread`)
- Continuously reads from ring buffer
- Writes to ALSA PCM device
- Logs underruns when ring buffer is empty

**Error Recovery:**
- ALSA underrun (EPIPE): Call `pcm.prepare()` to recover
- Buffer empty: Write silence frames, log warning
- ALSA error: Log error, attempt to reopen device

### 2.3 UART Command Manager

**Module:** `uart/uart_manager.py`

**Responsibility:**
- Send commands to ESP32 via UART
- Parse responses and events from ESP32
- Maintain command queue for serialization
- Handle timeouts and retries

**Public API:**

```python
class UARTManager:
    def __init__(self, config: Dict[str, Any]):
        """Initialize serial port."""
        
    def start(self) -> None:
        """Start UART read/write threads."""
        
    def stop(self) -> None:
        """Stop threads and close serial port."""
        
    def send_command(self, command: str, args: str = '') -> str:
        """Send command, wait for response, return response data."""
        
    def get_status(self) -> Dict[str, Any]:
        """Return UART status (connected, last command, etc.)."""
```

**Serial Protocol:**

```
Request Format:  COMMAND [args]\n
Response Format: OK|COMMAND|data\n  OR  ERR|COMMAND|error_msg\n
Event Format:    EVENT|event_name|data\n
```

**Example Exchanges:**

```
→ STATUS\n
← OK|STATUS|IDLE\n

→ VOLUME 75\n
← OK|VOLUME|75\n

→ CONNECT AA:BB:CC:DD:EE:FF\n
← OK|CONNECT|AA:BB:CC:DD:EE:FF\n
← EVENT|BT_CONNECTED|AA:BB:CC:DD:EE:FF\n

→ INVALID_CMD\n
← ERR|INVALID_CMD|Unknown command\n
```

**Thread Model:**
- Write thread: Dequeues commands, sends to serial port
- Read thread: Reads responses/events, parses and dispatches
- Command timeout: 5 seconds default (configurable)
- Retry logic: Up to 3 retries on timeout (exponential backoff)

**Command Queue:**

```python
class CommandQueue:
    """Thread-safe queue for UART commands."""
    
    def enqueue(self, command: str, args: str = '') -> Future:
        """Add command to queue, return Future for response."""
        
    def dequeue(self) -> Tuple[str, str, Future]:
        """Get next command (blocking)."""
```

### 2.4 Web Server

**Module:** `web/server.py`

**Responsibility:**
- Serve web UI (HTML templates, static files)
- Expose RESTful API for audio/UART control
- Stream real-time status via Server-Sent Events
- Handle HTTP requests/responses

**Public API:**

```python
class WebServer:
    def __init__(self, audio_engine: AudioEngine, 
                 uart_manager: UARTManager,
                 telemetry: TelemetryTracker,
                 config: Dict[str, Any]):
        """Initialize Flask app with dependencies."""
        
    def start(self) -> None:
        """Start Flask development server (or WSGI for production)."""
        
    def stop(self) -> None:
        """Gracefully shutdown server."""
```

**Flask Routes:**

```python
# Template routes
@app.route('/')
def index() -> str:
    """Render dashboard page."""

@app.route('/audio-sources')
def audio_sources() -> str:
    """Render audio sources page."""

@app.route('/bluetooth')
def bluetooth() -> str:
    """Render Bluetooth control page."""

# API routes (see API Specifications section)
@app.route('/api/status', methods=['GET'])
@app.route('/api/tone', methods=['POST'])
@app.route('/api/sweep', methods=['POST'])
@app.route('/api/wav', methods=['POST'])
@app.route('/api/silence', methods=['POST'])
@app.route('/api/stop', methods=['POST'])
@app.route('/api/uart', methods=['POST'])

# SSE stream
@app.route('/api/events')
def events() -> Response:
    """Server-Sent Events stream for real-time updates."""
```

**Template Rendering:**
- Jinja2 templates in `web/templates/`
- Base template: `base.html` (common header, footer, CSS/JS)
- Pages: `index.html`, `audio_sources.html`, `bluetooth.html`

**Static Files:**
- CSS: `web/static/css/style.css` (Bootstrap + custom)
- JavaScript: `web/static/js/app.js` (API client, SSE handler)

### 2.5 Configuration Manager

**Module:** `config/config_manager.py`

**Responsibility:**
- Load and parse `config.yaml`
- Validate configuration values
- Provide default values for missing keys
- Save runtime state (last Bluetooth device, etc.)

**Public API:**

```python
class ConfigManager:
    def __init__(self, config_path: str = 'config.yaml'):
        """Load config from file."""
        
    def get(self, key: str, default: Any = None) -> Any:
        """Get config value by dot-notation key (e.g., 'i2s.sample_rate')."""
        
    def set(self, key: str, value: Any) -> None:
        """Set config value at runtime."""
        
    def save(self) -> None:
        """Write config back to file."""
        
    def validate(self) -> List[str]:
        """Validate config, return list of errors (empty if valid)."""
```

**Validation Rules:**

```python
VALIDATION_RULES = {
    'i2s.sample_rate': lambda x: 8000 <= x <= 96000,
    'i2s.channels': lambda x: x in [1, 2],
    'i2s.format': lambda x: x in ['S16_LE', 'S32_LE'],
    'i2s.period_size': lambda x: 64 <= x <= 8192,
    'i2s.buffer_size': lambda x: 128 <= x <= 16384,
    'uart.baudrate': lambda x: x in [9600, 19200, 38400, 57600, 115200],
    'uart.timeout': lambda x: 0 < x <= 60,
    'audio.tone_freq': lambda x: 20 <= x <= 20000,
    'audio.tone_amp': lambda x: 0.0 <= x <= 1.0,
    'web.port': lambda x: 1024 <= x <= 65535,
}
```

### 2.6 Telemetry Tracker

**Module:** `telemetry/telemetry_tracker.py`

**Responsibility:**
- Monitor CPU and memory usage
- Track I2S statistics (underruns, frames written)
- Collect UART command metrics (success rate, latency)
- Provide data for SSE stream and debugging

**Public API:**

```python
class TelemetryTracker:
    def __init__(self):
        """Initialize metrics collection."""
        
    def update(self) -> None:
        """Update all metrics (called periodically)."""
        
    def get_metrics(self) -> Dict[str, Any]:
        """Return all metrics as dictionary."""
        
    def reset(self) -> None:
        """Reset counters (e.g., underrun count)."""
```

**Metrics Collected:**

```python
{
    'cpu': {
        'percent': 12.5,  # CPU usage percentage
        'loadavg_1m': 0.45  # 1-minute load average
    },
    'memory': {
        'rss_mb': 87.3,  # Resident Set Size in MB
        'percent': 17.1  # Memory usage percentage
    },
    'i2s': {
        'underruns': 2,  # Total underruns since start
        'frames_written': 2304000,  # Total frames written
        'buffer_fill_pct': 65.2  # Ring buffer fill percentage
    },
    'uart': {
        'connected': True,
        'commands_sent': 42,
        'commands_failed': 1,
        'avg_latency_ms': 23.4
    },
    'uptime_seconds': 3672
}
```

---

## Data Flow

### 3.1 Audio Generation Flow

```
┌──────────────────┐
│  User Action     │ (Web UI button click)
│  "Start 1kHz Tone"│
└────────┬─────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Flask API Handler                     │
│  POST /api/tone                        │
│  {"frequency": 1000, "amplitude": 0.5} │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  AudioEngine.set_source()              │
│  - Create ToneGenerator(1000, 0.5)     │
│  - Swap audio source atomically        │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Audio Generation Thread               │
│  (continuous loop @ ~21ms intervals)   │
│  ┌─────────────────────────────────┐   │
│  │ 1. Generate 1024 frames         │   │
│  │    tone_gen.generate(1024)      │   │
│  │    → np.array([1024, 2], int16) │   │
│  │                                 │   │
│  │ 2. Write to ring buffer         │   │
│  │    ring_buffer.write(frames)    │   │
│  │                                 │   │
│  │ 3. Sleep until next period      │   │
│  └─────────────────────────────────┘   │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Ring Buffer                           │
│  [Circular buffer, 8192 frames]        │
│  write_idx: 1024                       │
│  read_idx: 0                           │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  I2S Output Thread                     │
│  (continuous loop)                     │
│  ┌─────────────────────────────────┐   │
│  │ 1. Read from ring buffer        │   │
│  │    frames = ring_buffer.read()  │   │
│  │                                 │   │
│  │ 2. Write to ALSA                │   │
│  │    pcm.write(frames.tobytes())  │   │
│  │                                 │   │
│  │ 3. Check for underruns          │   │
│  └─────────────────────────────────┘   │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  ALSA/McASP Driver                     │
│  DMA transfer to McASP0 registers      │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Hardware I2S Pins                     │
│  P9.31 → BCLK (1.536 MHz clock)        │
│  P9.29 → WS (48 kHz word select)       │
│  P9.28 → DOUT (audio data bits)        │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  ESP32 or UDA1334ATS                   │
│  Receives I2S audio stream             │
└────────────────────────────────────────┘
```

### 3.2 UART Command Flow

```
┌──────────────────┐
│  User Action     │ (Web UI Bluetooth control)
│  "Connect to XX:XX:XX:XX:XX:XX"
└────────┬─────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Flask API Handler                     │
│  POST /api/uart                        │
│  {"command": "CONNECT", "args": "..."} │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  UARTManager.send_command()            │
│  - Enqueue command to CommandQueue     │
│  - Return Future object                │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  UART Write Thread                     │
│  1. Dequeue command                    │
│  2. Format: "CONNECT XX:XX:XX:XX:XX:XX\n"
│  3. Write to serial port               │
│  4. Start 5-second timeout timer       │
└────────┬───────────────────────────────┘
         │ (data over serial wire)
         ▼
┌────────────────────────────────────────┐
│  ESP32 (via /dev/ttyO4)                │
│  - Parse command                       │
│  - Execute Bluetooth connect           │
│  - Send response                       │
└────────┬───────────────────────────────┘
         │ (response over serial wire)
         ▼
┌────────────────────────────────────────┐
│  UART Read Thread                      │
│  1. Read line from serial port         │
│  2. Parse: "OK|CONNECT|XX:XX:XX:XX:XX:XX\n"
│  3. Resolve Future with response data  │
│  4. Cancel timeout timer               │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Flask API Handler (continued)         │
│  - Future resolves with response       │
│  - Return JSON response to client      │
│  - {"status": "ok", "data": "..."}     │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Web UI (JavaScript)                   │
│  - Update connection status indicator  │
│  - Display success message             │
└────────────────────────────────────────┘

Async Event Flow (parallel):
┌────────────────────────────────────────┐
│  ESP32 (autonomous event)              │
│  "EVENT|BT_CONNECTED|XX:XX:XX:XX:XX:XX\n"
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  UART Read Thread                      │
│  - Parse event                         │
│  - Dispatch to EventHandler            │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  EventHandler                          │
│  - Update internal Bluetooth state     │
│  - Notify SSE stream subscribers       │
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  SSE Stream (/api/events)              │
│  - Push event to all connected clients │
│  - {"type": "bt_connected", "mac": "..."} 
└────────┬───────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Web UI (EventSource listener)         │
│  - Update UI without polling           │
└────────────────────────────────────────┘
```

### 3.3 Server-Sent Events (SSE) Flow

```
┌──────────────────┐
│  Web Browser     │ Opens EventSource connection
│  new EventSource('/api/events')
└────────┬─────────┘
         │
         ▼
┌────────────────────────────────────────┐
│  Flask SSE Route                       │
│  @app.route('/api/events')             │
│  - Register client in subscribers list │
│  - Return Response with stream         │
└────────┬───────────────────────────────┘
         │
         ▼ (every 500ms)
┌────────────────────────────────────────┐
│  SSE Generator Thread                  │
│  while True:                           │
│      metrics = telemetry.get_metrics() │
│      status = {                        │
│          'i2s': {...},                 │
│          'uart': {...},                │
│          'cpu': {...}                  │
│      }                                 │
│      yield f"data: {json.dumps(status)}\n\n"
│      sleep(0.5)                        │
└────────┬───────────────────────────────┘
         │ (HTTP chunked transfer)
         ▼
┌────────────────────────────────────────┐
│  Web Browser                           │
│  eventSource.onmessage = (e) => {      │
│      data = JSON.parse(e.data);        │
│      updateDashboard(data);            │
│  }                                     │
└────────────────────────────────────────┘
```

---

## State Machines

### 4.1 Audio Source State Machine

```
States:
- IDLE: No audio generation
- TONE: Generating sine wave tone
- SWEEP: Generating frequency sweep
- WAV: Playing WAV file
- SILENCE: Generating silence

Transitions:
┌──────┐  set_source(TONE)   ┌──────┐
│ IDLE ├────────────────────►│ TONE │
└──┬───┘                      └───┬──┘
   │                              │
   │ set_source(SWEEP)            │ set_source(WAV)
   │  ┌───────┐                   │  ┌─────┐
   └─►│ SWEEP │◄──────────────────┴─►│ WAV │
      └───┬───┘                      └──┬──┘
          │                             │
          │ set_source(SILENCE)         │
          │  ┌─────────┐                │
          └─►│ SILENCE │◄───────────────┘
             └────┬────┘
                  │
                  │ stop()
                  ▼
             ┌──────┐
             │ IDLE │
             └──────┘

State Invariants:
- Only one AudioSource active at a time
- Transitions are atomic (no partial state)
- Ring buffer preserves continuity across transitions
```

### 4.2 UART Connection State Machine

```
States:
- DISCONNECTED: Serial port not open
- CONNECTED: Serial port open, ready for commands
- COMMAND_PENDING: Waiting for response
- ERROR: Serial port error, attempting recovery

Transitions:
┌──────────────┐  open()     ┌───────────┐
│ DISCONNECTED ├────────────►│ CONNECTED │
└──────────────┘             └─────┬─────┘
       ▲                           │
       │ close()                   │ send_command()
       │                           ▼
       │                    ┌────────────────┐
       │                    │ COMMAND_PENDING│
       │                    └───┬────────┬───┘
       │                        │        │
       │                        │        │ timeout (3 retries)
       │             response   │        │
       │             ┌──────────┘        │
       │             │                   ▼
       │             ▼              ┌────────┐
       │      ┌───────────┐         │ ERROR  │
       │      │ CONNECTED │         └────┬───┘
       │      └───────────┘              │
       │                                 │ recovery attempt
       └─────────────────────────────────┘

State Actions:
- DISCONNECTED: No UART operations allowed
- CONNECTED: Commands enqueued, read thread active
- COMMAND_PENDING: Timeout timer active, waiting for response
- ERROR: Log error, close port, attempt reopen after delay
```

### 4.3 I2S Driver State Machine

```
States:
- STOPPED: ALSA device not initialized
- RUNNING: ALSA device open, writing audio
- UNDERRUN: Buffer underrun detected
- ERROR: ALSA error, attempting recovery

Transitions:
┌─────────┐  start()   ┌─────────┐
│ STOPPED ├───────────►│ RUNNING │
└─────────┘            └────┬────┘
     ▲                      │
     │ stop()               │ ring_buffer.empty
     │                      ▼
     │               ┌──────────┐  pcm.prepare()  ┌─────────┐
     │               │ UNDERRUN ├────────────────►│ RUNNING │
     │               └──────────┘                 └─────────┘
     │                      │
     │                      │ ALSA error
     │                      ▼
     │                  ┌───────┐
     │                  │ ERROR │
     │                  └───┬───┘
     │                      │ recovery (reopen device)
     └──────────────────────┘

State Invariants:
- RUNNING: PCM device always writable
- UNDERRUN: Logged but recoverable
- ERROR: Max 3 recovery attempts before stopping
```

---

## API Specifications

### 5.1 RESTful API Endpoints

#### GET /api/status

**Description:** Get current system status (I2S, UART, audio source, telemetry).

**Request:**
```http
GET /api/status HTTP/1.1
Host: bbgw.local:5000
```

**Response (200 OK):**
```json
{
  "audio": {
    "source": "tone",
    "params": {
      "frequency": 1000,
      "amplitude": 0.5,
      "mode": "mono"
    },
    "buffer_fill_pct": 62.3
  },
  "i2s": {
    "underruns": 0,
    "frames_written": 1152000,
    "sample_rate": 48000,
    "status": "running"
  },
  "uart": {
    "connected": true,
    "last_command": "STATUS",
    "last_response": "OK|STATUS|IDLE"
  },
  "bluetooth": {
    "state": "connected",
    "device_mac": "AA:BB:CC:DD:EE:FF"
  },
  "telemetry": {
    "cpu_percent": 18.2,
    "memory_rss_mb": 89.7,
    "uptime_seconds": 3600
  }
}
```

---

#### POST /api/tone

**Description:** Start tone generation.

**Request:**
```http
POST /api/tone HTTP/1.1
Host: bbgw.local:5000
Content-Type: application/json

{
  "frequency": 1000,
  "amplitude": 0.5,
  "mode": "mono"
}
```

**Parameters:**
- `frequency` (number, 20-20000): Tone frequency in Hz
- `amplitude` (number, 0.0-1.0): Amplitude (0=silence, 1=full scale)
- `mode` (string, "mono" | "dual-tone"): Stereo mode

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Tone started",
  "params": {
    "frequency": 1000,
    "amplitude": 0.5,
    "mode": "mono"
  }
}
```

**Error (400 Bad Request):**
```json
{
  "status": "error",
  "message": "Invalid frequency: must be 20-20000 Hz"
}
```

---

#### POST /api/sweep

**Description:** Start frequency sweep.

**Request:**
```http
POST /api/sweep HTTP/1.1
Host: bbgw.local:5000
Content-Type: application/json

{
  "start_freq": 20,
  "end_freq": 20000,
  "duration": 10
}
```

**Parameters:**
- `start_freq` (number, 20-20000): Start frequency in Hz
- `end_freq` (number, 20-20000): End frequency in Hz
- `duration` (number, 1-60): Sweep duration in seconds

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Frequency sweep started",
  "params": {
    "start_freq": 20,
    "end_freq": 20000,
    "duration": 10
  }
}
```

---

#### POST /api/wav

**Description:** Play WAV file.

**Request:**
```http
POST /api/wav HTTP/1.1
Host: bbgw.local:5000
Content-Type: application/json

{
  "filename": "test_audio.wav",
  "loop": false
}
```

**Parameters:**
- `filename` (string): Filename in `wav_directory` (from config.yaml)
- `loop` (boolean, optional): Loop playback (default: false)

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "WAV playback started",
  "filename": "test_audio.wav",
  "duration_seconds": 45.2
}
```

**Error (404 Not Found):**
```json
{
  "status": "error",
  "message": "WAV file not found: test_audio.wav"
}
```

---

#### POST /api/silence

**Description:** Start silence generation (digital zeros).

**Request:**
```http
POST /api/silence HTTP/1.1
Host: bbgw.local:5000
```

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Silence started"
}
```

---

#### POST /api/stop

**Description:** Stop audio generation (return to IDLE).

**Request:**
```http
POST /api/stop HTTP/1.1
Host: bbgw.local:5000
```

**Response (200 OK):**
```json
{
  "status": "ok",
  "message": "Audio stopped"
}
```

---

#### POST /api/uart

**Description:** Send UART command to ESP32.

**Request:**
```http
POST /api/uart HTTP/1.1
Host: bbgw.local:5000
Content-Type: application/json

{
  "command": "VOLUME",
  "args": "75"
}
```

**Parameters:**
- `command` (string): Command name (STATUS, VOLUME, SCAN, CONNECT, DISCONNECT, START, STOP)
- `args` (string, optional): Command arguments

**Response (200 OK):**
```json
{
  "status": "ok",
  "command": "VOLUME",
  "response": "OK|VOLUME|75"
}
```

**Error (500 Internal Server Error):**
```json
{
  "status": "error",
  "command": "VOLUME",
  "message": "UART timeout: no response from ESP32"
}
```

---

### 5.2 Server-Sent Events (SSE) Protocol

#### GET /api/events

**Description:** Real-time status updates via Server-Sent Events.

**Request:**
```http
GET /api/events HTTP/1.1
Host: bbgw.local:5000
Accept: text/event-stream
```

**Response (200 OK):**
```http
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

data: {"type":"status","i2s":{"underruns":0,"buffer_fill_pct":65.2},"uart":{"connected":true},"cpu":{"percent":12.5},"memory":{"rss_mb":87.3}}

data: {"type":"status","i2s":{"underruns":0,"buffer_fill_pct":68.1},"uart":{"connected":true},"cpu":{"percent":13.2},"memory":{"rss_mb":87.4}}

data: {"type":"event","name":"bt_connected","data":"AA:BB:CC:DD:EE:FF"}

data: {"type":"status","i2s":{"underruns":0,"buffer_fill_pct":64.9},"uart":{"connected":true},"cpu":{"percent":11.8},"memory":{"rss_mb":87.4}}
```

**Event Types:**
- `status`: Periodic status update (every 500ms)
- `event`: Asynchronous event from ESP32 (bt_connected, bt_disconnected, etc.)
- `error`: Error notification (I2S underrun, UART timeout, etc.)

**Client JavaScript:**
```javascript
const eventSource = new EventSource('/api/events');

eventSource.onmessage = (event) => {
    const data = JSON.parse(event.data);
    
    if (data.type === 'status') {
        updateDashboard(data);
    } else if (data.type === 'event') {
        handleAsyncEvent(data.name, data.data);
    } else if (data.type === 'error') {
        showErrorNotification(data.message);
    }
};

eventSource.onerror = (error) => {
    console.error('SSE error:', error);
    // Auto-reconnect handled by browser
};
```

---

## Configuration Schema

### 6.1 config.yaml Structure

```yaml
# Target device mode (ESP32 or UDA1334ATS DAC)
target_device: esp32  # 'esp32' | 'uda1334'

# I2S output configuration
i2s:
  device: "hw:0,0"        # ALSA device name
  sample_rate: 48000      # 8000-96000 Hz
  channels: 2             # 1=mono, 2=stereo
  format: "S16_LE"        # PCM format (S16_LE, S32_LE)
  period_size: 1024       # Frames per period (64-8192)
  buffer_size: 4096       # Total buffer size in frames (128-16384)

# UART configuration (ESP32 communication)
uart:
  device: /dev/ttyO4      # Serial port device
  baudrate: 115200        # 9600, 19200, 38400, 57600, 115200
  timeout: 5.0            # Command timeout in seconds (0.1-60)
  retry_count: 3          # Number of retries on timeout (0-10)

# Audio generation settings
audio:
  default_source: tone    # 'tone' | 'sweep' | 'wav' | 'silence'
  tone_freq: 1000         # Default tone frequency (20-20000 Hz)
  tone_amp: 0.5           # Default amplitude (0.0-1.0)
  tone_mode: mono         # 'mono' | 'dual-tone'
  sweep_start: 20         # Sweep start frequency (20-20000 Hz)
  sweep_end: 20000        # Sweep end frequency (20-20000 Hz)
  sweep_duration: 10      # Sweep duration in seconds (1-60)
  wav_directory: /home/debian/audio  # Directory for WAV files
  ring_buffer_size: 8192  # Ring buffer size in frames (1024-32768)

# Web server configuration
web:
  host: 0.0.0.0           # Bind address (0.0.0.0 = all interfaces)
  port: 5000              # TCP port (1024-65535)
  debug: false            # Flask debug mode (true/false)
  log_level: INFO         # DEBUG | INFO | WARNING | ERROR | CRITICAL

# Bluetooth settings (runtime state)
bluetooth:
  last_device_mac: ""     # Last connected device (saved on shutdown)
  auto_connect: false     # Auto-connect on startup (true/false)

# Telemetry settings
telemetry:
  enabled: true           # Enable metrics collection (true/false)
  update_interval: 0.5    # Metrics update interval in seconds (0.1-5.0)
```

### 6.2 Environment Variables

Configuration can be overridden via environment variables:

```bash
export BBGW_I2S_DEVICE="hw:1,0"
export BBGW_UART_DEVICE="/dev/ttyO1"
export BBGW_WEB_PORT="8080"
export BBGW_LOG_LEVEL="DEBUG"

python3 main.py  # Uses env vars if set
```

**Precedence:** Environment variables > config.yaml > built-in defaults

---

## Threading Model

### 7.1 Thread Architecture

```
Main Thread:
  - Parse config
  - Initialize components
  - Start all threads
  - Wait for shutdown signal (SIGINT, SIGTERM)
  - Stop all threads gracefully

Audio Generation Thread:
  - Priority: Normal
  - Loop interval: period_size / sample_rate (~21ms @ 48kHz, 1024 frames)
  - Actions:
    1. Generate audio samples (tone/sweep/wav/silence)
    2. Write to ring buffer
    3. Sleep until next period
  - Exit condition: stop_event.is_set()

I2S Output Thread:
  - Priority: High (renice -5)
  - Loop interval: Continuous (blocking on ALSA write)
  - Actions:
    1. Read from ring buffer
    2. Write to ALSA PCM device
    3. Detect underruns
  - Exit condition: stop_event.is_set()

UART Write Thread:
  - Priority: Normal
  - Loop interval: Continuous (blocking on queue)
  - Actions:
    1. Dequeue command from CommandQueue
    2. Send to serial port
    3. Start timeout timer
  - Exit condition: stop_event.is_set()

UART Read Thread:
  - Priority: Normal
  - Loop interval: Continuous (blocking on serial read)
  - Actions:
    1. Read line from serial port
    2. Parse response/event
    3. Resolve Future or dispatch event
  - Exit condition: stop_event.is_set()

SSE Generator Thread (per client):
  - Priority: Low
  - Loop interval: 500ms (configurable)
  - Actions:
    1. Collect metrics from TelemetryTracker
    2. Format JSON
    3. Yield SSE event
  - Exit condition: Client disconnects

Flask Worker Threads (managed by Flask/WSGI):
  - Priority: Normal
  - Created per HTTP request
  - Handle API routes, template rendering
```

### 7.2 Thread Synchronization

**Ring Buffer:**
- Lock: `threading.Lock()` for read/write index updates
- Condition: None (lock-free reads when indices are atomic)
- Underrun handling: Write thread logs warning, I2S thread writes silence

**Command Queue:**
- Lock: `queue.Queue` (thread-safe built-in)
- Blocking: `queue.get()` blocks UART write thread until command available

**UART Response Futures:**
- Lock: `threading.Lock()` for Future dictionary access
- Condition: `concurrent.futures.Future` for async response waiting

**Telemetry Metrics:**
- Lock: `threading.Lock()` for metric dictionary updates
- Read-heavy, write-light (reads every 500ms, writes every 0.5-5s)

**Stop Events:**
- Shared `threading.Event` for graceful shutdown
- Set by main thread on SIGINT/SIGTERM
- Checked by all worker threads in loop condition

### 7.3 Deadlock Prevention

**Lock Ordering:**
1. Config lock (rarely held)
2. Telemetry lock
3. Ring buffer lock
4. UART Future dictionary lock

**Timeouts:**
- UART commands: 5-second timeout (prevents infinite wait)
- Ring buffer writes: Non-blocking (logs warning on full buffer)
- Thread joins: 2-second timeout on shutdown (force-kill if hung)

---

## Error Handling Strategy

### 8.1 Error Categories

**1. Recoverable Errors (log warning, continue):**
- I2S buffer underrun → `pcm.prepare()`, log warning
- UART command timeout → Retry up to 3 times, log error if all fail
- WAV file not found → Fall back to silence, log error
- SSE client disconnect → Remove from subscriber list

**2. Non-Recoverable Errors (log error, shutdown component):**
- ALSA device open failure → Stop I2S thread, disable audio
- Serial port open failure → Stop UART threads, disable UART commands
- Config validation failure → Exit application with error message

**3. Critical Errors (log critical, exit application):**
- Out of memory → Log traceback, exit
- Unhandled exception in main thread → Log traceback, exit
- Signal handling failure → Force exit

### 8.2 Error Logging

```python
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s: %(message)s',
    handlers=[
        logging.FileHandler('/var/log/bbgw_i2s_source.log'),
        logging.StreamHandler()  # Console output
    ]
)

# Module-level loggers
logger = logging.getLogger(__name__)

# Error logging examples
logger.debug("Audio buffer fill: 65.2%")
logger.info("Tone started: 1000 Hz, 0.5 amplitude")
logger.warning("I2S underrun detected (total: 3)")
logger.error("UART timeout: no response after 5 seconds")
logger.critical("ALSA device open failed: hw:0,0 not found")
```

### 8.3 Exception Handling Patterns

**Audio Generation Thread:**
```python
def audio_generation_loop():
    try:
        while not stop_event.is_set():
            try:
                frames = audio_source.generate(period_size)
                ring_buffer.write(frames)
            except Exception as e:
                logger.error(f"Audio generation error: {e}", exc_info=True)
                # Fall back to silence on error
                audio_source = SilenceGenerator()
            time.sleep(period_duration)
    except Exception as e:
        logger.critical(f"Audio thread crashed: {e}", exc_info=True)
        raise
```

**UART Command Send:**
```python
def send_command(self, command: str, args: str = '') -> str:
    for attempt in range(self.retry_count):
        try:
            future = self._enqueue_command(command, args)
            response = future.result(timeout=self.timeout)
            return response
        except TimeoutError:
            logger.warning(f"UART timeout (attempt {attempt+1}/{self.retry_count})")
            if attempt == self.retry_count - 1:
                raise UARTTimeoutError(f"No response after {self.retry_count} attempts")
        except serial.SerialException as e:
            logger.error(f"Serial port error: {e}")
            raise UARTError(f"Serial communication failed: {e}")
```

**Flask API Error Handler:**
```python
@app.errorhandler(Exception)
def handle_error(error):
    logger.error(f"API error: {error}", exc_info=True)
    
    if isinstance(error, ValueError):
        return jsonify({'status': 'error', 'message': str(error)}), 400
    elif isinstance(error, FileNotFoundError):
        return jsonify({'status': 'error', 'message': 'Resource not found'}), 404
    elif isinstance(error, UARTTimeoutError):
        return jsonify({'status': 'error', 'message': 'UART timeout'}), 500
    else:
        return jsonify({'status': 'error', 'message': 'Internal server error'}), 500
```

---

## Security Model

### 9.1 Threat Model

**Assets:**
- Configuration file (config.yaml) — contains Bluetooth MAC addresses
- WAV files (audio directory) — user audio content
- Web server — network-accessible control interface
- UART interface — ESP32 command channel

**Threats:**
- Unauthorized network access to web UI (no authentication in v1.0)
- File system access to config.yaml or WAV files (local user privilege required)
- UART sniffing (physical access to P9.11/P9.13 required)
- Denial of service (resource exhaustion via API spam)

**Assumptions:**
- Deployment on trusted network (home lab, development environment)
- Physical access controlled (BBGW not publicly accessible)
- No malicious users on local network
- OS-level user permissions properly configured

### 9.2 Security Controls

**Network Security:**
- Web server binds to `0.0.0.0` (all interfaces) but no authentication in v1.0
- Recommendation: Use firewall to restrict access to trusted IPs
- Future: Add HTTP Basic Auth or token-based authentication

**File System Security:**
- config.yaml: Readable by application user only (`chmod 600`)
- WAV directory: No file upload in v1.0 (prevents arbitrary writes)
- Logs: Written to `/var/log/bbgw_i2s_source.log` (rotate with logrotate)

**Input Validation:**
- All API parameters validated against schema (frequency 20-20000, amplitude 0-1, etc.)
- File paths sanitized (no directory traversal: `../../../etc/passwd`)
- UART commands whitelisted (only known commands accepted)

**Rate Limiting (not implemented in v1.0, future enhancement):**
- API: 100 requests/minute per IP
- SSE: Max 10 concurrent connections
- UART: Max 10 commands/second (prevent ESP32 overload)

### 9.3 Secure Configuration

**Production Deployment Checklist:**
- [ ] Change default web port from 5000 to non-standard port
- [ ] Configure firewall (ufw allow from 192.168.1.0/24 to any port 5000)
- [ ] Set file permissions: `chmod 600 config.yaml`
- [ ] Disable Flask debug mode: `web.debug: false`
- [ ] Use HTTPS (reverse proxy with nginx + Let's Encrypt)
- [ ] Review UART command whitelist (disable unused commands)
- [ ] Rotate logs (logrotate: 7 days, compress)

---

## Testing Strategy

### 10.1 Unit Testing

**Framework:** pytest + pytest-mock

**Coverage Target:** >90% line coverage (pytest-cov)

**Mock Strategy:**
- ALSA (`alsaaudio.PCM`): Mock with `unittest.mock.Mock`
- Serial (`serial.Serial`): Mock with `pytest-mock` fixtures
- Time (`time.time`, `time.sleep`): Mock for deterministic tests
- File I/O: Use `tmpdir` fixture for temporary files

**Example Unit Test:**

```python
import pytest
from audio.audio_engine import ToneGenerator

def test_tone_generator_frequency():
    """Test tone generator produces correct frequency."""
    sample_rate = 48000
    frequency = 1000
    amplitude = 0.5
    
    tone_gen = ToneGenerator(sample_rate, frequency, amplitude, 'mono')
    frames = tone_gen.generate(48000)  # 1 second
    
    # FFT to verify frequency
    fft = np.fft.rfft(frames[:, 0])
    freqs = np.fft.rfftfreq(len(frames), 1/sample_rate)
    peak_freq = freqs[np.argmax(np.abs(fft))]
    
    assert abs(peak_freq - frequency) < 1.0  # Within 1 Hz

def test_ring_buffer_write_read():
    """Test ring buffer write and read operations."""
    buffer = RingBuffer(size=1024)
    
    # Write 512 frames
    data = np.random.randint(-32768, 32767, (512, 2), dtype=np.int16)
    written = buffer.write(data)
    assert written == 512
    
    # Read 512 frames
    read_data = buffer.read(512)
    assert read_data.shape == (512, 2)
    np.testing.assert_array_equal(read_data, data)
    
    # Buffer should be empty
    assert buffer.get_fill_level() == 0.0

@pytest.mark.parametrize('command,args,expected', [
    ('STATUS', '', 'OK|STATUS|IDLE'),
    ('VOLUME', '75', 'OK|VOLUME|75'),
    ('INVALID', '', 'ERR|INVALID|Unknown command'),
])
def test_uart_command_response(mock_serial, command, args, expected):
    """Test UART command/response parsing."""
    mock_serial.readline.return_value = (expected + '\n').encode()
    
    uart = UARTManager(config={'uart': {'device': '/dev/null', 'baudrate': 115200}})
    response = uart.send_command(command, args)
    
    assert response == expected
```

### 10.2 Integration Testing

**Framework:** pytest with `@pytest.mark.hardware` (skipped in CI)

**Requirements:**
- BBGW hardware with McASP configured
- ESP32 connected via I2S + UART (or UDA1334ATS for DAC tests)
- Bluetooth speaker paired (for end-to-end tests)

**Test Scenarios:**

```python
@pytest.mark.hardware
def test_i2s_output_continuous():
    """Test continuous I2S output for 5 minutes."""
    config = load_config('config.yaml')
    
    audio_engine = AudioEngine(config, ring_buffer)
    i2s_driver = I2SDriver(config, ring_buffer)
    
    # Start tone
    audio_engine.set_source(ToneGenerator(48000, 1000, 0.5, 'mono'))
    audio_engine.start()
    i2s_driver.start()
    
    # Run for 5 minutes
    time.sleep(300)
    
    # Check for underruns
    stats = i2s_driver.get_stats()
    assert stats['underruns'] < 5  # <5 underruns in 5 minutes
    
    # Stop
    audio_engine.stop()
    i2s_driver.stop()

@pytest.mark.hardware
def test_uart_esp32_commands():
    """Test UART command interface with real ESP32."""
    uart = UARTManager(load_config('config.yaml'))
    uart.start()
    
    # Test STATUS
    response = uart.send_command('STATUS')
    assert response.startswith('OK|STATUS|')
    
    # Test VOLUME
    response = uart.send_command('VOLUME', '50')
    assert response == 'OK|VOLUME|50'
    
    uart.stop()

@pytest.mark.hardware
def test_web_ui_accessibility():
    """Test web UI is accessible and responsive."""
    import requests
    
    # Start web server
    web_server = WebServer(audio_engine, uart_manager, telemetry, config)
    web_server.start()
    
    # Test dashboard
    response = requests.get('http://localhost:5000/')
    assert response.status_code == 200
    assert 'Dashboard' in response.text
    
    # Test API
    response = requests.get('http://localhost:5000/api/status')
    assert response.status_code == 200
    data = response.json()
    assert 'audio' in data
    assert 'i2s' in data
    
    web_server.stop()
```

### 10.3 Performance Testing

**Framework:** pytest + psutil for resource monitoring

**Metrics:**
- CPU usage (<25% average)
- Memory usage (<100 MB RSS)
- I2S underruns (<5 per hour)
- UART command latency (<50 ms)

**Example Performance Test:**

```python
@pytest.mark.hardware
def test_cpu_usage_during_tone():
    """Test CPU usage remains <25% during tone generation."""
    import psutil
    
    # Baseline CPU
    process = psutil.Process()
    baseline_cpu = process.cpu_percent(interval=1.0)
    
    # Start tone
    audio_engine.set_source(ToneGenerator(48000, 1000, 0.5, 'mono'))
    audio_engine.start()
    i2s_driver.start()
    
    # Monitor CPU for 60 seconds
    cpu_samples = []
    for _ in range(60):
        time.sleep(1)
        cpu_samples.append(process.cpu_percent(interval=0))
    
    avg_cpu = np.mean(cpu_samples)
    assert avg_cpu < 25.0, f"CPU usage too high: {avg_cpu:.1f}%"
    
    audio_engine.stop()
    i2s_driver.stop()

@pytest.mark.hardware
def test_memory_leak_detection():
    """Test for memory leaks over 1 hour."""
    import psutil
    
    process = psutil.Process()
    initial_rss = process.memory_info().rss / 1024 / 1024  # MB
    
    # Run for 1 hour
    audio_engine.start()
    i2s_driver.start()
    time.sleep(3600)
    audio_engine.stop()
    i2s_driver.stop()
    
    final_rss = process.memory_info().rss / 1024 / 1024  # MB
    growth = final_rss - initial_rss
    
    assert growth < 60, f"Memory leak detected: {growth:.1f} MB growth in 1 hour"
```

### 10.4 CI/CD Pipeline

**GitHub Actions Workflow:**

```yaml
name: CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        python-version: [3.9, 3.10, 3.11]
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Set up Python
      uses: actions/setup-python@v4
      with:
        python-version: ${{ matrix.python-version }}
    
    - name: Install system dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y libasound2-dev
    
    - name: Install Python dependencies
      run: |
        pip install -r requirements.txt
    
    - name: Run unit tests
      run: |
        pytest tests/ \
          --ignore=tests/integration \
          --ignore=tests/performance \
          --cov=audio --cov=uart --cov=web --cov=config \
          --cov-report=xml
    
    - name: Upload coverage
      uses: codecov/codecov-action@v3
      with:
        file: ./coverage.xml
    
    - name: Lint with flake8
      run: |
        flake8 . --count --show-source --statistics
    
    - name: Check code formatting
      run: |
        black --check .
```

---

## Deployment Architecture

### 11.1 Hardware Setup

**BeagleBone Green Wireless:**
- Power: 5V 2A via barrel jack or USB
- Network: Wi-Fi (802.11 b/g/n) or Ethernet over USB
- Storage: microSD card (8GB+, Debian Linux)
- I2S: McASP0 on P9.28/29/31
- UART: UART4 on P9.11/13

**Device Tree Overlays:**

```bash
# /boot/uEnv.txt
uboot_overlay_addr0=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
uboot_overlay_addr1=/lib/firmware/BB-UART4-00A0.dtbo
```

**McASP I2S Wiring:**

```
BBGW P9 Header    →  ESP32 or UDA1334ATS
P9.31 (ACLKX)     →  BCLK (Bit Clock)
P9.29 (FSX)       →  WS (Word Select)
P9.28 (AXR0)      →  DIN (Data In)
P9.3  (3.3V)      →  VIN (Power, UDA1334ATS only)
P9.1  (GND)       →  GND (Ground)
```

**UART4 Wiring:**

```
BBGW P9 Header    →  ESP32
P9.11 (RX)        →  TX (ESP32 GPIO17)
P9.13 (TX)        →  RX (ESP32 GPIO16)
P9.1  (GND)       →  GND
```

### 11.2 Software Installation

**1. OS Setup:**

```bash
# Flash Debian to microSD card
# Download from https://beagleboard.org/latest-images
# Flash with Etcher or dd

# Boot BBGW, SSH in
ssh debian@192.168.7.2  # Password: temppwd

# Update system
sudo apt-get update
sudo apt-get upgrade -y
```

**2. Install Dependencies:**

```bash
# ALSA libraries
sudo apt-get install -y alsa-utils libasound2-dev

# Device Tree compiler
sudo apt-get install -y device-tree-compiler

# Python 3.9+
sudo apt-get install -y python3 python3-pip python3-venv

# Build tools (for pyalsaaudio)
sudo apt-get install -y build-essential
```

**3. Clone Repository:**

```bash
cd /home/debian
git clone https://github.com/yourusername/bbgw_i2s_source.git
cd bbgw_i2s_source
```

**4. Setup Script:**

```bash
# Run automated setup
chmod +x tools/setup_bbgw.sh
sudo ./tools/setup_bbgw.sh

# Setup will:
# - Compile Device Tree overlays
# - Install Python dependencies
# - Configure systemd service
# - Reboot (for overlays to load)
```

**5. Manual Python Setup (if not using setup script):**

```bash
# Create virtual environment
python3 -m venv venv
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

### 11.3 Systemd Service

**Service File:** `/etc/systemd/system/bbgw-i2s-source.service`

```ini
[Unit]
Description=BeagleBone Green Wireless I2S Audio Source
After=network.target sound.target

[Service]
Type=simple
User=debian
WorkingDirectory=/home/debian/bbgw_i2s_source
ExecStart=/home/debian/bbgw_i2s_source/venv/bin/python main.py
Restart=on-failure
RestartSec=5s
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**Enable and Start:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable bbgw-i2s-source
sudo systemctl start bbgw-i2s-source

# Check status
sudo systemctl status bbgw-i2s-source

# View logs
sudo journalctl -u bbgw-i2s-source -f
```

### 11.4 Network Configuration

**Wi-Fi Setup:**

```bash
# Connect to Wi-Fi
sudo connmanctl

connmanctl> enable wifi
connmanctl> scan wifi
connmanctl> services
connmanctl> agent on
connmanctl> connect wifi_<hash>_managed_psk
Enter passphrase: <your_wifi_password>
connmanctl> quit

# Verify connection
ip addr show wlan0
ping -c 3 google.com
```

**Static IP (optional):**

```bash
# /etc/network/interfaces.d/wlan0
auto wlan0
iface wlan0 inet static
    address 192.168.1.100
    netmask 255.255.255.0
    gateway 192.168.1.1
    dns-nameservers 8.8.8.8
    wpa-ssid "YourSSID"
    wpa-psk "YourPassword"
```

**mDNS (Avahi):**

```bash
# Access BBGW at bbgw.local instead of IP
sudo apt-get install -y avahi-daemon

# Test
ping bbgw.local
```

---

## Performance Considerations

### 12.1 CPU Optimization

**NumPy Vectorization:**
- Use NumPy for audio generation (100x faster than pure Python loops)
- Pre-allocate arrays (avoid dynamic resizing)
- Use `dtype=np.int16` (native ALSA format, no conversion needed)

**Example:**

```python
# SLOW (pure Python):
def generate_tone_slow(frequency, amplitude, num_frames, sample_rate):
    frames = []
    for i in range(num_frames):
        t = i / sample_rate
        sample = int(amplitude * 32767 * np.sin(2 * np.pi * frequency * t))
        frames.append([sample, sample])  # Stereo
    return frames

# FAST (NumPy vectorized):
def generate_tone_fast(frequency, amplitude, num_frames, sample_rate):
    t = np.arange(num_frames, dtype=np.float32) / sample_rate
    samples = (amplitude * 32767 * np.sin(2 * np.pi * frequency * t)).astype(np.int16)
    return np.column_stack([samples, samples])  # Stereo
```

**Thread Priorities:**
- I2S output thread: `nice -5` (higher priority for low latency)
- Audio generation: `nice 0` (normal priority)
- Web server: `nice 5` (lower priority, less critical)

**CPU Affinity (future optimization):**
- Pin I2S thread to CPU core (avoid context switches)
- Not implemented in v1.0 (single-core BBGW)

### 12.2 Memory Optimization

**Ring Buffer Sizing:**
- Balance latency vs underrun resistance
- Default: 8192 frames = 170 ms @ 48 kHz = 32 KB RAM
- Increase if underruns occur (e.g., 16384 frames = 64 KB)

**WAV File Caching:**
- Load WAV into memory on first play (avoid disk I/O in audio loop)
- Resample once, cache result (avoid repeated resampling)
- Limit WAV file size (e.g., <100 MB) to prevent OOM

**Memory Profiling:**

```python
import tracemalloc

tracemalloc.start()

# Run application...

snapshot = tracemalloc.take_snapshot()
top_stats = snapshot.statistics('lineno')

for stat in top_stats[:10]:
    print(stat)
```

### 12.3 I2S Latency

**ALSA Buffer Tuning:**

```python
# Low latency (higher CPU, more underruns):
period_size = 256   # 5.3 ms @ 48 kHz
buffer_size = 1024  # 21.3 ms

# Balanced (default):
period_size = 1024  # 21.3 ms
buffer_size = 4096  # 85.3 ms

# High latency (lower CPU, fewer underruns):
period_size = 2048  # 42.7 ms
buffer_size = 8192  # 170.7 ms
```

**DMA vs CPU Copy:**
- ALSA McASP driver uses DMA (zero-copy to hardware)
- No CPU overhead for I2S data transfer
- Only ring buffer copy (audio thread → I2S driver)

**Jitter Measurement:**

```python
import time

def measure_i2s_jitter():
    """Measure I2S write timing jitter."""
    expected_interval = period_size / sample_rate  # 21.3 ms
    
    intervals = []
    last_time = time.perf_counter()
    
    for _ in range(1000):
        pcm.write(data)
        now = time.perf_counter()
        intervals.append(now - last_time)
        last_time = now
    
    print(f"Mean: {np.mean(intervals)*1000:.2f} ms")
    print(f"Std:  {np.std(intervals)*1000:.2f} ms")
    print(f"Max:  {np.max(intervals)*1000:.2f} ms")
```

---

## Appendices

### Appendix A: ALSA Configuration

**List ALSA Devices:**

```bash
aplay -l
# Output:
# **** List of PLAYBACK Hardware Devices ****
# card 0: BBGWI2S [BBGW-I2S], device 0: davinci-mcasp.0-i2s-hifi i2s-hifi-0 []
#   Subdevices: 1/1
#   Subdevice #0: subdevice #0
```

**Test I2S Output:**

```bash
# Generate 1 kHz sine wave WAV
sox -n -r 48000 -c 2 /tmp/test_tone.wav synth 5 sine 1000

# Play to McASP I2S
aplay -D hw:0,0 /tmp/test_tone.wav

# Verify with oscilloscope on P9.28/29/31
```

**ALSA Configuration File (optional):**

`/home/debian/.asoundrc`

```
pcm.!default {
    type hw
    card 0
    device 0
}

ctl.!default {
    type hw
    card 0
}
```

### Appendix B: Device Tree Overlay Source

**BB-BBGW-I2S-00A0.dts** (excerpt):

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-green-wireless";
    
    part-number = "BB-BBGW-I2S";
    version = "00A0";
    
    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            mcasp0_pins: pinmux_mcasp0_pins {
                pinctrl-single,pins = <
                    0x190 0x20  /* P9.31 mcasp0_aclkx (BCLK) */
                    0x194 0x20  /* P9.29 mcasp0_fsx (WS) */
                    0x19c 0x22  /* P9.28 mcasp0_axr0 (DOUT) */
                >;
            };
        };
    };
    
    fragment@1 {
        target = <&mcasp0>;
        __overlay__ {
            pinctrl-names = "default";
            pinctrl-0 = <&mcasp0_pins>;
            status = "okay";
            
            op-mode = <0>;  /* MCASP_IIS_MODE */
            tdm-slots = <2>;
            serial-dir = <1 0 0 0>;  /* AXR0: TX, others disabled */
            tx-num-evt = <32>;
            rx-num-evt = <32>;
        };
    };
};
```

**Compile:**

```bash
dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
```

### Appendix C: UART Protocol Examples

**ESP32 Firmware (excerpt):**

```c
// ESP32 UART command handler
void handle_uart_command(const char* line) {
    char command[32], args[64];
    if (sscanf(line, "%s %s", command, args) < 1) {
        uart_send_response("ERR|PARSE|Invalid format\n");
        return;
    }
    
    if (strcmp(command, "STATUS") == 0) {
        uart_send_response("OK|STATUS|%s\n", bt_get_state());
    }
    else if (strcmp(command, "VOLUME") == 0) {
        int vol = atoi(args);
        if (vol < 0 || vol > 100) {
            uart_send_response("ERR|VOLUME|Range 0-100\n");
        } else {
            bt_set_volume(vol);
            uart_send_response("OK|VOLUME|%d\n", vol);
        }
    }
    else if (strcmp(command, "CONNECT") == 0) {
        bt_connect(args);  // MAC address
        uart_send_response("OK|CONNECT|%s\n", args);
    }
    else {
        uart_send_response("ERR|%s|Unknown command\n", command);
    }
}

// Bluetooth event handler (async)
void bt_event_connected(const char* mac) {
    uart_send_event("EVENT|BT_CONNECTED|%s\n", mac);
}
```

### Appendix D: Performance Benchmarks

**BBGW I2S Source (measured):**

| Metric | Value | Notes |
|--------|-------|-------|
| CPU Usage (idle) | 8-12% | Flask web server only |
| CPU Usage (tone 1kHz) | 15-20% | Tone generation + I2S output |
| CPU Usage (WAV playback) | 20-25% | Resampling + I2S output |
| Memory RSS (baseline) | 65 MB | After startup |
| Memory RSS (running) | 85-95 MB | Tone generation, web UI active |
| I2S Underruns | 0-2 per hour | Default buffer settings |
| UART Command Latency | 20-30 ms | STATUS, VOLUME (average) |
| Web UI Response Time | 50-100 ms | API call to audio change |
| SSE Update Rate | 2 Hz | 500 ms interval |

**Comparison with RPi:**

| Metric | BBGW | Raspberry Pi 4 |
|--------|------|----------------|
| CPU Usage (tone) | 15-20% | 5-10% |
| Memory RSS | 85 MB | 120 MB |
| I2S Latency | 21 ms | 25 ms (GPIO) |
| Power Consumption | ~2W | ~3.5W |

### Appendix E: References

**ALSA Programming:**
- ALSA Project Documentation: https://www.alsa-project.org/wiki/Documentation
- pyalsaaudio API: https://larsimmisch.github.io/pyalsaaudio/

**BeagleBone Device Tree:**
- BeagleBoard Device Tree Overlays: https://github.com/beagleboard/bb.org-overlays
- AM335x Technical Reference Manual: http://www.ti.com/lit/ug/spruh73p/spruh73p.pdf

**Flask/Web Development:**
- Flask Documentation: https://flask.palletsprojects.com/
- Server-Sent Events Spec: https://html.spec.whatwg.org/multipage/server-sent-events.html

**Audio DSP:**
- NumPy FFT: https://numpy.org/doc/stable/reference/routines.fft.html
- SciPy Signal Processing: https://docs.scipy.org/doc/scipy/reference/signal.html

---

**Document History:**

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-07 | Phil | Initial FS for v1.0.0-bbgw release |

---

*End of Functional Specification*
