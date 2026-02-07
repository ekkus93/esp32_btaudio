# BeagleBone Green Wireless I2S Device Tree Overlays

This directory contains Device Tree overlay (.dts) source files for configuring the BeagleBone Green Wireless McASP0 peripheral for I2S audio transmission.

---

## Overview

The BeagleBone Green Wireless uses Device Tree overlays to configure peripherals at boot time. Unlike the Raspberry Pi (which can bit-bang I2S via GPIO), the BBGW has dedicated hardware for I2S audio via the **McASP (Multichannel Audio Serial Port)** module.

This directory provides two overlay options:

1. **BB-BBGW-I2S-00A0.dts** — Full configuration with ALSA sound card (recommended)
2. **BB-BBGW-I2S-SIMPLE-00A0.dts** — Minimal pin mux only (fallback)

---

## Files

### 1. BB-BBGW-I2S-00A0.dts (Recommended)

**Description:** Complete Device Tree overlay with:
- McASP0 pin muxing (P9.31, P9.29, P9.28)
- McASP0 I2S master mode configuration
- ALSA simple-audio-card integration
- Dummy codec for transmit-only operation

**Features:**
- ✅ Full ALSA integration (accessible via `aplay -l`)
- ✅ Configures McASP0 as I2S master (generates BCLK and WS)
- ✅ 48 kHz sample rate support
- ✅ 16-bit stereo PCM
- ✅ Transmit on AXR1 (P9.28)

**Use When:**
- You want full ALSA integration
- You need the BBGW to be the I2S master (clock generator)
- You want the standard `hw:0,0` ALSA device

**ALSA Device Name:** `hw:CARD=BBGW-I2S,DEV=0` or `hw:0,0`

---

### 2. BB-BBGW-I2S-SIMPLE-00A0.dts (Fallback)

**Description:** Minimal overlay that only configures pin muxing.

**Features:**
- ✅ Pin mux configuration only
- ✅ Enables McASP0 peripheral
- ⚠️ No ALSA sound card created
- ⚠️ Requires manual McASP configuration via `/dev/mem` or kernel driver

**Use When:**
- The full overlay fails to load
- You need to debug pin muxing issues
- You want to configure McASP manually in userspace

**Note:** This overlay does NOT create an ALSA device. You'll need to configure McASP via other means.

---

## Pin Configuration

Both overlays configure the same pins:

| Pin   | Function       | McASP Signal | Direction | Mode | ESP32 Pin |
|-------|----------------|--------------|-----------|------|-----------|
| P9.31 | BCLK           | ACLKX        | Output    | 0    | GPIO26    |
| P9.29 | WS/LRCLK       | FSX          | Output    | 0    | GPIO25    |
| P9.28 | DOUT (Data)    | AXR1         | Output    | 2    | GPIO22    |
| P9.1  | GND            | —            | —         | —    | GND       |

**Signal Specifications:**
- **BCLK (Bit Clock):** 1.536 MHz (48 kHz × 32 bits/frame)
- **WS (Word Select):** 48 kHz, 50% duty cycle
- **DOUT (Data Out):** I2S format, MSB-first, 16-bit samples

---

## Compilation

### Prerequisites

Install the device tree compiler:

```bash
sudo apt update
sudo apt install device-tree-compiler
```

### Compile Full Overlay

```bash
cd /home/phil/work/esp32/esp32_btaudio/bbgw_i2s_source/overlays

# Compile with error checking
dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts

# Check for warnings
echo $?  # Should be 0
```

### Compile Simple Overlay

```bash
dtc -O dtb -o BB-BBGW-I2S-SIMPLE-00A0.dtbo -b 0 -@ BB-BBGW-I2S-SIMPLE-00A0.dts
```

**Compiler Options:**
- `-O dtb` — Output device tree blob (binary)
- `-o <file>` — Output filename
- `-b 0` — Boot CPU (0 for ARM)
- `-@` — Generate symbols for overlays

**Common Warnings (Can Ignore):**
- `Warning (unit_address_vs_reg): Node X has a unit name, but no reg property`
- These are normal for overlays and can be ignored

