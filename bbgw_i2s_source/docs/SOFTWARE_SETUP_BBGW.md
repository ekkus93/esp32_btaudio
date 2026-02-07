# Software Setup Guide — BeagleBone Green Wireless I2S Audio Source

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Complete software installation and configuration guide  
**Date:** 2026-02-07

---

## Overview

This document provides comprehensive software setup instructions for the BeagleBone Green Wireless I2S Audio Source project. It covers all software dependencies, Python packages, configuration files, and verification procedures needed for successful operation.

**Prerequisites:**
- Hardware setup complete (see [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md))
- BeagleBone Green Wireless with Debian 11+ installed
- Network connectivity (Wi-Fi or Ethernet)
- SSH access to BBGW

**What This Guide Covers:**
1. System package installation (ALSA, Python, development tools)
2. Python environment setup and dependencies
3. Project files installation
4. Configuration file setup
5. ESP32 firmware installation
6. Software verification and testing

**Related Documentation:**
- [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md) - Hardware configuration
- [MILESTONE1_HARDWARE_SETUP_BBGW.md](MILESTONE1_HARDWARE_SETUP_BBGW.md) - I2S software details
- [MILESTONE2_HARDWARE_SETUP_BBGW.md](MILESTONE2_HARDWARE_SETUP_BBGW.md) - UART software details
- [MILESTONE3_HARDWARE_SETUP_BBGW.md](MILESTONE3_HARDWARE_SETUP_BBGW.md) - Web UI software details

---

## Table of Contents

