# BeagleBone Green Wireless I2S Source

**I2S audio test jig for esp_bt_audio_source — BeagleBone Green Wireless port**

A Python-based I2S master transmitter running on BeagleBone Green Wireless to accelerate testing of the ESP32 Bluetooth audio sink. Provides tone generation, frequency sweeps, WAV playback, and UART control—all through a web UI.

**Port of:** `rpi_i2s_source` (Raspberry Pi version)

---

## Why BeagleBone Green Wireless?

**Built-in Wi-Fi:** No dongle needed for wireless development
- **802.11 b/g/n** Wi-Fi onboard
- SSH over Wi-Fi for headless operation
- Web UI accessible via Wi-Fi

**Powerful Audio:** AM335x McASP (Multichannel Audio Serial Port)
- **Hardware I2S engine** with DMA support
- Multiple channels and sample rates
- Lower CPU overhead vs bit-banged GPIO

**Real-Time Capabilities:** PRU (Programmable Real-time Units)
- Ultra-low latency potential (future enhancement)
- Deterministic timing for audio critical paths

**Strategy:** Use BBGW for all esp_bt_audio_source testing while developing ESP32-S3 production features in parallel.

---

## Hardware Requirements

### BeagleBone Green Wireless
- **Model:** BeagleBone Green Wireless (AM335x ARM Cortex-A8, 512MB RAM)
- **OS:** Debian Linux (official BeagleBone images)
- **Kernel:** 4.19+ (for Device Tree overlay support)
- **Power:** 5V DC via barrel jack or USB

### ESP32 Target
- **esp_bt_audio_source** running on ESP32/ESP32-S3
- Must be configured as I2S slave receiver (BCLK input, WS input, DIN input)

### Wiring (BBGW ↔ ESP32)

**I2S Interface (McASP):**
```
BBGW P9 Header           ESP32
P9.31 (McASP0_ACLKX) ──► GPIO26 (BCLK)
P9.29 (McASP0_FSX)   ──► GPIO25 (WS)
P9.28 (McASP0_AXR0)  ──► GPIO22 (DIN)
P9.1  (DGND)         ──► GND
```

**UART Interface (UART4):**
```
BBGW P9 Header           ESP32
P9.13 (UART4_TXD)    ──► GPIO17 (RX)
P9.11 (UART4_RXD)    ◄── GPIO16 (TX)
P9.1  (DGND)         ──► GND
```

**Notes:**
- Keep I2S wires <30 cm (minimize noise and reflections)
- Use 3.3V logic levels (BBGW GPIO is 3.3V, ESP32 GPIO is 3.3V)
- No level shifters needed
- **Pin assignments subject to Device Tree configuration** (see docs/BBGW_PIN_REFERENCE.md)

---

## Software Dependencies