**Errors to Fix:**
- `ERROR: Syntax error` — Fix syntax in .dts file
- `ERROR: undefined reference` — Missing symbol or include

---

## Installation on BeagleBone

### 1. Copy Compiled Overlay to BBGW

**Option A: USB/Ethernet copy (if BBGW accessible)**

```bash
# From development machine
scp BB-BBGW-I2S-00A0.dtbo debian@192.168.7.2:~
scp BB-BBGW-I2S-SIMPLE-00A0.dtbo debian@192.168.7.2:~
```

**Option B: USB drive**

```bash
# Copy to USB drive, then on BBGW:
sudo cp /media/usb0/BB-BBGW-I2S-00A0.dtbo /lib/firmware/
```

**Option C: Manual compilation on BBGW**

```bash
# Copy .dts source to BBGW
scp BB-BBGW-I2S-00A0.dts debian@192.168.7.2:~

# On BBGW:
dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
```

### 2. Install Overlay to Firmware Directory

```bash
# On BeagleBone Green Wireless
sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
sudo chmod 644 /lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Verify installation
ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
```

### 3. Enable Overlay in /boot/uEnv.txt

**Method A: Using uboot_overlay_addr4 (Recommended)**

```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Find the line (around line 30-40):
# uboot_overlay_addr4=<file4>.dtbo

# Uncomment and change to:
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Save and exit (Ctrl+O, Enter, Ctrl+X)
```

**Method B: Using enable_uboot_overlays and uboot_overlay_pru**

```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Ensure this line is uncommented:
enable_uboot_overlays=1

# Add overlay to one of the available slots:
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
```

**Method C: Using cape_enable (Older method)**

```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Add this line:
cape_enable=bone_capemgr.enable_partno=BB-BBGW-I2S
```

### 4. Reboot BeagleBone

```bash
sudo reboot
```

---

## Verification

### 1. Check Kernel Messages

After reboot, verify the overlay loaded successfully:

```bash
# Check for McASP initialization
dmesg | grep -i mcasp

# Expected output (example):
# [    X.XXXXXX] omap-mcasp 48038000.mcasp: ASP revision 2.00
# [    X.XXXXXX] davinci-mcasp 48038000.mcasp: using I2S mode
```

### 2. Check Pin Muxing

Verify pins are configured correctly:

```bash
# Check pin mux configuration
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A2 990
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A2 994
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A2 99c

# Expected output (example):
# pin 100 (PIN100) 44e10990 00000000 pinctrl-single  # P9.31 Mode 0
# pin 101 (PIN101) 44e10994 00000000 pinctrl-single  # P9.29 Mode 0
# pin 103 (PIN103) 44e1099c 00000002 pinctrl-single  # P9.28 Mode 2
```

### 3. Check ALSA Device (Full Overlay Only)

If using BB-BBGW-I2S-00A0.dtbo:

```bash
# List ALSA playback devices
aplay -l

# Expected output:
# **** List of PLAYBACK Hardware Devices ****
# card 0: BBGW-I2S [BBGW-I2S], device 0: davinci-mcasp.0-snd-soc-dummy-dai snd-soc-dummy-dai-0 []
#   Subdevices: 1/1
#   Subdevice #0: subdevice #0

# Check card information
aplay -L | grep BBGW
```

### 4. Test ALSA Playback

Generate a test tone:

```bash
# Generate 1 kHz test tone (16-bit stereo, 48 kHz)
speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 1000

# Stop with Ctrl+C

# Or use aplay with raw PCM file
aplay -D hw:0,0 -f S16_LE -c 2 -r 48000 test.wav
```

### 5. Logic Analyzer Verification

For hardware validation, connect a logic analyzer to P9.31, P9.29, P9.28:

- **BCLK (P9.31):** Should show 1.536 MHz clock (48 kHz × 32 bits)
- **WS (P9.29):** Should show 48 kHz square wave (50% duty cycle)
- **DOUT (P9.28):** Should show I2S data stream (valid during ALSA playback)

