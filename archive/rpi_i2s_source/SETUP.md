# Raspberry Pi Setup Guide

Comprehensive guide for setting up a Raspberry Pi as an I2S audio test jig for the esp_bt_audio_source project.

---

## Table of Contents

1. [Hardware Requirements](#hardware-requirements)
2. [OS Installation](#os-installation)
3. [Network Configuration](#network-configuration)
4. [UART Configuration](#uart-configuration)
5. [I2S Configuration](#i2s-configuration)
6. [Software Installation](#software-installation)
7. [Systemd Service Setup](#systemd-service-setup)
8. [Verification](#verification)
9. [Troubleshooting](#troubleshooting)

---

## Hardware Requirements

### Raspberry Pi

**Recommended Models:**
- Raspberry Pi 4 Model B (2GB RAM or higher)
- Raspberry Pi 400 (keyboard form factor)
- Raspberry Pi 5 (latest model)

**Supported Models:**
- Raspberry Pi 3 B+ (minimum, may have performance limitations)
- Raspberry Pi Zero 2 W (compact, WiFi-only)

**Not Recommended:**
- Raspberry Pi Zero / Zero W (single-core, insufficient performance)
- Raspberry Pi 1 / 2 (outdated, no GPIO I2S support)

### Power Supply
- Official Raspberry Pi Power Supply (5V 3A for RPi 4/5)
- USB-C (RPi 4/5) or micro-USB (RPi 3)

### MicroSD Card
- **Minimum:** 8 GB Class 10
- **Recommended:** 16 GB or 32 GB UHS-I (faster boot/read speeds)
- **Brands:** SanDisk, Samsung, Kingston (avoid counterfeit cards)

### Accessories
- HDMI cable + monitor (for initial setup; can be headless after)
- USB keyboard (for initial setup)
- Ethernet cable (optional, for wired network during setup)

### ESP32 Target Device
- ESP32 or ESP32-S3 running **esp_bt_audio_source** firmware
- I2S slave configuration (receives BCLK, WS, DIN from Raspberry Pi)
- UART configured (GPIO16 TX, GPIO17 RX, 115200 baud)

### Wiring
- Breadboard or custom PCB for connections
- Jumper wires (Dupont wires, male-to-male or male-to-female)
- **Keep I2S wires <30 cm** to minimize noise

---

## OS Installation

### Step 1: Download Raspberry Pi Imager

Download the official Raspberry Pi Imager:
- **Website:** https://www.raspberrypi.com/software/
- **Platforms:** Windows, macOS, Linux

### Step 2: Write OS to MicroSD Card

1. **Launch Raspberry Pi Imager**
2. **Choose Device:** Select your Raspberry Pi model (e.g., "Raspberry Pi 4")
3. **Choose OS:**
   - **Recommended:** "Raspberry Pi OS (64-bit)" — Debian Bookworm
   - **Alternative:** "Raspberry Pi OS Lite (64-bit)" — headless (no desktop)
4. **Choose Storage:** Select your microSD card
5. **Click "Next"**

### Step 3: OS Customization (IMPORTANT)

**Before writing,** click **"Edit Settings"** to configure:

#### General Tab:
- ✅ Set hostname: `rpi-i2s-source` (or your preference)
- ✅ Set username: `pi` (default, or custom)
- ✅ Set password: (choose a strong password)
- ✅ Configure WiFi:
  - SSID: Your WiFi network name
  - Password: Your WiFi password
  - WiFi country: Your country code (e.g., `US`, `GB`, `AU`)
- ✅ Set locale:
  - Timezone: Your timezone (e.g., `America/Los_Angeles`)
  - Keyboard layout: Your layout (e.g., `us`)

#### Services Tab:
- ✅ Enable SSH (password authentication or SSH key)

**Save settings and proceed with write.**

### Step 4: Write Image

- Click **"Yes"** to confirm (this will erase the microSD card)
- Wait for write + verification (~5-10 minutes depending on card speed)
- Eject microSD card safely

### Step 5: First Boot

1. Insert microSD card into Raspberry Pi
2. Connect power supply
3. Wait for boot (~30-60 seconds first boot)
4. Raspberry Pi should auto-connect to WiFi

---

## Network Configuration

### Find Raspberry Pi IP Address

**Option 1: Router Admin Page**
- Log into your router admin interface (e.g., `http://192.168.1.1`)
- Look for connected devices, find `rpi-i2s-source` or MAC address starting with `B8:27:EB` / `DC:A6:32` / `E4:5F:01`

**Option 2: Network Scan**
```bash
# On your laptop (Linux/macOS)
nmap -sn 192.168.1.0/24 | grep -B2 "Raspberry"

# Or use Angry IP Scanner (Windows/macOS/Linux GUI)
```

**Option 3: Connect Monitor + Keyboard**
- Connect HDMI monitor and USB keyboard
- Log in and run:
```bash
hostname -I
```

### SSH into Raspberry Pi

Once you have the IP address:
```bash
ssh pi@192.168.1.100  # Replace with your RPi IP
# Enter password when prompted
```

### Static IP (Optional but Recommended)

For consistent access, set a static IP:

**Option A: Router DHCP Reservation**
- Most reliable method
- Configure DHCP reservation in router settings (bind IP to MAC address)

**Option B: Configure Static IP on Raspberry Pi**

Edit `dhcpcd.conf`:
```bash
sudo nano /etc/dhcpcd.conf
```

Add at the end:
```conf
# Static IP configuration for eth0 (wired) or wlan0 (WiFi)
interface wlan0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=192.168.1.1 8.8.8.8
```

Save (`Ctrl+O`, Enter, `Ctrl+X`) and reboot:
```bash
sudo reboot
```

---

## UART Configuration

The Raspberry Pi's primary UART (`/dev/ttyAMA0`) is required for communication with the ESP32. By default, this UART may be used by Bluetooth or the Linux console.

### Step 1: Disable Bluetooth on UART

**Why:** Raspberry Pi 3/4/5 use the hardware UART for Bluetooth by default. We need this UART for ESP32 communication.

Edit `/boot/config.txt`:
```bash
sudo nano /boot/config.txt
```

Add this line at the end:
```conf
# Disable Bluetooth to free up primary UART
dtoverlay=disable-bt
```

Save and exit.

### Step 2: Disable Serial Console (if enabled)

Check if serial console is enabled:
```bash
cat /boot/cmdline.txt | grep console=serial
```

If you see `console=serial0,115200`, you need to remove it:
```bash
sudo nano /boot/cmdline.txt
```

Remove `console=serial0,115200` from the line (leave everything else intact).

**Example Before:**
```
console=serial0,115200 console=tty1 root=PARTUUID=xxx ...
```

**Example After:**
```
console=tty1 root=PARTUUID=xxx ...
```

Save and exit.

### Step 3: Enable UART

Ensure UART is enabled:
```bash
sudo raspi-config
```

Navigate:
- **3 Interface Options**
- **I6 Serial Port**
- "Would you like a login shell over serial?" → **No**
- "Would you like the serial port hardware to be enabled?" → **Yes**
- Exit and reboot

### Step 4: Reboot and Verify

```bash
sudo reboot
```

After reboot, verify UART device:
```bash
ls -l /dev/serial0
# Expected output:
# lrwxrwxrwx 1 root root 7 ... /dev/serial0 -> ttyAMA0

ls -l /dev/ttyAMA0
# Expected output:
# crw-rw---- 1 root dialout 204, 64 ... /dev/ttyAMA0
```

### Step 5: Add User to dialout Group

To access UART without root privileges:
```bash
sudo usermod -a -G dialout pi
```

**Log out and log back in** for group membership to take effect:
```bash
exit
# SSH back in
ssh pi@192.168.1.100
```

Verify group membership:
```bash
groups
# Should include: pi dialout ...
```

---

## I2S Configuration

### Step 1: Enable I2S Peripheral

The Raspberry Pi's I2S interface must be enabled in the device tree.

Edit `/boot/config.txt`:
```bash
sudo nano /boot/config.txt
```

Add this line:
```conf
# Enable I2S peripheral
dtparam=i2s=on
```

Save and exit.

### Step 2: Optional - I2S Memory-Mapped Mode

For better performance (lower latency, fewer underruns), use `i2s-mmap` overlay:
```conf
# Use I2S memory-mapped driver (recommended for audio applications)
dtoverlay=i2s-mmap
```

**Note:** Use **either** `dtparam=i2s=on` **OR** `dtoverlay=i2s-mmap`, not both. The `i2s-mmap` overlay is preferred for audio applications.

### Step 3: Reboot

```bash
sudo reboot
```

### Step 4: Verify I2S Device

After reboot, check for I2S ALSA device:
```bash
aplay -l
```

Expected output (look for I2S device):
```
**** List of PLAYBACK Hardware Devices ****
card 0: vc4hdmi [vc4-hdmi], device 0: MAI PCM i2s-hifi-0 [MAI PCM i2s-hifi-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: sndrpii2s [snd_rpi_i2s], device 0: simple-card_codec_link snd-soc-dummy-dai-0 [simple-card_codec_link snd-soc-dummy-dai-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

The `snd_rpi_i2s` card is the I2S interface we'll use.

---

## Software Installation

### Step 1: Update System Packages

```bash
sudo apt update
sudo apt upgrade -y
```

### Step 2: Install System Dependencies

```bash
sudo apt install -y python3-pip python3-venv python3-dev
sudo apt install -y alsa-utils  # For ALSA I2S driver
sudo apt install -y git
```

### Step 3: Clone Repository

```bash
cd /home/pi
git clone https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/rpi_i2s_source
```

### Step 4: Create Python Virtual Environment

```bash
python3 -m venv venv
source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
```

**This will install:**
- Flask 3.0 (web server)
- pyserial 3.5 (UART communication)
- NumPy 1.24 (audio generation)
- SciPy 1.11 (WAV processing, chirp generation)
- PyYAML 6.0 (configuration)
- psutil (resource monitoring)
- pytest, pytest-mock (testing)

### Step 5: Create Configuration File

```bash
cp config.yaml.template config.yaml
nano config.yaml  # Customize if needed
```

**Review key settings:**
```yaml
audio:
  sample_rate: 48000
  channels: 2
  buffer_size: 8192

i2s:
  driver: "alsa"  # or "pigpio"
  device: "hw:1,0"  # ALSA device (check with aplay -l)

uart:
  device: "/dev/serial0"
  baudrate: 115200

web:
  host: "0.0.0.0"
  port: 5000
```

### Step 6: Create Audio Directory

```bash
mkdir -p /home/pi/audio
```

**Optional:** Copy test WAV files:
```bash
# Example: Download a test tone
wget https://www2.cs.uic.edu/~i101/SoundFiles/BabyElephantWalk60.wav -O /home/pi/audio/test.wav
```

---

## Systemd Service Setup

To automatically start the I2S source on boot, create a systemd service.

### Step 1: Create Service File

```bash
sudo nano /etc/systemd/system/rpi-i2s-source.service
```

**Content:**
```ini
[Unit]
Description=Raspberry Pi I2S Audio Test Jig
After=network.target
Wants=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/esp32_btaudio/rpi_i2s_source
ExecStart=/home/pi/esp32_btaudio/rpi_i2s_source/venv/bin/python main.py
Restart=on-failure
RestartSec=10

# Environment
Environment="PYTHONUNBUFFERED=1"

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=rpi-i2s-source

[Install]
WantedBy=multi-user.target
```

Save and exit.

### Step 2: Enable Service

```bash
sudo systemctl daemon-reload
sudo systemctl enable rpi-i2s-source
```

### Step 3: Start Service

```bash
sudo systemctl start rpi-i2s-source
```

### Step 4: Check Service Status

```bash
sudo systemctl status rpi-i2s-source
```

Expected output:
```
● rpi-i2s-source.service - Raspberry Pi I2S Audio Test Jig
     Loaded: loaded (/etc/systemd/system/rpi-i2s-source.service; enabled; vendor preset: enabled)
     Active: active (running) since ...
   Main PID: 1234 (python)
      Tasks: 5 (limit: 4164)
     Memory: 45.2M
        CPU: 1.234s
     CGroup: /system.slice/rpi-i2s-source.service
             └─1234 /home/pi/esp32_btaudio/rpi_i2s_source/venv/bin/python main.py
```

### Step 5: View Logs

```bash
# Real-time log tail
sudo journalctl -u rpi-i2s-source -f

# Last 50 lines
sudo journalctl -u rpi-i2s-source -n 50

# Since last boot
sudo journalctl -u rpi-i2s-source -b
```

### Step 6: Test Auto-Start

```bash
sudo reboot
```

After reboot, verify service started automatically:
```bash
sudo systemctl status rpi-i2s-source
```

Access web UI:
```
http://192.168.1.100:5000
```

### Service Management Commands

```bash
# Stop service
sudo systemctl stop rpi-i2s-source

# Restart service
sudo systemctl restart rpi-i2s-source

# Disable auto-start
sudo systemctl disable rpi-i2s-source

# Re-enable auto-start
sudo systemctl enable rpi-i2s-source
```

---

## Verification

### Hardware Verification Checklist

**I2S Wiring:**
- [ ] RPi GPIO18 (BCLK) → ESP32 BCLK (GPIO26)
- [ ] RPi GPIO19 (WS) → ESP32 WS (GPIO25)
- [ ] RPi GPIO21 (DOUT) → ESP32 DIN (GPIO22)
- [ ] RPi GND → ESP32 GND
- [ ] Wire length <30 cm (minimize noise)

**UART Wiring:**
- [ ] RPi GPIO14 (TXD) → ESP32 RX (GPIO17)
- [ ] RPi GPIO15 (RXD) → ESP32 TX (GPIO16)
- [ ] RPi GND → ESP32 GND (may share with I2S GND)

**Power:**
- [ ] Raspberry Pi powered with official power supply (5V 3A)
- [ ] ESP32 powered via USB or external 3.3V/5V supply

### Software Verification

**1. Check UART Device:**
```bash
ls -l /dev/serial0
# Expected: lrwxrwxrwx ... /dev/serial0 -> ttyAMA0
```

**2. Check I2S Device:**
```bash
aplay -l | grep -i i2s
# Expected: snd_rpi_i2s or bcm2835
```

**3. Check Python Environment:**
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
python -c "import flask, serial, numpy, scipy, yaml; print('All dependencies OK')"
# Expected: All dependencies OK
```

**4. Test Application Startup:**
```bash
cd /home/pi/esp32_btaudio/rpi_i2s_source
source venv/bin/activate
python main.py
```

Expected output:
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
 * Running on http://192.168.1.100:5000
```

Press `Ctrl+C` to stop.

**5. Test Web UI:**

Open browser:
```
http://192.168.1.100:5000
```

You should see the dashboard with:
- Audio source selector (Tone, Sweep, WAV)
- I2S status panel
- UART status panel
- Bluetooth control tab

**6. Test I2S Audio Generation:**

In web UI:
1. Select "Tone" source
2. Set frequency to 1000 Hz
3. Set amplitude to 50%
4. Click "Start"

Check I2S status panel:
- Status: Active
- Buffer fill: ~40-60%
- Underruns: 0 (or very low)

**7. Test UART Communication (if ESP32 connected):**

In web UI Bluetooth tab:
1. Click "SCAN"
2. Wait for nearby Bluetooth devices to appear
3. Click "CONNECT" next to your Bluetooth speaker
4. Wait for connection (~10-20 seconds)
5. Click "START" to begin playback

You should hear a 1 kHz tone from the Bluetooth speaker.

---

## Troubleshooting

### Issue: Cannot SSH into Raspberry Pi

**Possible Causes:**
- SSH not enabled during OS setup
- Wrong IP address
- Raspberry Pi not connected to WiFi

**Solutions:**
1. Re-flash microSD card with SSH enabled in customization
2. Connect monitor + keyboard, enable SSH:
   ```bash
   sudo raspi-config
   # 3 Interface Options → I2 SSH → Enable
   ```
3. Check router for correct IP address
4. Use Ethernet cable for initial setup

### Issue: `/dev/serial0` not found

**Cause:** UART not enabled or Bluetooth not disabled

**Solution:**
1. Check `/boot/config.txt` has `dtoverlay=disable-bt`
2. Check `/boot/cmdline.txt` does NOT have `console=serial0,115200`
3. Run `sudo raspi-config` → Interface Options → Serial Port:
   - Login shell over serial? → **No**
   - Serial port hardware enabled? → **Yes**
4. Reboot

### Issue: Permission denied on `/dev/ttyAMA0`

**Cause:** User not in `dialout` group

**Solution:**
```bash
sudo usermod -a -G dialout pi
# Log out and log back in
exit
ssh pi@192.168.1.100
groups  # Verify dialout is listed
```

### Issue: I2S device not found (aplay -l shows no I2S)

**Cause:** I2S not enabled in `/boot/config.txt`

**Solution:**
```bash
sudo nano /boot/config.txt
# Add:
dtoverlay=i2s-mmap
# Save and reboot
sudo reboot
```

### Issue: Systemd service fails to start

**Check logs:**
```bash
sudo journalctl -u rpi-i2s-source -n 50
```

**Common causes:**
- Python virtual environment path incorrect
- Working directory incorrect
- User `pi` doesn't have permissions
- Missing dependencies

**Solution:**
Verify paths in `/etc/systemd/system/rpi-i2s-source.service`:
```ini
WorkingDirectory=/home/pi/esp32_btaudio/rpi_i2s_source
ExecStart=/home/pi/esp32_btaudio/rpi_i2s_source/venv/bin/python main.py
```

### Issue: High I2S underruns

**Possible causes:**
- CPU overloaded
- Slow microSD card
- WiFi interference
- Insufficient buffer size

**Solutions:**
1. Increase buffer size in `config.yaml`:
   ```yaml
   audio:
     buffer_size: 16384  # Increase from 8192
   ```
2. Use Class 10 or UHS-I microSD card
3. Close unnecessary applications
4. Use Ethernet instead of WiFi

### Issue: No audio from Bluetooth speaker

**Check:**
1. ESP32 powered on and running esp_bt_audio_source firmware
2. ESP32 I2S configured as **slave** (not master)
3. Bluetooth speaker paired and connected (check web UI)
4. Volume >0% in web UI
5. I2S wiring correct (use logic analyzer or oscilloscope to verify signals)

### Issue: UART timeout - no response from ESP32

**Check:**
1. ESP32 powered on
2. UART wiring correct:
   - RPi TX (GPIO14) → ESP32 RX (GPIO17)
   - RPi RX (GPIO15) → ESP32 TX (GPIO16)
   - GND connected
3. UART device correct in `config.yaml`:
   ```yaml
   uart:
     device: "/dev/serial0"
     baudrate: 115200
   ```
4. Test UART with loopback (short GPIO14 and GPIO15 on RPi, send characters, should echo back)

---

## Next Steps

After completing this setup guide:

1. **Run Tests:** See [TESTING.md](TESTING.md) for unit, integration, and performance tests
2. **Deploy Application:** Configure systemd service for production use
3. **Hardware Validation:** Connect ESP32 and Bluetooth speaker, validate end-to-end audio pipeline
4. **Performance Validation:** Run performance tests to verify NFRs (CPU <25%, memory <100 MB)

---

## Additional Resources

- **Raspberry Pi Documentation:** https://www.raspberrypi.com/documentation/
- **I2S on Raspberry Pi:** https://learn.adafruit.com/adafruit-i2s-mems-microphone-breakout/raspberry-pi-wiring-test
- **UART on Raspberry Pi:** https://www.raspberrypi.com/documentation/computers/configuration.html#uarts-and-device-tree
- **systemd Service Tutorial:** https://www.freedesktop.org/software/systemd/man/systemd.service.html

---

## Support

For issues or questions:
- Check [Troubleshooting](#troubleshooting) section above
- Review [TESTING.md](TESTING.md) for hardware validation procedures
- See repository issues page: https://github.com/ekkus93/esp32_btaudio/issues