1. [System Requirements](#system-requirements)
2. [System Package Installation](#system-package-installation)
3. [Python Environment Setup](#python-environment-setup)
4. [Project Installation](#project-installation)
5. [Configuration](#configuration)
6. [ESP32 Firmware Setup](#esp32-firmware-setup)
7. [Software Verification](#software-verification)
8. [Troubleshooting](#troubleshooting)
9. [Success Criteria](#success-criteria)

---

## System Requirements

### Operating System

**Supported:**
- Debian 11 (Bullseye) - Recommended
- Debian 10 (Buster) - Supported
- Ubuntu 20.04+ for BeagleBone - Untested but should work

**Kernel:**
- Linux 5.10+ recommended (for McASP I2S support)
- Verify kernel version:
  ```bash
  uname -r
  # Expected: 5.10.x or later
  ```

### Storage

**Minimum:**
- 1 GB free space for system packages
- 500 MB free space for Python packages and project files
- 200 MB free space for logs and temporary files

**Check available space:**
```bash
df -h /
# Ensure at least 2 GB free
```

### Memory

**RAM:**
- 512 MB total (BBGW hardware limit)
- At least 200 MB free during operation

**Check memory:**
```bash
free -h
```

### Network

**Requirements:**
- Internet connectivity for package installation
- SSH access for remote configuration
- Wi-Fi or Ethernet configured

**Verify connectivity:**
```bash
ping -c 3 google.com
```

---

## System Package Installation

### Step 1: Update Package Lists

```bash
# Update package lists
sudo apt-get update

# Upgrade existing packages (optional but recommended)
sudo apt-get upgrade -y
```

**Note:** Upgrade may take 10-30 minutes depending on how outdated the system is.

### Step 2: Install Core Packages

```bash
# Install essential packages
sudo apt-get install -y \
    build-essential \
    git \
    curl \
    wget \
    vim \
    screen \
    htop
```

**Package Descriptions:**
- `build-essential`: C/C++ compiler and build tools
- `git`: Version control (for cloning repository)
- `curl`, `wget`: Download utilities
- `vim`: Text editor
- `screen`: Terminal multiplexer (for background processes)
- `htop`: System monitor

### Step 3: Install Audio Packages (ALSA)

```bash
# Install ALSA utilities and libraries
sudo apt-get install -y \
    alsa-utils \
    libasound2 \
    libasound2-dev
```

**Package Descriptions:**
- `alsa-utils`: Command-line tools (`aplay`, `arecord`, `speaker-test`)
- `libasound2`: ALSA library
- `libasound2-dev`: ALSA development headers

**Verify ALSA installation:**
```bash
aplay --version
# Expected: aplay version 1.2.x
```

### Step 4: Install Python 3

```bash
# Install Python 3 and pip
sudo apt-get install -y \
    python3 \
    python3-pip \
    python3-dev \
    python3-venv
```

**Verify Python installation:**
```bash
python3 --version
# Expected: Python 3.9.x or later

pip3 --version
# Expected: pip 20.x or later
```

### Step 5: Install Device Tree Compiler

```bash
# Install Device Tree compiler (for overlay compilation)
sudo apt-get install -y device-tree-compiler
```

**Verify installation:**
```bash
dtc --version
# Expected: Version: DTC 1.6.x
```

### Step 6: Install Serial Communication Tools (Optional)

```bash
# Install serial terminal tools
sudo apt-get install -y \
    minicom \
    screen

# Install Python serial library system-wide (alternative to pip)
sudo apt-get install -y python3-serial
```

---

## Python Environment Setup

### Step 1: Upgrade pip

```bash
# Upgrade pip to latest version
python3 -m pip install --upgrade pip
```

### Step 2: Install Python Dependencies

**Option A: Install from requirements.txt (recommended)**

```bash
# Navigate to project directory
cd ~/bbgw_i2s_source

# Install from requirements.txt
pip3 install -r requirements.txt
```

**Option B: Install packages manually**

```bash
# Core dependencies
pip3 install \
    pyalsaaudio==0.9.2 \
    pyserial==3.5 \
    flask==2.3.2 \
    flask-cors==4.0.0 \
    pyyaml==6.0

# Testing dependencies
pip3 install \
    pytest==7.4.0 \
    pytest-timeout==2.1.0 \
    psutil==5.9.5 \
    requests==2.31.0
```

**Package Descriptions:**

**Core Packages:**
- `pyalsaaudio`: Python bindings for ALSA (I2S audio output)
- `pyserial`: Serial communication library (UART to ESP32)
- `flask`: Web framework (web UI and REST API)
- `flask-cors`: Cross-Origin Resource Sharing support
- `pyyaml`: YAML configuration file parser

**Testing Packages:**
- `pytest`: Testing framework
- `pytest-timeout`: Timeout support for long tests
- `psutil`: System resource monitoring
- `requests`: HTTP client (for web UI testing)

### Step 3: Verify Python Packages

```bash
# Verify core packages installed
python3 -c "import alsaaudio; print('pyalsaaudio:', alsaaudio.__version__)"
python3 -c "import serial; print('pyserial:', serial.__version__)"
python3 -c "import flask; print('flask:', flask.__version__)"
python3 -c "import yaml; print('pyyaml:', yaml.__version__)"

# Verify testing packages
python3 -c "import pytest; print('pytest:', pytest.__version__)"
python3 -c "import psutil; print('psutil:', psutil.__version__)"
```

**Expected output:**
```
pyalsaaudio: 0.9.2
pyserial: 3.5
flask: 2.3.2
pyyaml: 6.0
pytest: 7.4.0
psutil: 5.9.5
```

### Step 4: Set Up Virtual Environment (Optional)

**Note:** Virtual environments recommended for isolated Python environments, but not required for BBGW.

```bash
# Create virtual environment
python3 -m venv ~/bbgw_venv

# Activate virtual environment
source ~/bbgw_venv/bin/activate

# Install packages in virtual environment
pip install -r ~/bbgw_i2s_source/requirements.txt

# Deactivate when done
deactivate
```

---

## Project Installation

### Step 1: Clone Repository

```bash
# Clone from GitHub
cd ~
git clone https://github.com/yourusername/esp32_btaudio.git

# Navigate to BBGW project directory
cd esp32_btaudio/bbgw_i2s_source
```

**Alternative: Manual file transfer**

If Git not available or repository private:

```bash
# On laptop, create tarball
tar czf bbgw_i2s_source.tar.gz bbgw_i2s_source/

# Transfer to BBGW via scp
scp bbgw_i2s_source.tar.gz debian@beaglebone.local:~/

# On BBGW, extract
cd ~
tar xzf bbgw_i2s_source.tar.gz
```

### Step 2: Verify Project Structure

```bash
cd ~/bbgw_i2s_source
ls -la
```

**Expected directory structure:**
```
bbgw_i2s_source/
├── audio/              # Audio generation and I2S output
├── uart/               # UART communication with ESP32
├── config/             # Configuration management
├── telemetry/          # System monitoring
├── web/                # Flask web UI
│   ├── static/         # CSS, JS, images
│   └── templates/      # HTML templates
├── tests/              # Unit and integration tests
│   ├── unit/           # Unit tests
│   └── integration/    # Integration tests
├── docs/               # Documentation
├── main.py             # Main application entry point
├── config.yaml         # Configuration file (to be created)
├── requirements.txt    # Python dependencies
├── milestone1_test.py  # Milestone 1 I2S test
├── milestone2_test.py  # Milestone 2 UART test
├── milestone3_web_ui_test.py  # Milestone 3 Web UI test
└── run_integration_tests.py   # Integration test runner
```

### Step 3: Set File Permissions

```bash
# Make test scripts executable
cd ~/bbgw_i2s_source
chmod +x milestone1_test.py
chmod +x milestone2_test.py
chmod +x milestone3_web_ui_test.py
chmod +x run_integration_tests.py

# Verify permissions
ls -l *.py
```

**Expected output:**
```
-rwxr-xr-x ... milestone1_test.py
-rwxr-xr-x ... milestone2_test.py
-rwxr-xr-x ... milestone3_web_ui_test.py
-rwxr-xr-x ... run_integration_tests.py
```

---

## Configuration

### Step 1: Create Configuration File

```bash
cd ~/bbgw_i2s_source

# Copy template to config.yaml
cp config.yaml.template config.yaml

# Edit configuration
nano config.yaml
```

**Sample config.yaml:**

```yaml
# BeagleBone Green Wireless I2S Audio Source Configuration

# I2S Audio Configuration
i2s:
  device: "hw:0,0"              # ALSA device (BBGW-I2S)
  sample_rate: 48000            # Sample rate (Hz)
  channels: 2                   # Stereo
  format: "S16_LE"              # 16-bit signed little-endian
  buffer_size: 4096             # Buffer size (frames)
  period_size: 1024             # Period size (frames)

# UART Configuration
uart:
  device: "/dev/ttyO4"          # UART4 device
  baud_rate: 115200             # Baud rate
  timeout: 1.0                  # Read timeout (seconds)
  write_timeout: 1.0            # Write timeout (seconds)

# Audio Generation
audio:
  default_frequency: 1000       # Default tone frequency (Hz)
  default_amplitude: 0.5        # Default amplitude (0.0 - 1.0)
  max_amplitude: 0.8            # Maximum amplitude (prevent clipping)

# Telemetry Configuration
telemetry:
  update_interval: 1.0          # Status update interval (seconds)
  log_level: "INFO"             # Logging level (DEBUG, INFO, WARNING, ERROR)
  log_file: "bbgw_i2s_source.log"  # Log file path

# Web UI Configuration
web:
  host: "0.0.0.0"               # Listen on all interfaces
  port: 5000                    # Web server port
  debug: false                  # Flask debug mode (false for production)
  sse_update_interval: 0.5      # Server-Sent Events update (seconds)

# Testing Configuration
testing:
  hardware_test_timeout: 30     # Hardware test timeout (seconds)
  integration_test_duration: 300  # Integration test duration (seconds)
```

**Key Configuration Options:**

- **i2s.device**: ALSA device name (use `aplay -l` to verify)
- **i2s.buffer_size**: Larger = less underruns, more latency
- **uart.device**: UART device path (use `ls /dev/ttyO*` to verify)
- **web.host**: Use `0.0.0.0` for LAN access, `127.0.0.1` for localhost only
- **web.port**: Default 5000; change if port conflict

### Step 2: Verify Configuration

```bash
# Test configuration file syntax
python3 -c "
import yaml
with open('config.yaml') as f:
    config = yaml.safe_load(f)
    print('Configuration loaded successfully')
    print('I2S device:', config['i2s']['device'])
    print('UART device:', config['uart']['device'])
"
```

**Expected output:**
```
Configuration loaded successfully
I2S device: hw:0,0
UART device: /dev/ttyO4
```

### Step 3: Configure User Permissions

```bash
# Add user to audio group (for ALSA access)
sudo usermod -a -G audio $USER

# Add user to dialout group (for UART access)
sudo usermod -a -G dialout $USER

# Verify group membership
groups $USER
# Should include: audio dialout
```

**Important:** Log out and back in for group changes to take effect.

```bash
# Log out
exit

# SSH back in
ssh debian@beaglebone.local
```

---

## ESP32 Firmware Setup

### Overview

The ESP32 requires the `esp_bt_audio_source` firmware to receive I2S audio from BBGW and transmit via Bluetooth.

**Firmware Location:** `esp32_btaudio/esp_bt_audio_source/`

**Prerequisites:**
- ESP-IDF installed on development machine (not BBGW)
- ESP32 connected to development machine via USB

### Step 1: Flash ESP32 Firmware (on Development Machine)

**On development laptop/desktop (not BBGW):**

```bash
# Navigate to ESP32 project
cd esp32_btaudio/esp_bt_audio_source

# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Configure project (first time only)
idf.py menuconfig
# Verify I2S and Bluetooth settings

# Build firmware
idf.py build

# Flash to ESP32 (USB connected)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

**Expected output:**
```
I (XXX) main: ESP32 Bluetooth Audio Source
I (XXX) main: Initializing I2S (slave mode)...
I (XXX) main: I2S initialized: BCK=GPIO26, WS=GPIO25, DIN=GPIO22
I (XXX) main: Initializing Bluetooth...
I (XXX) main: Bluetooth initialized
I (XXX) main: Waiting for I2S data...
```

### Step 2: Pair Bluetooth Speaker

1. **Power on Bluetooth speaker** and set to pairing mode

2. **On ESP32 serial monitor:**
   - ESP32 should auto-discover and connect to speaker
   - Look for "Bluetooth connected" message

3. **Verify connection:**
   - Speaker should show connected status (LED indicator)
   - ESP32 should show "A2DP connected" in serial output

### Step 3: Verify ESP32 UART

**On BBGW:**

```bash
# Test UART communication with ESP32
python3 -c "
import serial
ser = serial.Serial('/dev/ttyO4', 115200, timeout=1)
ser.write(b'STATUS\\n')
response = ser.readline()
print('ESP32 response:', response.decode('utf-8').strip())
ser.close()
"
```

**Expected response:**
```
ESP32 response: {"status":"OK","i2s":"idle","bluetooth":"connected"}
```

---

## Software Verification

### Step 1: Verify ALSA I2S

```bash
# List ALSA devices
aplay -l

# Expected output:
# card 0: BBGWI2S [BBGW-I2S], device 0: ...

# Test I2S output
speaker-test -D hw:0,0 -r 48000 -c 2 -t sine -f 1000 -l 1
```

**Expected:** 1 kHz tone heard on Bluetooth speaker (if ESP32 connected and BT speaker paired).

### Step 2: Verify Python Audio Module

```bash
cd ~/bbgw_i2s_source

# Test audio module
python3 -c "
from audio.tone_generator import ToneGenerator
from audio.i2s_output import I2SOutput
import yaml

# Load config
with open('config.yaml') as f:
    config = yaml.safe_load(f)

# Test tone generation
gen = ToneGenerator(config['i2s']['sample_rate'])
samples = gen.generate_tone(1000, 0.5, duration=1.0)
print(f'Generated {len(samples)} samples')

# Test I2S output (requires ALSA device)
i2s = I2SOutput(config['i2s'])
print(f'I2S device: {i2s.device}')
"
```

**Expected output:**
```
Generated 96000 samples
I2S device: hw:0,0
```

### Step 3: Verify Python UART Module

```bash
# Test UART module
python3 -c "
from uart.uart_comm import UARTComm
import yaml

# Load config
with open('config.yaml') as f:
    config = yaml.safe_load(f)

# Test UART connection
uart = UARTComm(config['uart'])
print(f'UART opened: {uart.device}')

# Send STATUS command
response = uart.send_command('STATUS')
print(f'ESP32 response: {response}')

uart.close()
"
```

**Expected output:**
```
UART opened: /dev/ttyO4
ESP32 response: {"status":"OK",...}
```

### Step 4: Verify Flask Web UI

```bash
# Start Flask server
cd ~/bbgw_i2s_source
python3 main.py
```

**Expected output:**
```
 * Serving Flask app 'main'
 * Debug mode: off
 * Running on http://0.0.0.0:5000
```

**Test from another terminal:**

```bash
# Test REST API
curl http://localhost:5000/api/status

# Expected: JSON with system status
```

**Test from laptop browser:**
```
http://beaglebone.local:5000
# or
http://<bbgw-ip-address>:5000
```

### Step 5: Run Milestone Tests

**Milestone 1: I2S Tone Generation**

```bash
cd ~/bbgw_i2s_source
./milestone1_test.py
```

**Expected:** All tests pass, 1 kHz tone audible on Bluetooth speaker.

**Milestone 2: UART Command Interface**

```bash
./milestone2_test.py
```

**Expected:** All tests pass, UART commands and responses working.

**Milestone 3: Flask Web UI**

```bash
# Start Flask server in background
python3 main.py &

# Run Milestone 3 test from laptop
./milestone3_web_ui_test.py --host beaglebone.local
```

**Expected:** All 5 tests pass (server connectivity, pages, API, latency, SSE).

---

## Troubleshooting

### Python Package Issues

#### Problem: pip install fails with "externally-managed-environment"

**Symptoms:**
```
error: externally-managed-environment
```

**Solutions:**

1. **Use --break-system-packages flag (quick fix):**
   ```bash
   pip3 install --break-system-packages -r requirements.txt
   ```

2. **Use virtual environment (recommended):**
   ```bash
   python3 -m venv ~/bbgw_venv
   source ~/bbgw_venv/bin/activate
   pip install -r requirements.txt
   ```

3. **Install system packages:**
   ```bash
   sudo apt-get install python3-alsaaudio python3-serial python3-flask
   ```

#### Problem: pyalsaaudio import fails

**Symptoms:**
```python
ImportError: No module named 'alsaaudio'
```

**Solutions:**

1. **Install via pip:**
   ```bash
   pip3 install pyalsaaudio
   ```

2. **Install system package:**
   ```bash
   sudo apt-get install python3-alsaaudio
   ```

3. **Verify libasound2-dev installed:**
   ```bash
   sudo apt-get install libasound2-dev
   pip3 install --no-cache-dir pyalsaaudio
   ```

### Configuration Issues

#### Problem: config.yaml not found

**Symptoms:**
```
FileNotFoundError: [Errno 2] No such file or directory: 'config.yaml'
```

**Solutions:**

1. **Create from template:**
   ```bash
   cp config.yaml.template config.yaml
   ```

2. **Verify working directory:**
   ```bash
   cd ~/bbgw_i2s_source
   ls config.yaml
   ```

#### Problem: ALSA device "hw:0,0" not found

**Symptoms:**
```
alsaaudio.ALSAAudioError: No such file or directory [hw:0,0]
```

**Solutions:**

1. **List ALSA devices:**
   ```bash
   aplay -l
   ```

2. **Update config.yaml with correct device:**
   ```yaml
   i2s:
     device: "hw:0,0"  # or "BBGW-I2S" or whatever aplay -l shows
   ```

### Permission Issues

#### Problem: Permission denied on /dev/ttyO4

**Solutions:**

1. **Add user to dialout group:**
   ```bash
   sudo usermod -a -G dialout $USER
   # Log out and back in
   ```

2. **Temporary fix:**
   ```bash
   sudo chmod 666 /dev/ttyO4
   ```

#### Problem: Permission denied for ALSA device

**Solutions:**

1. **Add user to audio group:**
   ```bash
   sudo usermod -a -G audio $USER
   # Log out and back in
   ```

### Flask Web UI Issues

#### Problem: Address already in use (port 5000)

**Symptoms:**
```
OSError: [Errno 98] Address already in use
```

**Solutions:**

1. **Kill existing Flask process:**
   ```bash
   pkill -f "python3 main.py"
   ```

2. **Find and kill process on port 5000:**
   ```bash
   sudo lsof -ti:5000 | xargs kill -9
   ```

3. **Change port in config.yaml:**
   ```yaml
   web:
     port: 8000  # Use different port
   ```

#### Problem: Cannot access web UI from laptop

**Solutions:**

1. **Verify Flask listening on 0.0.0.0:**
   ```bash
   grep "host" config.yaml
   # Should be: host: "0.0.0.0"
   ```

2. **Check firewall:**
   ```bash
   sudo ufw status
   sudo ufw allow 5000/tcp
   ```

3. **Verify BBGW IP address:**
   ```bash
   ip addr show wlan0
   # Use this IP from laptop browser
   ```

---

## Success Criteria

Software setup is complete when all of the following criteria are met:

### System Packages

- [ ] All system packages installed without errors
- [ ] `aplay --version` shows ALSA version 1.2.x+
- [ ] `python3 --version` shows Python 3.9.x+
- [ ] `dtc --version` shows DTC 1.6.x+

### Python Packages

- [ ] All Python packages installed (`pip3 list | grep <package>`)
- [ ] pyalsaaudio, pyserial, flask, pyyaml, pytest all import successfully
- [ ] No import errors when running test scripts

### Project Files

- [ ] Project cloned or transferred to `~/bbgw_i2s_source`
- [ ] Directory structure complete (audio/, uart/, config/, web/, tests/)
- [ ] Test scripts executable (milestone1_test.py, etc.)
- [ ] config.yaml created and valid

### Configuration

- [ ] config.yaml syntax valid (Python YAML parser succeeds)
- [ ] I2S device matches `aplay -l` output
- [ ] UART device exists (`/dev/ttyO4`)
- [ ] User in audio and dialout groups

### ESP32 Firmware

- [ ] ESP32 firmware flashed successfully
- [ ] ESP32 serial output shows "I2S initialized"
- [ ] Bluetooth speaker paired and connected
- [ ] UART communication working (STATUS command responds)

### Software Verification

- [ ] `aplay` can output to I2S device
- [ ] Python audio module imports and initializes
- [ ] Python UART module connects and sends commands
- [ ] Flask web UI starts without errors
- [ ] REST API responds to curl requests
- [ ] Milestone 1 test passes (I2S tone)
- [ ] Milestone 2 test passes (UART)
- [ ] Milestone 3 test passes (Web UI)

---

## Next Steps

After completing software setup:

1. **Testing:**
   - Run all milestone tests (1, 2, 3)
   - Run unit tests: `pytest tests/unit/`
   - Run integration tests: `./run_integration_tests.py --suite quick`

2. **Optimization:**
   - Tune I2S buffer sizes in config.yaml
   - Adjust log levels for production
   - Configure CPU governor for performance

3. **Deployment:**
   - Set up systemd service for automatic startup
   - Configure static IP or DHCP reservation
   - Set up log rotation
   - Create backup of working configuration

4. **Monitoring:**
   - Monitor system logs: `tail -f bbgw_i2s_source.log`
   - Monitor system resources: `htop`
   - Check for buffer underruns in logs

---

## Additional Resources

### Python Documentation
- [pyalsaaudio Documentation](https://larsimmisch.github.io/pyalsaaudio/)
- [pyserial Documentation](https://pythonhosted.org/pyserial/)
- [Flask Documentation](https://flask.palletsprojects.com/)

### ALSA Documentation
- [ALSA Project](https://www.alsa-project.org/)
- [ALSA Configuration](https://www.alsa-project.org/wiki/Asoundrc)

### Project Documentation
- [Hardware Setup Guide](HARDWARE_SETUP_BBGW.md)
- [Integration Testing](INTEGRATION_TEST_SETUP_BBGW.md)
- [Architecture Document](../ARCH.md)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
