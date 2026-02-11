# BeagleBone Green Wireless I2S Source — Troubleshooting Guide

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Comprehensive troubleshooting reference for common issues  
**Date:** 2026-02-07

---

## Overview

This guide provides solutions to common problems encountered when setting up and running the BBGW I2S Source application. Issues are organized by category for quick reference.

**Related Documentation:**
- [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md) - Hardware configuration and wiring
- [SOFTWARE_SETUP_BBGW.md](SOFTWARE_SETUP_BBGW.md) - Software installation and configuration
- [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md) - Device Tree overlay configuration
- [BBGW_PIN_REFERENCE.md](BBGW_PIN_REFERENCE.md) - Pin assignments and GPIO numbering

---

## Table of Contents

1. [McASP/I2S Issues](#mcaspi2s-issues)
2. [UART Issues](#uart-issues)
3. [Network Issues](#network-issues)
4. [Performance Issues](#performance-issues)
5. [Device Tree Issues](#device-tree-issues)
6. [Application Issues](#application-issues)
7. [UDA1334ATS DAC Output Issues](#uda1334ats-dac-output-issues)
8. [Quick Diagnostic Commands](#quick-diagnostic-commands)

---

## McASP/I2S Issues

### Issue 1: Device Tree overlay not loading

**Symptoms:**
```bash
$ aplay -l
aplay: device_list:274: no soundcards found...

$ dmesg | grep mcasp
# No output
```

**Diagnosis:**
```bash
# Check if overlay is configured in U-Boot
grep "BB-BBGW-I2S" /boot/uEnv.txt

# Check if overlay file exists
ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Check for Device Tree errors
dmesg | grep -i "device tree\|overlay"
```

**Solutions:**

1. **Verify overlay file exists:**
   ```bash
   sudo ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```
   - If missing, reinstall: `sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/`

2. **Check /boot/uEnv.txt configuration:**
   ```bash
   sudo nano /boot/uEnv.txt
   ```
   - Ensure line is **not** commented out:
     ```
     uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
     ```
   - Must not have leading `#` character

3. **Reboot to apply changes:**
   ```bash
   sudo reboot
   ```

4. **Verify overlay loaded after reboot:**
   ```bash
   dmesg | grep -i mcasp
   # Should show: "davinci-mcasp 48038000.mcasp: ... registered"
   ```

**See also:** [BBGW_DEVICE_TREE_GUIDE.md Section 7](BBGW_DEVICE_TREE_GUIDE.md#common-issues)

---

### Issue 2: ALSA device not found

**Symptoms:**
```bash
$ aplay -l
aplay: device_list:274: no soundcards found...

$ python3 main.py
ERROR: ALSA device 'hw:0,0' not found
```

**Diagnosis:**
```bash
# Check if McASP driver loaded
dmesg | grep -i mcasp

# Check ALSA cards
cat /proc/asound/cards

# Check for ALSA errors
dmesg | grep -i alsa
```

**Solutions:**

1. **Verify Device Tree overlay loaded (see Issue 1)**

2. **Check ALSA modules loaded:**
   ```bash
   lsmod | grep snd
   ```
   - Should show: `snd_soc_davinci_mcasp`, `snd_soc_simple_card`, etc.

3. **Reload ALSA:**
   ```bash
   sudo alsactl init
   sudo alsactl restore
   ```

4. **Check config.yaml device name:**
   ```bash
   # Get correct device name
   aplay -l
   
   # Update config.yaml
   nano config.yaml
   # Set: i2s.device: "hw:0,0"  # Use card number from aplay -l
   ```

5. **Reboot if ALSA modules not loading:**
   ```bash
   sudo reboot
   ```

---

### Issue 3: No audio output on I2S pins

**Symptoms:**
- Logic analyzer shows no signal on P9.31, P9.29, P9.28
- ESP32 receives no I2S data
- No errors in logs

**Diagnosis:**
```bash
# Test ALSA playback
speaker-test -D hw:0,0 -r 48000 -c 2 -t sine -f 1000

# Check pin muxing
cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins | grep -E "pin 103|pin 104|pin 105"
```

**Solutions:**

1. **Verify ALSA playback works:**
   ```bash
   # Generate test WAV
   speaker-test -D hw:0,0 -r 48000 -c 2 -t wav -W /tmp/test.wav

   # Play test WAV
   aplay -D hw:0,0 /tmp/test.wav
   ```
   - Should hear audio on ESP32/speaker
   - If no errors but no audio, check wiring

2. **Check pin connections:**
   - P9.31 (BCLK) → ESP32 GPIO26
   - P9.29 (WS) → ESP32 GPIO25
   - P9.28 (DOUT) → ESP32 GPIO22
   - P9.1 or P9.2 (GND) → ESP32 GND

3. **Verify pin muxing is correct:**
   ```bash
   cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins | grep "pin 10[345]"
   ```
   - Should show McASP functions, not GPIO

4. **Check for pin conflicts:**
   ```bash
   dmesg | grep -i "pin.*conflict\|already requested"
   ```
   - Disable conflicting overlays in /boot/uEnv.txt

5. **Test with logic analyzer:**
   - Connect analyzer to P9.31, P9.29, P9.28
   - Run `aplay -D hw:0,0 /tmp/test.wav`
   - Should see:
     - BCLK: 1.536 MHz square wave
     - WS: 48 kHz square wave
     - DOUT: Data pulses synchronized with BCLK

---

### Issue 4: Distorted or garbled audio

**Symptoms:**
- Audio plays but sounds wrong
- Clicking, popping, crackling noises
- Pitch incorrect
- One channel missing

**Diagnosis:**
```bash
# Check for buffer underruns
dmesg | tail -50 | grep -i underrun

# Check CPU usage
top
# Look for high CPU processes

# Check sample rate
grep sample_rate config.yaml
```

**Solutions:**

1. **Sample rate mismatch:**
   - **Cause:** BBGW and ESP32 sample rates don't match
   - **Fix:** Ensure both use 48 kHz
     ```yaml
     # config.yaml
     i2s:
       sample_rate: 48000
     ```
   - ESP32 must also be configured for 48 kHz

2. **Buffer underruns:**
   - **Cause:** CPU too slow to feed audio data
   - **Fix:** Increase buffer size
     ```yaml
     # config.yaml
     i2s:
       buffer_size: 8192  # Increase from 4096
     ```

3. **Wiring issues:**
   - **Cause:** Loose connections, long wires, interference
   - **Fix:**
     - Check all connections secure
     - Keep I2S wires <30 cm
     - Route away from power lines
     - Use ground wires at both ends

4. **Format mismatch:**
   - **Cause:** BBGW and ESP32 audio formats differ
   - **Fix:** Ensure both use S16_LE (16-bit signed little-endian)
     ```yaml
     # config.yaml
     i2s:
       format: "S16_LE"
       channels: 2
     ```

5. **ESP32 clock issues:**
   - **Cause:** ESP32 I2S clock configuration incorrect
   - **Fix:** Verify ESP32 is in slave mode, using BBGW clocks
     ```c
     // ESP32 I2S config
     .mode = I2S_MODE_SLAVE | I2S_MODE_RX
     ```

---

### Issue 5: Buffer underruns

**Symptoms:**
```bash
$ dmesg | tail -20
[12345.678] davinci-mcasp 48038000.mcasp: underrun on TX channel
```

**Diagnosis:**
```bash
# Monitor underruns in real-time
dmesg -w | grep -i underrun

# Check CPU usage
top

# Check I/O wait
iostat -x 1
```

**Solutions:**

1. **Increase buffer size:**
   ```yaml
   # config.yaml
   i2s:
     buffer_size: 8192  # or 16384
     period_size: 2048  # or 4096
   ```

2. **Reduce CPU load:**
   ```bash
   # Stop unnecessary services
   sudo systemctl stop apache2  # If not using
   sudo systemctl stop bluetooth  # If not using onboard BT
   ```

3. **Increase process priority:**
   ```bash
   # Run with higher priority
   sudo nice -n -10 python3 main.py
   ```

4. **Check for I/O contention:**
   - Avoid writing to SD card during playback
   - Use tmpfs for temporary files:
     ```bash
     mkdir -p /tmp/audio
     # Update config.yaml audio directory to /tmp/audio
     ```

5. **Upgrade kernel (if old):**
   ```bash
   sudo apt update
   sudo apt upgrade
   # Newer kernels have better McASP performance
   ```

---

## UART Issues

### Issue 6: /dev/ttyO4 not found

**Symptoms:**
```bash
$ ls /dev/ttyO4
ls: cannot access '/dev/ttyO4': No such file or directory

$ python3 main.py
ERROR: Failed to open UART device '/dev/ttyO4'
```

**Diagnosis:**
```bash
# Check if UART4 overlay loaded
dmesg | grep -i "uart4\|ttyO4"

# Check U-Boot configuration
grep "BB-BBGW-UART4" /boot/uEnv.txt

# List all tty devices
ls /dev/ttyO*
```

**Solutions:**

1. **Verify UART4 overlay configured:**
   ```bash
   sudo nano /boot/uEnv.txt
   ```
   - Ensure line is **not** commented:
     ```
     uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
     ```

2. **Verify overlay file exists:**
   ```bash
   sudo ls -l /lib/firmware/BB-BBGW-UART4-00A0.dtbo
   ```
   - If missing, reinstall overlay

3. **Reboot to apply:**
   ```bash
   sudo reboot
   ```

4. **Verify device created:**
   ```bash
   ls -l /dev/ttyO4
   # Should show: crw-rw---- 1 root dialout 247, 4 ...
   ```

5. **Check kernel messages:**
   ```bash
   dmesg | grep ttyO4
   # Should show: "48024000.serial: ttyO4 at MMIO 0x48024000 ..."
   ```

**See also:** [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md)

---

### Issue 7: Permission denied accessing /dev/ttyO4

**Symptoms:**
```bash
$ python3 main.py
PermissionError: [Errno 13] Permission denied: '/dev/ttyO4'
```

**Diagnosis:**
```bash
# Check device permissions
ls -l /dev/ttyO4
# Should show: crw-rw---- 1 root dialout 247, 4 ...

# Check user groups
groups
# Should include 'dialout'
```

**Solutions:**

1. **Add user to dialout group:**
   ```bash
   sudo usermod -a -G dialout $USER
   ```

2. **Log out and back in:**
   - Group membership requires re-login
   - Or use: `newgrp dialout` (temporary)

3. **Verify group membership:**
   ```bash
   groups
   # Should show: ... dialout ...
   ```

4. **Alternative: Run with sudo (NOT RECOMMENDED):**
   ```bash
   sudo python3 main.py
   # Better to fix permissions properly
   ```

---

### Issue 8: No response from ESP32

**Symptoms:**
```bash
$ python3 -c "import serial; s = serial.Serial('/dev/ttyO4', 115200, timeout=1); s.write(b'STATUS\n'); print(s.readline())"
b''  # Empty response
```

**Diagnosis:**
```bash
# Test UART loopback (requires jumper wire P9.11 ↔ P9.13)
minicom -D /dev/ttyO4 -b 115200
# Type characters; if echoed back, UART works

# Check ESP32 connection
# Verify TX/RX crossover:
#   BBGW P9.13 (TX) → ESP32 GPIO16 (RX)
#   BBGW P9.11 (RX) ← ESP32 GPIO17 (TX)
```

**Solutions:**

1. **Verify TX/RX crossover:**
   - BBGW TX must connect to ESP32 RX
   - BBGW RX must connect to ESP32 TX
   - **NOT** TX→TX, RX→RX

2. **Check ESP32 powered on:**
   - Verify ESP32 has power
   - Check ESP32 status LED (if present)

3. **Check ESP32 firmware:**
   - ESP32 must be running esp_bt_audio_source firmware
   - UART must be enabled in ESP32 configuration

4. **Verify baud rate matches:**
   - BBGW: 115200 (in config.yaml)
   - ESP32: 115200 (in firmware)

5. **Check ground connection:**
   - Ensure common ground between BBGW and ESP32

6. **Test with minicom:**
   ```bash
   minicom -D /dev/ttyO4 -b 115200
   ```
   - Type commands manually
   - Check for responses

---

### Issue 9: Garbled UART data

**Symptoms:**
- ESP32 responses contain garbage characters
- Commands don't work
- Random characters appear

**Diagnosis:**
```bash
# Check baud rate
grep baudrate config.yaml

# Test with minicom at different rates
minicom -D /dev/ttyO4 -b 9600   # Try lower rate
minicom -D /dev/ttyO4 -b 115200  # Normal rate
```

**Solutions:**

1. **Baud rate mismatch:**
   - **Most common cause**
   - Ensure BBGW and ESP32 both use 115200
   - Update config.yaml:
     ```yaml
     uart:
       baudrate: 115200
     ```

2. **Electrical noise:**
   - Use shielded cable for long runs
   - Keep UART wires away from I2S wires
   - Keep UART wires away from power lines
   - Add 100 Ω resistors in series with TX/RX (optional)

3. **Ground loop:**
   - Ensure single ground connection between BBGW and ESP32
   - Don't use multiple ground wires if not needed

4. **Wiring issues:**
   - Check for loose connections
   - Verify TX/RX not swapped
   - Check for shorts (TX touching RX, etc.)

5. **Hardware problem:**
   - Try different jumper wires
   - Test with USB-to-serial adapter to isolate issue

---

## Network Issues

### Issue 10: Wi-Fi not connecting

**Symptoms:**
```bash
$ ip link show wlan0
# wlan0 not present or DOWN

$ ping google.com
ping: google.com: Temporary failure in name resolution
```

**Diagnosis:**
```bash
# Check Wi-Fi interface status
ip link show wlan0

# Check Wi-Fi driver loaded
lsmod | grep -i wifi

# Check network manager status
systemctl status connman
# OR
systemctl status NetworkManager
```

**Solutions:**

1. **Check Wi-Fi interface:**
   ```bash
   # Bring up interface
   sudo ip link set wlan0 up

   # Scan for networks
   sudo iw wlan0 scan | grep SSID
   ```

2. **Configure Wi-Fi with connman:**
   ```bash
   sudo connmanctl
   connmanctl> enable wifi
   connmanctl> scan wifi
   connmanctl> services
   connmanctl> agent on
   connmanctl> connect wifi_*_managed_psk
   # Enter passphrase when prompted
   ```

3. **Alternative: Configure with wpa_supplicant:**
   ```bash
   sudo nano /etc/wpa_supplicant/wpa_supplicant.conf
   ```
   Add:
   ```
   network={
       ssid="YourSSID"
       psk="YourPassword"
   }
   ```
   ```bash
   sudo systemctl restart wpa_supplicant
   ```

4. **Check firewall:**
   ```bash
   # Disable firewall temporarily to test
   sudo ufw disable
   ```

5. **Reboot:**
   ```bash
   sudo reboot
   ```

---

### Issue 11: Web UI not accessible

**Symptoms:**
```bash
# From another computer:
$ curl http://192.168.8.1:5000
curl: (7) Failed to connect to 192.168.8.1 port 5000: Connection refused
```

**Diagnosis:**
```bash
# Check Flask server running
ps aux | grep python3

# Check Flask listening on correct port
sudo netstat -tulnp | grep :5000

# Check firewall
sudo ufw status

# Check BBGW IP address
hostname -I
```

**Solutions:**

1. **Verify Flask server running:**
   ```bash
   cd ~/bbgw_i2s_source
   source venv/bin/activate
   python3 main.py
   ```
   - Should show: `Running on http://0.0.0.0:5000`

2. **Check bind address in config.yaml:**
   ```yaml
   web:
     bind_address: "0.0.0.0"  # Listen on all interfaces
     port: 5000
   ```

3. **Check firewall allows port 5000:**
   ```bash
   sudo ufw allow 5000/tcp
   sudo ufw reload
   ```

4. **Verify BBGW IP address:**
   ```bash
   hostname -I
   # Use this IP in browser, e.g., http://192.168.7.2:5000
   ```

5. **Test from BBGW itself:**
   ```bash
   curl http://localhost:5000
   ```
   - If works locally but not remotely, firewall issue

6. **Check browser URL:**
   - Use `http://`, not `https://`
   - Include port `:5000`
   - Example: `http://192.168.7.2:5000`

---

### Issue 12: Firewall blocking connections

**Symptoms:**
- Can ping BBGW but can't access web UI
- UART works but web UI doesn't

**Diagnosis:**
```bash
# Check firewall status
sudo ufw status verbose

# Check iptables rules
sudo iptables -L -n -v
```

**Solutions:**

1. **Allow port 5000 through firewall:**
   ```bash
   sudo ufw allow 5000/tcp
   sudo ufw reload
   ```

2. **Disable firewall temporarily (testing only):**
   ```bash
   sudo ufw disable
   # Test web UI
   sudo ufw enable  # Re-enable after testing
   ```

3. **Check iptables rules:**
   ```bash
   # List rules
   sudo iptables -L -n

   # Allow port 5000
   sudo iptables -A INPUT -p tcp --dport 5000 -j ACCEPT
   ```

4. **Persist iptables rules:**
   ```bash
   sudo apt-get install iptables-persistent
   sudo netfilter-persistent save
   ```

---

## Performance Issues

### Issue 13: High CPU usage

**Symptoms:**
```bash
$ top
# python3 process showing 80-100% CPU
```

**Diagnosis:**
```bash
# Monitor CPU usage
top -p $(pgrep python3)

# Check for runaway threads
ps -eLf | grep python3
```

**Solutions:**

1. **Reduce audio buffer processing:**
   ```yaml
   # config.yaml
   i2s:
     buffer_size: 4096  # Reduce if too high
   ```

2. **Reduce SSE update rate:**
   ```yaml
   # config.yaml
   web:
     sse_update_interval: 1.0  # Increase from 0.5 seconds
   ```

3. **Disable debug logging:**
   ```yaml
   # config.yaml
   logging:
     level: "INFO"  # Not "DEBUG"
   ```

4. **Profile Python code:**
   ```bash
   python3 -m cProfile -s cumulative main.py > profile.txt
   # Review profile.txt for slow functions
   ```

5. **Close unused browser tabs:**
   - Each SSE connection consumes CPU
   - Close extra web UI tabs

---

### Issue 14: Memory leaks

**Symptoms:**
```bash
$ free -h
# Memory usage grows over time

$ ps aux | grep python3
# RSS (resident memory) increasing steadily
```

**Diagnosis:**
```bash
# Monitor memory usage over time
watch -n 5 'ps aux | grep python3'

# Check for Python memory leaks
python3 -m memory_profiler main.py
```

**Solutions:**

1. **Restart application periodically:**
   ```bash
   # Add to crontab (restart daily at 3 AM)
   0 3 * * * systemctl restart bbgw-i2s-source
   ```

2. **Check for circular references:**
   - Review Python code for objects holding references
   - Use weak references where appropriate

3. **Limit audio file caching:**
   ```yaml
   # config.yaml
   audio:
     max_cache_size: 10  # MB
   ```

4. **Monitor with htop:**
   ```bash
   sudo apt-get install htop
   htop
   # Press 'F5' for tree view
   ```

5. **Use memory profiler:**
   ```bash
   pip install memory_profiler
   python3 -m memory_profiler main.py
   ```

---

### Issue 15: Slow response times

**Symptoms:**
- Web UI buttons take >5 seconds to respond
- Tone changes delayed
- SSE updates laggy

**Diagnosis:**
```bash
# Check CPU usage
top

# Check I/O wait
iostat -x 1

# Check network latency
ping <bbgw-ip>
```

**Solutions:**

1. **Reduce I/O operations:**
   - Don't write logs to SD card during audio playback
   - Use tmpfs for temporary files:
     ```bash
     sudo mount -t tmpfs -o size=100M tmpfs /tmp/audio
     ```

2. **Optimize audio generation:**
   - Pre-generate tone buffers
   - Use NumPy efficiently
   - Cache WAV files

3. **Reduce SSE update frequency:**
   ```yaml
   # config.yaml
   web:
     sse_update_interval: 1.0  # Increase from 0.5 seconds
   ```

4. **Check network quality:**
   - Use wired connection if possible
   - Reduce Wi-Fi interference

5. **Upgrade to faster SD card:**
   - Use Class 10 or UHS-I card
   - Avoid cheap/slow cards

---

## Device Tree Issues

### Issue 16: Overlay compilation errors

**Symptoms:**
```bash
$ sudo dtc -O dtb -o /lib/firmware/BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
ERROR: syntax error
```

**Diagnosis:**
```bash
# Check DTS syntax
cat BB-BBGW-I2S-00A0.dts

# Verify dtc version
dtc --version
```

**Solutions:**

1. **Check DTS syntax:**
   - Ensure all braces `{}` match
   - Ensure all semicolons `;` present
   - Check for typos in property names

2. **Verify pinmux offsets:**
   - P9.31: 0x190
   - P9.29: 0x194
   - P9.28: 0x19c
   - See [BBGW_PIN_REFERENCE.md](BBGW_PIN_REFERENCE.md)

3. **Use correct dtc flags:**
   ```bash
   sudo dtc -O dtb -o output.dtbo -b 0 -@ input.dts
   ```
   - `-@` flag required for overlays

4. **Update device-tree-compiler:**
   ```bash
   sudo apt-get update
   sudo apt-get install --reinstall device-tree-compiler
   ```

5. **Use pre-compiled overlays:**
   - Download from project repository
   - Copy to `/lib/firmware/`

**See also:** [BBGW_DEVICE_TREE_GUIDE.md Section 5](BBGW_DEVICE_TREE_GUIDE.md#overlay-compilation)

---

### Issue 17: Pin conflicts

**Symptoms:**
```bash
$ dmesg | grep -i overlay
OF: overlay: Failed to apply overlay: -16

$ dmesg | grep -i "already requested"
pinctrl-single 44e10800.pinmux: pin 44e10190 already requested
```

**Diagnosis:**
```bash
# Check which overlays are loaded
cat /proc/device-tree/chosen/overlays/name

# Check pin usage
cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins | grep "pin 104\|pin 105\|pin 103"
```

**Solutions:**

1. **Identify conflicting overlays:**
   ```bash
   # List all loaded overlays
   ls /proc/device-tree/chosen/overlays/
   ```

2. **Disable conflicting overlays in /boot/uEnv.txt:**
   ```bash
   sudo nano /boot/uEnv.txt
   ```
   - Common conflicts:
     - HDMI overlays (use same pins as McASP)
     - Cape overlays
     - Other audio overlays

3. **Disable HDMI (if not needed):**
   ```bash
   # In /boot/uEnv.txt, uncomment:
   disable_uboot_overlay_video=1
   ```

4. **Reboot to apply:**
   ```bash
   sudo reboot
   ```

5. **Verify pins available:**
   ```bash
   cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep "pin 104\|pin 105\|pin 103"
   # Should show no conflicts
   ```

**See also:** [BBGW_DEVICE_TREE_GUIDE.md Section 7](BBGW_DEVICE_TREE_GUIDE.md#common-issues)

---

### Issue 18: Kernel messages showing errors

**Symptoms:**
```bash
$ dmesg | tail -20
[  123.456] OF: resolver: overlay phandle fixup failed: -22
[  123.789] bone_capemgr: slot #4: Applied #3 overlays, but failed to load firmware
```

**Diagnosis:**
```bash
# Check overlay format
file /lib/firmware/BB-BBGW-I2S-00A0.dtbo
# Should show: Device Tree Blob

# Check kernel compatibility
uname -r
# Kernel should be 4.19+ for overlay support
```

**Solutions:**

1. **Recompile overlay with correct dtc version:**
   ```bash
   sudo apt-get install --reinstall device-tree-compiler
   sudo dtc -O dtb -o /lib/firmware/BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
   ```

2. **Check compatible property:**
   ```c
   // In .dts file:
   compatible = "ti,beaglebone", "ti,beaglebone-green", "ti,beaglebone-green-wireless";
   ```

3. **Update kernel:**
   ```bash
   sudo apt-get update
   sudo apt-get upgrade
   # May update to newer kernel with better overlay support
   ```

4. **Check U-Boot version:**
   ```bash
   sudo /opt/scripts/tools/version.sh
   # Should show recent U-Boot version
   ```

5. **Use known-good overlay:**
   - Download pre-compiled overlay from project
   - Compare with working overlays in `/lib/firmware/`

---

## Application Issues

### Issue 19: Flask server won't start

**Symptoms:**
```bash
$ python3 main.py
OSError: [Errno 98] Address already in use
```

**Diagnosis:**
```bash
# Check if port 5000 in use
sudo netstat -tulnp | grep :5000

# Check for running Python processes
ps aux | grep python3
```

**Solutions:**

1. **Kill existing Flask server:**
   ```bash
   # Find process ID
   ps aux | grep python3
   
   # Kill process
   sudo kill <PID>
   ```

2. **Change port in config.yaml:**
   ```yaml
   web:
     port: 5001  # Use different port
   ```

3. **Wait for port release:**
   - Port may be in TIME_WAIT state
   - Wait 60 seconds and retry

4. **Reboot to clear:**
   ```bash
   sudo reboot
   ```

---

### Issue 20: Python module import errors

**Symptoms:**
```bash
$ python3 main.py
ModuleNotFoundError: No module named 'flask'
```

**Diagnosis:**
```bash
# Check virtual environment active
which python3
# Should show: /home/debian/bbgw_i2s_source/venv/bin/python3

# List installed packages
pip3 list
```

**Solutions:**

1. **Activate virtual environment:**
   ```bash
   cd ~/bbgw_i2s_source
   source venv/bin/activate
   ```

2. **Install dependencies:**
   ```bash
   pip3 install -r requirements.txt
   ```

3. **Verify installation:**
   ```bash
   pip3 list | grep -i flask
   # Should show Flask 3.0.x
   ```

4. **Recreate virtual environment (if corrupt):**
   ```bash
   cd ~/bbgw_i2s_source
   rm -rf venv
   python3 -m venv venv
   source venv/bin/activate
   pip3 install -r requirements.txt
   ```

---

### Issue 21: Configuration file errors

**Symptoms:**
```bash
$ python3 main.py
yaml.scanner.ScannerError: mapping values are not allowed here
```

**Diagnosis:**
```bash
# Check YAML syntax
cat config.yaml

# Validate YAML
python3 -c "import yaml; yaml.safe_load(open('config.yaml'))"
```

**Solutions:**

1. **Check YAML indentation:**
   - Use spaces, not tabs
   - Consistent 2-space or 4-space indentation

2. **Fix common YAML errors:**
   - Ensure colons have space after: `key: value`
   - Quote strings with special characters: `"value:with:colons"`
   - Check for missing quotes around device paths

3. **Use template as reference:**
   ```bash
   cp config.yaml config.yaml.backup
   cp config.yaml.template config.yaml
   # Re-apply customizations carefully
   ```

4. **Validate online:**
   - Copy config.yaml content to https://www.yamllint.com/
   - Check for syntax errors

---

## UDA1334ATS DAC Output Issues

**Context:** Using UDA1334ATS stereo DAC in test mode for direct I2S audio output (no ESP32).

📖 **Full guide:** [UDA1334ATS_SETUP_GUIDE.md](UDA1334ATS_SETUP_GUIDE.md)

---

### Issue 1: No audio output from UDA1334ATS

**Symptoms:**
- Headphones/speakers connected to DAC are silent
- Web UI shows I2S active, no errors
- Application logs show normal operation

**Diagnosis:**

1. **Check power to UDA1334ATS:**
   ```bash
   # Verify BBGW 3.3V output (some modules have LED indicator)
   # Use multimeter: P9.3 to P9.1 should measure ~3.3V
   ```

2. **Verify I2S signals present:**
   ```bash
   # Test ALSA playback directly
   aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 /dev/zero &
   sleep 5
   killall aplay
   
   # With logic analyzer or oscilloscope:
   # - P9.31 (BCLK): Should show 3.072 MHz square wave (32-bit slots)
   # - P9.29 (WSEL): Should show 48 kHz square wave
   # - P9.28 (DOUT): Should show data transitions
   ```

3. **Check wiring:**
   ```bash
   # Verify connections (use continuity tester if available):
   # P9.31 → UDA1334ATS BCLK
   # P9.29 → UDA1334ATS WSEL (or WS/LRCLK)
   # P9.28 → UDA1334ATS DIN
   # P9.3  → UDA1334ATS VIN
   # P9.1  → UDA1334ATS GND
   ```

**Solutions:**

1. **Check jumper wires:**
   - Replace suspect wires (intermittent connections common)
   - Ensure firm connection to P9 header
   - Verify UDA1334ATS pin headers are soldered properly

2. **Verify power:**
   ```bash
   # Check BBGW power supply (5V 2A minimum)
   # Weak USB power can cause issues
   # Use barrel jack 5V adapter if USB unreliable
   ```

3. **Test with simple ALSA playback:**
   ```bash
   # Play test WAV file
   aplay -D hw:0,0 /usr/share/sounds/alsa/Front_Center.wav
   # Should hear "front center" in both ears
   ```

4. **Check headphones/speakers:**
   - Test with different headphones (known working)
   - Verify 3.5mm jack fully inserted into UDA1334ATS
   - Check volume (some UDA1334ATS modules have output level jumper)

**Related:** See [UDA1334ATS_SETUP_GUIDE.md - Troubleshooting](UDA1334ATS_SETUP_GUIDE.md#troubleshooting)

---

### Issue 2: Distorted or clipped audio from UDA1334ATS

**Symptoms:**
- Audio is fuzzy, harsh, or clipped
- Loud pops or crackles
- Signal sounds overdriven

**Diagnosis:**

1. **Check amplitude setting:**
   ```bash
   # In web UI, check tone amplitude
   # If set to 1.0 (100%), signal may clip
   ```

2. **Power supply quality:**
   ```bash
   # Measure BBGW 5V input with multimeter under load
   # Should be 5V ±5% (4.75V - 5.25V)
   # Ripple or voltage sag indicates weak power supply
   ```

**Solutions:**

1. **Reduce amplitude:**
   - Set web UI tone amplitude to **0.3 (30%)** or lower
   - UDA1334ATS has fixed gain; reduce source level instead

2. **Improve power supply:**
   ```bash
   # Use quality 5V 2A power adapter (not cheap USB charger)
   # Barrel jack preferred over USB for stability
   ```

3. **Check ground connection:**
   - Verify BBGW GND (P9.1) → UDA1334ATS GND
   - Avoid ground loops (single point ground)
   - Use short ground wire (<10 cm)

4. **Increase I2S buffer size** (if pops/clicks):
   ```yaml
   # In config.yaml
   i2s:
     period_size: 2048  # Increase from 1024
     buffer_size: 8192  # Increase from 4096
   ```

---

### Issue 3: Only one channel output (mono instead of stereo)

**Symptoms:**
- Audio only in left OR right ear
- Web UI set to stereo mode
- Dual-tone test plays same frequency in both ears

**Diagnosis:**

1. **Check WSEL (Word Select) connection:**
   ```bash
   # P9.29 (McASP0_FSX) MUST be connected to UDA1334ATS WSEL
   # This signal toggles left/right channels
   # Without it, DAC stays on one channel
   ```

2. **Verify config:**
   ```yaml
   # In config.yaml, check:
   i2s:
     channels: 2  # Must be 2 for stereo
   ```

**Solutions:**

1. **Rewire WSEL:**
   ```bash
   # Ensure P9.29 → UDA1334ATS WSEL (or WS/LRCLK) connected
   # Use continuity tester to verify
   ```

2. **Test headphones:**
   - Try different headphones (verify stereo jack)
   - Check 3.5mm plug fully inserted (partial insertion = mono)

3. **Check UDA1334ATS module:**
   - Some modules have WS/WSEL mislabeled
   - Consult module-specific pinout diagram
   - Try swapping BCLK/WSEL if stereo still broken (wrong pinout)

---

### Issue 4: Pops, clicks, or glitches in audio

**Symptoms:**
- Intermittent pops or clicks during playback
- Audio cuts out briefly
- Rhythmic ticking noise

**Diagnosis:**

1. **Check for I2S underruns:**
   ```bash
   # Monitor application logs
   grep -i "underrun\|xrun" /var/log/bbgw_i2s_source.log
   ```

2. **Check CPU load:**
   ```bash
   top
   # Python process should be <25% CPU
   # If >80%, system overloaded
   ```

**Solutions:**

1. **Increase buffer sizes:**
   ```yaml
   # In config.yaml
   i2s:
     period_size: 2048  # Larger = more latency, fewer underruns
     buffer_size: 8192
   ```

2. **Disable Wi-Fi (test):**
   ```bash
   # Temporarily disable to check for interference
   sudo ifconfig wlan0 down
   # Test audio again
   # Re-enable: sudo ifconfig wlan0 up
   ```

3. **Reduce CPU load:**
   - Close unnecessary processes (`sudo systemctl stop <service>`)
   - Lower web UI refresh rate (if implemented)
   - Disable debug logging (`log_level: INFO` in config.yaml)

4. **Check wiring for EMI:**
   - Keep I2S wires away from power cables
   - Twist BCLK/WSEL/DIN together to reduce crosstalk
   - Add ferrite bead if high-frequency noise present

---

### Issue 5: Hiss or background noise (high noise floor)

**Symptoms:**
- Constant background hiss (even during silence mode)
- Noise increases with volume
- "Hum" at 60 Hz or 120 Hz (AC interference)

**Diagnosis:**

1. **Measure noise floor:**
   ```bash
   # In web UI, select "Silence" mode
   # Turn up headphones volume to max
   # Listen for hiss/hum
   ```

2. **Check ground loop:**
   ```bash
   # Disconnect BBGW from all peripherals except UDA1334ATS
   # If noise disappears, ground loop present
   ```

**Solutions:**

1. **Improve ground connection:**
   - Use short, thick ground wire (P9.1 → DAC GND)
   - Single-point ground (no multiple ground paths)
   - Star ground configuration if multiple devices

2. **Power supply filtering:**
   - Use linear power supply (not switching mode) if possible
   - Add 100µF + 0.1µF capacitors across BBGW 5V input (if comfortable with soldering)
   - Use USB isolator if ground loop persists

3. **Check UDA1334ATS module quality:**
   - Cheap modules may have poor noise performance
   - Adafruit UDA1334A breakout has better filtering than generic clones
   - SNR should be >85 dB (datasheet spec: 95 dB typical)

4. **Reduce gain path:**
   - Lower amplitude in web UI (0.3 or less)
   - Use high-impedance headphones (>32Ω) for better SNR

---

## Quick Diagnostic Commands

### System Health

```bash
# Check kernel version
uname -r

# Check system load
uptime

# Check disk space
df -h

# Check memory usage
free -h

# Check running processes
ps aux | grep -E "python|flask|alsa"
```

### I2S/Audio

```bash
# List ALSA devices
aplay -l

# Test audio playback
speaker-test -D hw:0,0 -r 48000 -c 2 -t sine -f 1000

# Check McASP driver
dmesg | grep -i mcasp

# Check ALSA errors
dmesg | grep -i alsa

# Check pin muxing
cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins | grep -E "pin 103|pin 104|pin 105"
```

### UART

```bash
# Check UART device
ls -l /dev/ttyO4

# Test UART with minicom
minicom -D /dev/ttyO4 -b 115200

# Check UART driver
dmesg | grep -i uart

# Check user groups
groups
```

### Network

```bash
# Check IP address
hostname -I

# Check Wi-Fi status
ip link show wlan0

# Test connectivity
ping -c 3 google.com

# Check open ports
sudo netstat -tulnp

# Check firewall
sudo ufw status
```

### Device Tree

```bash
# List loaded overlays
ls /proc/device-tree/chosen/overlays/

# Check overlay loading
dmesg | grep -i overlay

# Verify overlay files
ls -l /lib/firmware/BB-BBGW-*.dtbo

# Check U-Boot configuration
grep "BB-BBGW" /boot/uEnv.txt
```

### Application

```bash
# Check Flask server running
ps aux | grep python3

# Check listening ports
sudo netstat -tulnp | grep :5000

# Check logs
tail -f ~/bbgw_i2s_source/logs/app.log

# Test API endpoint
curl http://localhost:5000/api/status
```

---

## Getting Help

### Self-Help Resources

1. **Review documentation:**
   - [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md)
   - [SOFTWARE_SETUP_BBGW.md](SOFTWARE_SETUP_BBGW.md)
   - [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md)
   - [BBGW_vs_RPI_COMPARISON.md](BBGW_vs_RPI_COMPARISON.md)

2. **Check logs:**
   ```bash
   # Kernel messages
   dmesg | less
   
   # Application logs
   tail -f ~/bbgw_i2s_source/logs/app.log
   
   # System logs
   journalctl -xe
   ```

3. **Search BeagleBone forums:**
   - https://forum.beagleboard.org/
   - Search for error messages

### Reporting Issues

When reporting issues, include:

1. **System information:**
   ```bash
   uname -a
   cat /etc/debian_version
   ```

2. **Hardware setup:**
   - BBGW model
   - ESP32 model
   - Wiring diagram/description

3. **Software versions:**
   ```bash
   python3 --version
   pip3 list | grep -E "flask|serial|numpy"
   ```

4. **Error messages:**
   ```bash
   # Kernel messages
   dmesg | tail -50
   
   # Application errors
   cat ~/bbgw_i2s_source/logs/app.log
   ```

5. **Configuration:**
   ```bash
   cat config.yaml
   ```

6. **Steps to reproduce:**
   - Exact commands run
   - Expected vs actual behavior

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