---

## Troubleshooting

### Overlay Not Loading

**Symptoms:**
- No kernel messages about McASP
- `dmesg | grep mcasp` shows nothing

**Fixes:**

1. **Check compilation errors:**
   ```bash
   dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
   ```
   Fix any ERROR messages (warnings OK).

2. **Verify file exists in /lib/firmware:**
   ```bash
   ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

3. **Check /boot/uEnv.txt syntax:**
   ```bash
   grep overlay /boot/uEnv.txt
   ```
   Ensure no typos in overlay path.

4. **Check boot messages:**
   ```bash
   dmesg | grep -i "device tree"
   dmesg | grep -i "overlay"
   ```

5. **Try simple overlay first:**
   Use `BB-BBGW-I2S-SIMPLE-00A0.dtbo` to isolate issues.

---

### Pin Mux Not Changing

**Symptoms:**
- Pins show Mode 7 (GPIO) instead of Mode 0/2
- Pin debugfs shows wrong mode

**Fixes:**

1. **Check for pin conflicts:**
   ```bash
   sudo cat /sys/devices/platform/bone_capemgr/slots
   ```
   Look for other capes that might use P9.28, P9.29, P9.31.

2. **Disable conflicting capes:**
   Edit `/boot/uEnv.txt` and comment out conflicting overlays.

3. **Use config-pin (Debian 10+):**
   ```bash
   # Manually set pin modes
   config-pin P9.31 mcasp
   config-pin P9.29 mcasp
   config-pin P9.28 mcasp
   ```

---

### ALSA Device Not Found

**Symptoms:**
- `aplay -l` doesn't show BBGW-I2S card
- `/dev/snd/` is empty

**Fixes:**

1. **Check sound card creation:**
   ```bash
   dmesg | grep -i "sound\|audio\|alsa"
   ```

2. **Verify McASP loaded:**
   ```bash
   lsmod | grep mcasp
   dmesg | grep davinci
   ```

3. **Load ALSA modules manually:**
   ```bash
   sudo modprobe snd-soc-davinci-mcasp
   sudo modprobe snd-soc-simple-card
   ```

4. **Check for errors:**
   ```bash
   dmesg | grep -i error
   ```

5. **Use simple overlay + manual ALSA config:**
   Create `/etc/asound.conf` with custom ALSA configuration (advanced).

---

### No Audio Output

**Symptoms:**
- ALSA device exists
- `aplay` runs without errors
- No I2S signals on logic analyzer

**Fixes:**

1. **Check McASP clock configuration:**
   ```bash
   dmesg | grep -i "clock\|mcasp"
   ```

2. **Verify system clocks:**
   McASP needs proper clock input. Check if system clocks are enabled.

3. **Test with speaker-test:**
   ```bash
   speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 1000 -l 1
   ```

4. **Check for DMA errors:**
   ```bash
   dmesg | grep -i dma
   ```

5. **Try different sample rates:**
   ```bash
   # Try 44.1 kHz
   speaker-test -D hw:0,0 -c 2 -r 44100 -F S16_LE -t sine -f 1000
   ```

---

### Pin Conflicts with Other Capes

**Symptoms:**
- Overlay loads but pins don't work
- Other cape stops working after loading I2S overlay

**Common Conflicts:**
- **P9.28, P9.29, P9.31:** Used by some audio capes, display capes
- **HDMI:** Uses some McASP pins (disable if not needed)

**Fixes:**

1. **Disable HDMI (if not needed):**
   ```bash
   # Edit /boot/uEnv.txt
   sudo nano /boot/uEnv.txt
   
   # Uncomment:
   disable_uboot_overlay_video=1
   ```

2. **Check loaded capes:**
   ```bash
   sudo cat /sys/devices/platform/bone_capemgr/slots
   ```

3. **Unload conflicting cape:**
   ```bash
   # If using cape manager (older kernels)
   echo -4 > /sys/devices/platform/bone_capemgr/slots
   # (Replace -4 with slot number from slots file)
   ```

---

## Advanced Configuration

### Changing Sample Rate

To support different sample rates (e.g., 44.1 kHz), modify the overlay:

```dts
/* In fragment@2, sound section: */
sound_master: simple-audio-card,cpu {
    sound-dai = <&mcasp0>;
    system-clock-frequency = <22579200>;  /* 44.1 kHz * 512 */
};
```

Recompile and reinstall.

### Using Different Serializer (AXR0 instead of AXR1)

Modify fragment@1 in overlay:

```dts
/* Use AXR0 (P9.30) instead of AXR1 (P9.28) */
serial-dir = <
    1 /* AXR0: transmit (P9.30) */
    0 /* AXR1: unused */
    0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