### System Packages (Debian Linux)
```bash
sudo apt update
sudo apt install -y python3-pip python3-venv
sudo apt install -y alsa-utils               # ALSA for McASP
sudo apt install -y device-tree-compiler     # For Device Tree overlays
sudo apt install -y build-essential          # For compiling (optional)
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

Run the automated setup script on your BeagleBone Green Wireless:

```bash
cd /home/debian
git clone https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/bbgw_i2s_source
bash setup_bbgw.sh
```

The script will:
- ✅ Update system packages
- ✅ Install Python 3, pip, venv, and build tools
- ✅ Install ALSA and device tree compiler
- ✅ Configure McASP Device Tree overlay for I2S
- ✅ Configure UART4 Device Tree overlay
- ✅ Create Python virtual environment and install dependencies
- ✅ Create audio directory (`/home/debian/audio`)
- ✅ Create `config.yaml` from template
- ✅ Add user to `dialout` group for UART access
- ✅ Reboot to apply Device Tree changes

**First time setup:** Reboot required after running setup script.

### Manual Setup

For detailed manual setup instructions, see:
- **[docs/HARDWARE_SETUP_BBGW.md](docs/HARDWARE_SETUP_BBGW.md)** — Complete hardware configuration guide
- **[docs/SOFTWARE_SETUP_BBGW.md](docs/SOFTWARE_SETUP_BBGW.md)** — Complete software installation guide

Or see milestone-specific guides:
- [docs/MILESTONE1_HARDWARE_SETUP_BBGW.md](docs/MILESTONE1_HARDWARE_SETUP_BBGW.md) — I2S/McASP setup
- [docs/MILESTONE2_HARDWARE_SETUP_BBGW.md](docs/MILESTONE2_HARDWARE_SETUP_BBGW.md) — UART4 setup
- [docs/MILESTONE3_HARDWARE_SETUP_BBGW.md](docs/MILESTONE3_HARDWARE_SETUP_BBGW.md) — Web UI/Wi-Fi setup

---

## Configuration

### config.yaml

The main configuration file is `config.yaml`. Copy from template:

```bash
cp config.yaml.template config.yaml
nano config.yaml
```

**Key BBGW-specific settings:**

```yaml
i2s:
  device: "hw:0,0"        # ALSA device (verify with aplay -l)
  sample_rate: 48000      # 48 kHz (supported by McASP)
  channels: 2             # Stereo
  format: "S16_LE"        # 16-bit signed little-endian PCM

uart:
  device: "/dev/ttyO4"    # UART4 on BeagleBone
  baudrate: 115200
  timeout: 5.0

web:
  bind_address: "0.0.0.0" # Listen on all interfaces (Wi-Fi + Ethernet)
  port: 5000
  debug: false
```

**Important:** 
- I2S device name (`hw:0,0`) may vary—verify with `aplay -l` after Device Tree setup
- UART device is `/dev/ttyO4` (UART4) on BeagleBone (vs `/dev/serial0` on RPi)

---

## Usage

### Start the Application

Activate the virtual environment and run:

```bash
cd /home/debian/esp32_btaudio/bbgw_i2s_source
source venv/bin/activate
python3 main.py
```

**Expected output:**
```
2026-02-07 12:00:00 - INFO - ConfigManager initialized
2026-02-07 12:00:00 - INFO - AudioEngine initialized
2026-02-07 12:00:00 - INFO - I2SDriverALSA initialized (device=hw:0,0)
2026-02-07 12:00:00 - INFO - Started AudioEngine
2026-02-07 12:00:00 - INFO - Started I2SDriver
2026-02-07 12:00:00 - INFO - Started UARTCommandManager
2026-02-07 12:00:00 - INFO - Starting Flask server on 0.0.0.0:5000
 * Running on http://192.168.8.1:5000 (BBGW Wi-Fi IP)
