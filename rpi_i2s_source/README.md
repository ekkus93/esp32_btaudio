# Raspberry Pi I2S Source

**Rapid development I2S audio test jig for esp_bt_audio_source**

A Python-based I2S master transmitter running on Raspberry Pi to accelerate testing of the ESP32 Bluetooth audio sink. Provides tone generation, frequency sweeps, WAV playback, and UART control—all through a web UI.

---

## Why Raspberry Pi?

**Development Speed:** Python + RPi = 10-20x faster iteration vs ESP-IDF + ESP32-S3
- **Working test jig in 1-2 days** (16 hours) vs weeks-long ESP32-S3 development
- No build/flash cycle—edit Python code and run immediately
- Rich ecosystem: NumPy, Flask, pyserial, SciPy

**Strategy:** Use RPi for all esp_bt_audio_source testing while developing ESP32-S3 production features in parallel.

---

## Hardware Requirements

### Raspberry Pi
- **Recommended:** Raspberry Pi 4 (2GB+ RAM) for development
- **Supported:** RPi 3 B+, RPi Zero 2 W, RPi 5
- **OS:** Raspberry Pi OS (Bookworm or later, 64-bit recommended)

### ESP32 Target
- **esp_bt_audio_source** running on ESP32/ESP32-S3
- Must be configured as I2S slave receiver (BCLK input, WS input, DIN input)

### Wiring (RPi ↔ ESP32)

**I2S Interface:**
```
RPi (BCM GPIO)          ESP32
GPIO18 (BCLK) ────────► GPIO26 (BCLK)
GPIO19 (WS)   ────────► GPIO25 (WS)
GPIO21 (DOUT) ────────► GPIO22 (DIN)
GND           ────────► GND
```

**UART Interface:**
```
RPi (BCM GPIO)          ESP32
GPIO14 (TXD0) ────────► GPIO17 (RX)
GPIO15 (RXD0) ◄──────── GPIO16 (TX)
GND           ────────► GND
```

**Notes:**
- Keep I2S wires <30 cm (minimize noise)
- Use 3.3V logic levels (RPi GPIO is 3.3V, ESP32 GPIO is 3.3V tolerant)
- No level shifters needed

---

## Software Dependencies

### System Packages (Raspberry Pi OS)
```bash
sudo apt update
sudo apt install -y python3-pip python3-venv
sudo apt install -y alsa-utils  # For ALSA I2S driver (recommended)
# OR
# sudo apt install -y pigpio      # For pigpio I2S driver (advanced)
```

### Python Dependencies
Installed automatically via `pip install -r requirements.txt`:
- Flask 3.0 (web server)
- pyserial 3.5 (UART communication)
- NumPy 1.24 (audio generation)
- SciPy 1.11 (WAV processing, sweeps)
- PyYAML 6.0 (configuration)
- psutil (system monitoring)
- pytest (testing framework)

---

## Installation

### Quick Setup (Automated)

Run the automated setup script on your Raspberry Pi:

```bash
cd /home/pi
git clone https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/rpi_i2s_source
bash setup_rpi.sh
```

The script will:
- ✅ Update system packages
- ✅ Install Python 3, pip, venv, and build tools
- ✅ Install ALSA or pigpio (your choice)
- ✅ Configure UART (disable Bluetooth on UART)
- ✅ Create Python virtual environment and install dependencies
- ✅ Create audio directory (`/home/pi/audio`)
- ✅ Create `config.yaml` from template
- ✅ Add user to `dialout` group for UART access

**Reboot if prompted** (required for UART configuration).

---

### Manual Setup (Step-by-Step)

If you prefer manual setup or need to customize:

### 1. Clone Repository
```bash
cd /home/pi
git clone https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/rpi_i2s_source
```

### 2. Create Python Virtual Environment
```bash
python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

### 3. Configure UART (Disable Bluetooth on UART)
```bash
sudo nano /boot/config.txt
# Add this line:
dtoverlay=disable-bt