>;

/* Also update pin mux in fragment@0: */
pinctrl-single,pins = <
    0x990 0x00  /* P9.31: ACLKX */
    0x994 0x00  /* P9.29: FSX */
    0x998 0x00  /* P9.30: AXR0 (Mode 0) */
>;
```

### I2S Slave Mode (BBGW receives clock from ESP32)

If ESP32 is the I2S master, modify fragment@2:

```dts
simple-audio-card,bitclock-master = <&sound_codec>;
simple-audio-card,frame-master = <&sound_codec>;
```

And add external clock configuration (advanced, requires ESP32 hardware changes).

---

## References

### Documentation
- [AM335x Technical Reference Manual (Chapter 22: McASP)](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
- [BeagleBone Device Tree Overlays](https://elinux.org/Beagleboard:BeagleBoneBlack_Debian#Loading_custom_capes)
- [Linux ALSA simple-audio-card](https://www.kernel.org/doc/html/latest/sound/soc/index.html)

### Example Overlays
- [BB.org Overlays Repository](https://github.com/beagleboard/bb.org-overlays)
- [BeagleBone Audio Overlays](https://github.com/beagleboard/bb.org-overlays/tree/master/src/arm)

### Tools
- Device Tree Compiler: `sudo apt install device-tree-compiler`
- Logic Analyzer: Saleae Logic, DSLogic, PulseView/sigrok

---

## UART4 Device Tree Overlay (ESP32 Communication)

### Overview

The BeagleBone Green Wireless provides 6 UART peripherals (UART0-UART5). For ESP32 communication:
- **UART0** is reserved for the serial console (avoid)
- **UART3** may interfere with Bluetooth (avoid)
- **UART4** is recommended for ESP32 communication (/dev/ttyO4)

This overlay enables UART4 on pins P9.11 (RXD) and P9.13 (TXD) for communication with the ESP32.

### File

**BB-BBGW-UART4-00A0.dts**

**Description:** UART4 Device Tree overlay with pin muxing for P9.11/P9.13.

**Features:**
- ✅ Pin mux for P9.11 (UART4 RXD, Mode 6)
- ✅ Pin mux for P9.13 (UART4 TXD, Mode 6)
- ✅ UART4 peripheral enable
- ✅ Creates /dev/ttyO4 device
- ✅ 115200 baud, 8N1, no flow control

**Use When:**
- You want persistent UART4 enablement (survives reboot)
- You prefer Device Tree overlay over config-pin

**Device:** `/dev/ttyO4`

---

### Pin Configuration

| Pin   | Function  | UART Signal | Direction | Mode | Value | ESP32 Pin |
|-------|-----------|-------------|-----------|------|-------|-----------|
| P9.11 | UART4 RXD | RXD         | Input     | 6    | 0x26  | GPIO17 (TX) |
| P9.13 | UART4 TXD | TXD         | Output    | 6    | 0x06  | GPIO16 (RX) |
| P9.1  | GND       | —           | —         | —    | —     | GND       |

**Pin Mux Details:**
- **P9.11 (offset 0x070):** Mode 6, Input, Pull-up enabled (value 0x26)
- **P9.13 (offset 0x074):** Mode 6, Output, Pull disabled (value 0x06)

**Signal Specifications:**
- **Baudrate:** 115200 (default, configurable via application)
- **Data format:** 8 data bits, no parity, 1 stop bit (8N1)
- **Flow control:** None (RTS/CTS not used)
- **Voltage:** 3.3V TTL (compatible with ESP32)

---

### Compilation

Compile the UART4 overlay:

```bash
cd /home/phil/work/esp32/esp32_btaudio/bbgw_i2s_source/overlays