```

### Access the Web UI

From any device on the same network, open a web browser:

```
http://<bbgw-ip>:5000
```

**Find your BBGW IP address:**
```bash
hostname -I
```

**Default BBGW Wi-Fi IP:** Usually `192.168.8.1` (USB gadget mode) or assigned by DHCP.

### Web UI Features

- **Dashboard:** Tone generator controls (frequency, amplitude, stereo mode)
- **Audio Sources:** Tone, Frequency Sweep, WAV File, Silence
- **Bluetooth Control:** UART commands (SCAN, CONNECT, DISCONNECT, VOLUME)
- **Real-time Monitoring:** I2S status, buffer health, UART status (via SSE)

---

## Features

### Audio Generation
- **Tone Generator:** 20 Hz - 20 kHz sine waves, adjustable amplitude
- **Frequency Sweep:** Logarithmic chirp (20 Hz → 20 kHz over 10s)
- **WAV Playback:** 44.1 kHz WAV files auto-resampled to 48 kHz
- **Dual-Tone Mode:** Left/right channel ID (1 kHz left, 440 Hz right)
- **Silence Mode:** Digital silence (useful for power measurements)

### I2S Output (via McASP)
- **Sample Rate:** 48 kHz (configurable)
- **Bit Depth:** 16-bit signed PCM
- **Format:** I2S standard (BCLK, WS, DOUT)
- **ALSA Backend:** Native Linux ALSA with McASP driver

### UART Control
- **Protocol:** Simple text-based commands (`COMMAND args\n`)
- **Commands:** STATUS, VOLUME, SCAN, CONNECT, DISCONNECT, START, STOP
- **Response Format:** `OK|COMMAND|result` or `ERR|COMMAND|message`
- **Event Notifications:** Async events (BT_CONNECTED, BT_DISCONNECTED)

### Web Interface
- **RESTful API:** `/api/status`, `/api/tone`, `/api/sweep`, `/api/wav`, etc.
- **Server-Sent Events:** Real-time status updates (2 Hz)
- **Responsive UI:** Works on desktop, tablet, mobile

---

## Testing

### Run Unit Tests

```bash
source venv/bin/activate
pytest -v
```

**Expected:** 232+ tests passing

### Hardware Validation Tests

**Milestone 1: I2S Tone Generation**
```bash
./milestone1_tone_test.py --duration 300
```
Validates 5-minute continuous 1 kHz tone generation via McASP.

**Milestone 2: UART Command Interface**
```bash
./milestone2_uart_test.py --device /dev/ttyO4
```
Tests UART commands (STATUS, VOLUME, timeout handling, events).

**Milestone 3: Web UI**
```bash
./milestone3_web_ui_test.py --host <bbgw-ip>
```
Tests web UI accessibility, API endpoints, tone latency, SSE stream.

---

## Documentation

### Quick Start Guides ⚡
- **[docs/HARDWARE_SETUP_BBGW.md](docs/HARDWARE_SETUP_BBGW.md)** — Complete hardware configuration (Device Tree, wiring, verification)
- **[docs/SOFTWARE_SETUP_BBGW.md](docs/SOFTWARE_SETUP_BBGW.md)** — Complete software installation (packages, Python, configuration)
- **[docs/INTEGRATION_TESTING_GUIDE.md](docs/INTEGRATION_TESTING_GUIDE.md)** — End-to-end testing procedures

### BeagleBone-Specific Technical Guides 🔧
- **[docs/BBGW_DEVICE_TREE_GUIDE.md](docs/BBGW_DEVICE_TREE_GUIDE.md)** — Device Tree overlays (I2S/UART configuration)
- **[docs/BBGW_PIN_REFERENCE.md](docs/BBGW_PIN_REFERENCE.md)** — P9 header pinout and GPIO numbering
- **[docs/BBGW_vs_RPI_COMPARISON.md](docs/BBGW_vs_RPI_COMPARISON.md)** — Platform comparison and migration guide

### Milestone-Specific Guides 📚
- [docs/MILESTONE1_HARDWARE_SETUP_BBGW.md](docs/MILESTONE1_HARDWARE_SETUP_BBGW.md) — I2S/McASP setup
- [docs/MILESTONE2_HARDWARE_SETUP_BBGW.md](docs/MILESTONE2_HARDWARE_SETUP_BBGW.md) — UART4 setup
- [docs/MILESTONE3_HARDWARE_SETUP_BBGW.md](docs/MILESTONE3_HARDWARE_SETUP_BBGW.md) — Web UI/Wi-Fi setup

### Additional Documentation
- [docs/TODO.md](docs/TODO.md) — Port task list and progress tracking
- **[docs/TROUBLESHOOTING_BBGW.md](docs/TROUBLESHOOTING_BBGW.md)** — Common issues and solutions
- **[docs/PERFORMANCE_OPTIMIZATION.md](docs/PERFORMANCE_OPTIMIZATION.md)** — Performance tuning guide

---

## Troubleshooting

### No I2S Audio Output

**Symptoms:** No BCLK/WS signals, ESP32 receives no data

**Check:**
1. Verify Device Tree overlay loaded: `dmesg | grep -i mcasp`
2. Verify ALSA device exists: `aplay -l`
3. Check McASP configuration: `cat /boot/uEnv.txt | grep mcasp`
4. Test ALSA playback: `aplay -D hw:0,0 /usr/share/sounds/alsa/Front_Center.wav`

**Fix:** See [docs/TROUBLESHOOTING_BBGW.md](docs/TROUBLESHOOTING_BBGW.md) or [docs/HARDWARE_SETUP_BBGW.md](docs/HARDWARE_SETUP_BBGW.md) Section 5 for detailed solutions.

### UART Not Working

**Symptoms:** `/dev/ttyO4` not found, permission denied

**Check:**
1. Verify UART4 enabled: `ls -l /dev/ttyO4`
2. Check permissions: `ls -l /dev/ttyO4`
3. Verify Device Tree: `cat /boot/uEnv.txt | grep uart`
4. Check user group: `groups` (should include `dialout`)

**Fix:**
```bash
# Enable UART4 overlay
sudo nano /boot/uEnv.txt
# Add: cape_enable=bone_capemgr.enable_partno=BB-UART4
sudo reboot