# Save and reboot:
sudo reboot
```

Verify UART device exists after reboot:
```bash
ls -l /dev/serial0
# Should show: lrwxrwxrwx 1 root root 5 ... /dev/serial0 -> ttyAMA0
```

### 4. Create Configuration File
```bash
cp config.yaml.template config.yaml
nano config.yaml  # Customize if needed (GPIO pins, paths, etc.)
```

### 5. Create Audio Directory
```bash
mkdir -p /home/pi/audio
# Optional: Copy test WAV files to /home/pi/audio/
```

---

## Quick Start

### 1. Start the Application
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
python main.py
```

You should see:
```
INFO:main:Starting Raspberry Pi I2S Source...
INFO:main:Configuration loaded from config.yaml
INFO:main:Ring buffer created (8192 samples)
INFO:main:Audio engine started
INFO:main:I2S driver started (ALSA, 48 kHz stereo)
INFO:main:UART manager started (/dev/serial0, 115200 baud)
INFO:main:Web server starting on http://0.0.0.0:5000
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:5000
 * Running on http://192.168.1.100:5000  # Your RPi IP
```

### 2. Access Web UI
Open browser on your laptop (same LAN as RPi):
```
http://<raspberry-pi-ip>:5000
```

**Example:** `http://192.168.1.100:5000`

### 3. Test I2S Audio
1. **Dashboard Tab:**
   - Select "Tone" audio source
   - Set frequency to 1000 Hz
   - Set amplitude to 50%
   - Click "Start"

2. **Bluetooth Tab:**
   - Click "SCAN" to discover nearby Bluetooth speakers
   - Click "CONNECT" next to your speaker's MAC address
   - Wait for connection (10-20 seconds)
   - Click "START" to begin playback

3. **Verify Audio:**
   - You should hear a 1 kHz sine tone from the Bluetooth speaker
   - I2S status panel shows "Active", buffer fill ~50%, zero underruns

---

## Features

### Audio Sources
- **Tone Generator:** Sine wave, 20 Hz - 20 kHz, configurable amplitude
  - Modes: Mono, Left-only, Right-only, Dual-tone (stereo)
- **Frequency Sweep:** Logarithmic chirp 20 Hz → 20 kHz
- **WAV Playback:** Load WAV files from `/home/pi/audio/`
  - Auto-resample to 48 kHz if needed
  - Stereo or mono (mono → duplicate to both channels)

### UART Control (esp_bt_audio_source commands)
- **SCAN:** Discover Bluetooth devices
- **CONNECT <MAC>:** Pair and connect to speaker
- **DISCONNECT:** Disconnect current device
- **START/STOP:** Control Bluetooth playback
- **VOLUME <0-100>:** Adjust speaker volume
- **STATUS:** Query Bluetooth connection state

### Web UI
- **Real-time telemetry:** I2S buffer health, underruns, BT connection state
- **Audio controls:** Frequency/amplitude sliders, source selector
- **Bluetooth panel:** Device scan, connect, volume, playback control
- **Log viewer:** UART command history, system diagnostics

---

## Project Structure