# Compile
dtc -O dtb -o BB-BBGW-UART4-00A0.dtbo -b 0 -@ BB-BBGW-UART4-00A0.dts

# Or use the script (compiles all overlays)
./compile_overlays.sh --all
```

---

### Installation

Three methods to enable UART4:

#### Method 1: config-pin (Recommended for Testing)

**Pros:** Quick, no reboot required  
**Cons:** Not persistent (lost on reboot)

```bash
# Enable UART4 pins
sudo config-pin P9.11 uart
sudo config-pin P9.13 uart

# Verify
ls -l /dev/ttyO4
```

#### Method 2: Device Tree Overlay (Recommended for Production)

**Pros:** Persistent across reboots  
**Cons:** Requires compilation and reboot

```bash
# 1. Compile overlay (if not done)
./compile_overlays.sh --all

# 2. Copy to firmware directory
sudo cp BB-BBGW-UART4-00A0.dtbo /lib/firmware/

# 3. Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# 4. Add overlay (find uboot_overlay_addr4 or similar)
uboot_overlay_addr4=/lib/firmware/BB-BBGW-UART4-00A0.dtbo

# 5. Reboot
sudo reboot

# 6. Verify after reboot
ls -l /dev/ttyO4
```

#### Method 3: Automated Script (Easiest)

**Pros:** Detects best method automatically  
**Cons:** May fall back to non-persistent method

```bash
# Run enable script (auto-detects best method)
./enable_uart4.sh

# Or specify method explicitly
./enable_uart4.sh --config-pin    # Non-persistent
./enable_uart4.sh --overlay       # Persistent
```

---

### Verification

#### Quick Verification

```bash
# Check device exists
ls -l /dev/ttyO4

# Should show:
# crw-rw---- 1 root dialout 250, 4 Feb  7 12:00 /dev/ttyO4
```

#### Comprehensive Verification

```bash
# Run verification script
./verify_uart4.sh

# Or with verbose output
./verify_uart4.sh --verbose
```

**Checks performed:**
1. ✅ Hardware detection (BeagleBone model, ARM architecture)
2. ✅ Device file (/dev/ttyO4 presence, major/minor numbers)
3. ✅ Permissions (readable/writable, dialout group membership)
4. ✅ Pin mux (P9.11/P9.13 Mode 6 verification via debugfs)
5. ✅ Kernel UART driver (dmesg messages, loaded modules)
6. ⚠️ Loopback test (optional, requires P9.11↔P9.13 jumper)

#### Loopback Test (Hardware Validation)

**Prerequisites:**
- UART4 enabled
- P9.11 connected to P9.13 with jumper wire
- python3 and pyserial installed

```bash
# Install dependencies (if needed)
sudo apt install python3 python3-pip
pip3 install pyserial

# Run loopback test
./test_uart4_loopback.sh

# Or with custom settings
./test_uart4_loopback.sh --baudrate=230400 --duration=10
```

**What it tests:**
- ✅ Bidirectional data transmission
- ✅ Data integrity (all bytes match)
- ✅ Throughput measurement
- ✅ Error rate calculation

**Expected Output:**
```
========================================
Loopback Test Results
========================================
Duration:         5.02 seconds
Packets sent:     100
Packets received: 100
Bytes sent:       6400
Bytes received:   6400
Success rate:     100.0%
Error rate:       0.0%
Throughput:       10195 bits/sec (1274 bytes/sec)
========================================

✓ PASS: All data transmitted correctly
```

---

### Troubleshooting UART4

#### Device Not Found (/dev/ttyO4 missing)

**Symptom:** `ls /dev/ttyO4` shows "No such file or directory"

**Possible Causes:**
1. UART4 not enabled in Device Tree
2. Universal cape may have different pin configuration
3. Kernel driver not loaded

**Solution:**
```bash
# Check if UART4 is enabled
dmesg | grep -i uart | grep -i "48022000\|uart4"