# Add user to dialout group
sudo usermod -a -G dialout debian
# Log out and back in
```

### Wi-Fi Not Connecting

**Check:**
```bash
# Verify Wi-Fi interface
ip link show wlan0

# Scan for networks
sudo iw wlan0 scan | grep SSID

# Configure Wi-Fi
sudo connmanctl
connmanctl> agent on
connmanctl> connect wifi_*_managed_psk
```

---

## Differences from Raspberry Pi Version

### Hardware
- **I2S:** McASP (hardware engine) vs GPIO bit-banging or ALSA
- **Pins:** P8/P9 headers vs BCM GPIO numbering
- **UART:** `/dev/ttyO4` vs `/dev/serial0`
- **Wi-Fi:** Built-in vs USB dongle

### Software
- **Device Tree:** Overlays required for McASP and UART
- **ALSA Device:** McASP-specific vs generic I2S
- **Pin Configuration:** `/boot/uEnv.txt` vs `/boot/config.txt`

### Code Changes (Minimal)
- `config.yaml`: Updated device names
- `i2s/driver_alsa.py`: BBGW ALSA device name
- `uart/command_manager.py`: `/dev/ttyO4` default
- Documentation: BBGW-specific guides

**Everything else:** Identical to RPi version (audio engine, web UI, UART protocol)

---

## Performance

**Typical Performance (BeagleBone Green Wireless):**
- **CPU Usage:** 15-25% (single core at 1 GHz)
- **Memory:** ~150 MB RAM (Python, Flask, audio buffers)
- **I2S Underruns:** <5 per hour (with 4096 frame buffer)
- **Web UI Latency:** 10-30 ms (tone control response time)
- **UART Command Latency:** <50 ms (round trip)

**Comparison to Raspberry Pi 4:**
- **Similar:** Audio quality, feature set, web UI
- **Slower:** Single-core ARM Cortex-A8 (1 GHz) vs quad-core Cortex-A72 (1.8 GHz)
- **Advantage:** Built-in Wi-Fi, hardware McASP engine

---

## License

Same as parent project (esp32_btaudio).

---

## Credits

**Port:** BeagleBone Green Wireless adaptation of `rpi_i2s_source`  
**Original:** Raspberry Pi I2S Source for esp_bt_audio_source testing  
**Project:** https://github.com/ekkus93/esp32_btaudio

---

## Support

For BBGW-specific issues:
- Check [docs/TROUBLESHOOTING_BBGW.md](docs/TROUBLESHOOTING_BBGW.md)
- Review [docs/TODO.md](docs/TODO.md) for port status
- Open GitHub issue with "BBGW" tag

For general issues:
- See parent project documentation
- Check RPi version guides (most concepts apply)