```
rpi_i2s_source/
├── main.py                     # Application entry point
├── requirements.txt            # Python dependencies
├── config.yaml.template        # Default configuration (copy to config.yaml)
├── config.yaml                 # User configuration (gitignored)
├── .gitignore
├── README.md                   # This file
├── audio/
│   ├── __init__.py
│   ├── engine.py               # AudioEngine class (tone/sweep/WAV generation)
│   ├── i2s_driver.py           # I2SDriver (ALSA or pigpio implementation)
│   └── ring_buffer.py          # Thread-safe circular buffer
├── uart/
│   ├── __init__.py
│   └── command_manager.py      # UART communication with ESP32
├── config/
│   ├── __init__.py
│   └── manager.py              # Configuration loader/validator
├── telemetry/
│   ├── __init__.py
│   └── tracker.py              # Metrics collection
├── web/
│   ├── __init__.py
│   ├── app.py                  # Flask web server
│   ├── static/
│   │   ├── css/
│   │   │   └── style.css       # Custom styling
│   │   └── js/
│   │       └── dashboard.js    # Web UI JavaScript
│   └── templates/
│       ├── index.html          # Main dashboard
│       ├── bluetooth.html      # Bluetooth control panel
│       └── logs.html           # Log viewer
├── tests/
│   ├── test_ring_buffer.py
│   ├── test_audio_engine.py
│   ├── test_uart_manager.py
│   └── integration/
│       └── test_i2s_pipeline.py
└── docs/
    ├── PRD.md                  # Product Requirements Document
    ├── FS.md                   # Functional Specification
    └── TODO.md                 # Task tracking
```

---

## Troubleshooting

### Issue: "Permission denied: /dev/serial0"
**Solution:** Add user to `dialout` group:
```bash
sudo usermod -a -G dialout pi
# Log out and log back in
```

### Issue: "ALSA device not found"
**Solution:** Enable I2S peripheral in `/boot/config.txt`:
```bash
sudo nano /boot/config.txt
# Add:
dtparam=i2s=on
# Save and reboot
```

### Issue: "UART timeout - no response from ESP32"
**Check:**
1. ESP32 powered on and running esp_bt_audio_source firmware
2. UART wiring correct (RPi TX → ESP32 RX, RPi RX → ESP32 TX, GND connected)
3. UART not in use by Bluetooth: `dtoverlay=disable-bt` set in `/boot/config.txt`

### Issue: "No audio from Bluetooth speaker"
**Check:**
1. I2S wiring correct (BCLK, WS, DOUT, GND connected)
2. ESP32 configured as I2S slave (not master)
3. Bluetooth speaker paired and connected (check Bluetooth tab in web UI)
4. Volume >0% (adjust in web UI Bluetooth panel)

### Issue: "High CPU usage (>50%)"
**Possible causes:**
- WAV resampling (44.1 kHz → 48 kHz): Pre-convert WAV files to 48 kHz
- Frequency sweep: Normal (SciPy chirp generation is CPU-intensive)
- Memory leak: Check with `top` or `htop`, restart application

---

## Development Timeline

**Target:** Working I2S master in <2 days (~16 hours total)

**Milestones:**
- **M1 (4h):** Basic I2S tone generation (1 kHz sine → Bluetooth speaker)
- **M2 (4h):** UART command interface (send/receive commands to ESP32)
- **M3 (6h):** Flask web UI (dashboard, Bluetooth control, real-time status)
- **M4 (4h):** Advanced audio sources (sweep, WAV playback)
- **M5 (2h):** Stability testing (1-hour continuous run, systemd service)

**Total:** 20 hours budgeted (16 hours core + 4 hours buffer)

---

## Testing

### Unit Tests
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
pytest tests/ -v
```

### Integration Tests (requires ESP32 connected)
```bash
pytest tests/integration/ -v
```

### Performance Tests
```bash
# Monitor CPU/memory for 5 minutes
python tests/performance/monitor_resources.py --duration=300
```

---

## Future Enhancements (Post-MVP)

- Multi-tone generator (play up to 4 simultaneous tones)
- WAV file upload via web UI (drag-and-drop)
- Bluetooth device auto-connect on startup
- Real-time audio waveform/FFT visualization
- THD+N measurement
- Latency measurement (I2S → Bluetooth round-trip)

See [docs/TODO.md](docs/TODO.md) for detailed task breakdown.

---

## License

See top-level repository LICENSE file.

---

## Related Projects

- **esp_bt_audio_source:** ESP32 Bluetooth audio transmitter (I2S slave receiver)
- **esp_i2s_source:** ESP32-S3 I2S master with internet radio (production version)

---

## Contact

For issues or questions, see repository issues page or contact maintainer.