# Expected: "48022000.serial: ttyO4 at MMIO 0x48022000"

# If not found, enable via config-pin
sudo config-pin P9.11 uart
sudo config-pin P9.13 uart

# Or install overlay (persistent)
./enable_uart4.sh --overlay
```

#### Permission Denied When Opening /dev/ttyO4

**Symptom:** "Permission denied" when accessing /dev/ttyO4

**Cause:** User not in `dialout` group

**Solution:**
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and back in (or reboot)
# Verify membership
groups | grep dialout
```

#### Loopback Test Fails

**Symptom:** Loopback test reports data mismatch or incomplete receive

**Possible Causes:**
1. Jumper wire not connected between P9.11 and P9.13
2. Loose connection
3. Other device using UART4 (e.g., getty, ModemManager)

**Solution:**
```bash
# 1. Check jumper wire connection physically

# 2. Verify no other process is using UART4
sudo lsof /dev/ttyO4

# If found, kill the process:
sudo killall <process_name>

# 3. Disable getty on ttyO4 (if running)
sudo systemctl stop serial-getty@ttyO4.service
sudo systemctl disable serial-getty@ttyO4.service

# 4. Re-run loopback test
./test_uart4_loopback.sh
```

#### Pin Mux Not Set to Mode 6

**Symptom:** verify_uart4.sh reports wrong mode for P9.11 or P9.13

**Cause:** Another overlay or config-pin command overrode pin mux

**Solution:**
```bash
# Check current pin mux
cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -E "pin 28|pin 29"

# Should show:
#  pin 28 (PIN28): ... (MUX UNCLAIMED) (GPIO UNCLAIMED)
#  pin 29 (PIN29): 48022000.serial (MUX UNCLAIMED) (GPIO UNCLAIMED)

# Re-apply pin mux
sudo config-pin P9.11 uart
sudo config-pin P9.13 uart

# Or reload overlay
sudo reboot  # If using Device Tree overlay
```

#### Data Corruption or Baud Rate Mismatch

**Symptom:** Garbled data, random characters

**Cause:** Baud rate mismatch between BBGW and ESP32

**Solution:**
```bash
# Verify UART4 configuration
stty -F /dev/ttyO4

# Should show: speed 115200 baud; ...

# Set baud rate explicitly
stty -F /dev/ttyO4 115200

# Match ESP32 baud rate in config.yaml
# Default: 115200 (8N1, no flow control)
```

---

## Next Steps

After successfully loading the overlays:

### I2S (McASP0)
1. ✅ Verify ALSA device: `aplay -l`
2. ✅ Update `bbgw_i2s_source/config.yaml` with correct ALSA device name
3. ✅ Test with `speaker-test`

### UART4
1. ✅ Verify device: `ls -l /dev/ttyO4`
2. ✅ Run loopback test: `./test_uart4_loopback.sh`
3. ✅ Update `bbgw_i2s_source/config.yaml` with `/dev/ttyO4`

### Hardware Integration
1. ✅ Connect ESP32 hardware
   - I2S: P9.31 (BCLK) → GPIO26, P9.29 (WS) → GPIO25, P9.28 (DOUT) → GPIO22
   - UART: P9.13 (TXD) → GPIO16 (ESP32 RX), P9.11 (RXD) → GPIO17 (ESP32 TX)
   - Power: GND → GND (common ground required)
2. ✅ Run Milestone 1 test (1 kHz tone generation via I2S)
3. ✅ Run Milestone 2 test (UART command/response)
4. ✅ Verify with logic analyzer (optional but recommended)
5. ✅ Proceed to Phase 2: Code Adaptations

---

**Last Updated:** 2026-02-07  
**Status:** Phase 1 (Device Tree Configuration) complete  
**Next:** Test on hardware, then proceed to Phase 2 (Code Adaptations)
