# Hardware Setup Guide — BeagleBone Green Wireless I2S Audio Source

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Complete hardware configuration guide for I2S audio transmission to ESP32  
**Date:** 2026-02-07

---

## Overview

This document provides comprehensive hardware setup instructions for the BeagleBone Green Wireless I2S Audio Source project. It consolidates all hardware configuration steps needed to successfully transmit I2S audio from BBGW to ESP32 for Bluetooth playback.

**System Architecture:**
```
┌─────────────────────────────────┐
│  BeagleBone Green Wireless      │
│  ┌──────────────────────────┐   │
│  │  McASP I2S (Master)      │   │      ┌─────────────────────┐
│  │  - BCLK:  P9.31 (GPIO110)├───┼─────→│ ESP32 I2S (Slave)  │
│  │  - WS:    P9.29 (GPIO111)├───┼─────→│   BCK:  GPIO26     │
│  │  - DOUT:  P9.28 (GPIO112)├───┼─────→│   WS:   GPIO25     │
│  └──────────────────────────┘   │      │   DIN:  GPIO22     │
│                                  │      │                     │
│  ┌──────────────────────────┐   │      │  ┌────────────────┐│
│  │  UART4                   │   │      │  │  UART          ││
│  │  - TX:    P9.13 (GPIO31) ├───┼─────→│  RX:  GPIO16    ││
│  │  - RX:    P9.11 (GPIO30) │←──┼──────┤  TX:  GPIO17    ││
│  └──────────────────────────┘   │      └──│───────────────┘│
│                                  │         │                 │
│  GND (P9.1, P9.2, P9.43, P9.44)  ├─────────┤  GND           │
└─────────────────────────────────┘         └────────────────┘
                                                    │
                                                    ▼
                                            ┌───────────────┐
                                            │   Bluetooth   │
                                            │    Speaker    │
                                            └───────────────┘
```

**What This Guide Covers:**
1. Hardware components and requirements
2. Device Tree overlay configuration (McASP I2S, UART)
3. Physical wiring (I2S, UART, power, ground)
4. Hardware verification procedures
5. Common troubleshooting

**Related Documentation:**
- [MILESTONE1_HARDWARE_SETUP_BBGW.md](MILESTONE1_HARDWARE_SETUP_BBGW.md) - Detailed I2S setup
- [MILESTONE2_HARDWARE_SETUP_BBGW.md](MILESTONE2_HARDWARE_SETUP_BBGW.md) - Detailed UART setup
- [MILESTONE3_HARDWARE_SETUP_BBGW.md](MILESTONE3_HARDWARE_SETUP_BBGW.md) - Web UI network setup
- [SOFTWARE_SETUP_BBGW.md](SOFTWARE_SETUP_BBGW.md) - Software installation guide

---

## Table of Contents

1. [Hardware Components](#hardware-components)
2. [Device Tree Configuration](#device-tree-configuration)
3. [Physical Wiring](#physical-wiring)
4. [Hardware Verification](#hardware-verification)
5. [Troubleshooting](#troubleshooting)
6. [Pin Reference](#pin-reference)
7. [Success Criteria](#success-criteria)

---

## Hardware Components

### Required Components

#### BeagleBone Green Wireless
- **Model:** SeeedStudio BeagleBone Green Wireless
- **Processor:** AM335x ARM Cortex-A8 @ 1 GHz
- **RAM:** 512 MB DDR3
- **Operating System:** Debian 11 (Bullseye) or later
- **Kernel:** Linux 5.10+ (for McASP support)
- **Storage:** microSD card (8 GB+ recommended)
- **Power:** 5V @ 1A minimum (USB or barrel jack)
  - **Important:** Use quality power supply for stable operation

#### ESP32 Development Board
- **Model:** ESP32-DevKitC or compatible
- **Firmware:** `esp_bt_audio_source` (from this repository)
- **I2S Configuration:** Slave mode
  - BCK (Bit Clock): GPIO26
  - WS (Word Select / LRCLK): GPIO25
  - DIN (Data In): GPIO22
- **UART Configuration:**
  - RX: GPIO16 (receives from BBGW TX)
  - TX: GPIO17 (transmits to BBGW RX)
  - Baud rate: 115200 bps
- **Power:** USB or 3.3V external supply

#### Bluetooth Speaker
- **Requirements:**
  - Bluetooth Classic (A2DP) support
  - Paired with ESP32
  - Powered on and within range (~10 meters)
- **Recommended:** Speaker with visual BT connection indicator

#### Wiring and Accessories
- **Jumper Wires:** Female-to-male (BBGW to ESP32)
  - Minimum 7 wires (3 I2S + 2 UART + 2 GND)
  - Recommended: 10+ wires for redundant GND connections
- **Breadboard (optional):** For organizing connections
- **Logic Analyzer (optional):** For I2S signal verification
  - Recommended: 8-channel, 24+ MHz sampling rate

### Power Supply Recommendations

**BeagleBone Green Wireless:**
- **USB Power:** 5V @ 1A minimum
  - Sufficient for basic operation
  - May cause instability under heavy I2S load
- **Barrel Jack Power (recommended):** 5V @ 2A
  - Better for continuous I2S operation
  - Reduces risk of brownouts
- **Power Quality:** Use regulated supply; avoid USB hubs

**ESP32:**
- **USB Power:** 5V via USB port (simplest)
- **External 3.3V:** Direct to 3.3V pin (requires regulated supply)
- **Important:** Do NOT exceed 3.6V on ESP32 pins

---

## Device Tree Configuration

BeagleBone Green Wireless requires Device Tree overlays to enable McASP I2S and UART peripherals. This section covers overlay installation and configuration.

### Overview

**Device Tree Overlays Needed:**
1. **BB-BBGW-I2S-00A0.dtbo** - Enables McASP I2S on P9.31/29/28
2. **BB-BBGW-UART4-00A0.dtbo** - Enables UART4 on P9.13/11

**Overlay Location:** `/lib/firmware/`

**Configuration File:** `/boot/uEnv.txt`

### Step 1: Check Existing Overlays

```bash
# List available overlays
ls /lib/firmware/BB-*.dtbo

# Check if required overlays exist
ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
ls -l /lib/firmware/BB-BBGW-UART4-00A0.dtbo
```

### Step 2: Install Device Tree Overlays

If overlays don't exist, they should be provided in this repository or compiled from source.

**Option A: Use Pre-compiled Overlays (recommended)**

```bash
# Copy overlays from repository
sudo cp overlays/BB-BBGW-I2S-00A0.dtbo /lib/firmware/
sudo cp overlays/BB-BBGW-UART4-00A0.dtbo /lib/firmware/

# Set permissions
sudo chmod 644 /lib/firmware/BB-BBGW-*.dtbo
```

**Option B: Compile from Source (advanced)**

See [MILESTONE1_HARDWARE_SETUP_BBGW.md](MILESTONE1_HARDWARE_SETUP_BBGW.md#device-tree-overlay-setup) for detailed compilation instructions.

### Step 3: Configure /boot/uEnv.txt

```bash
# Edit uEnv.txt
sudo nano /boot/uEnv.txt
```

**Add these lines:**

```bash
# Enable McASP I2S overlay (for I2S audio output)
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Enable UART4 overlay (for ESP32 communication)
uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
```

**Important Notes:**
- Use `uboot_overlay_addr4` and `uboot_overlay_addr5` (or next available addresses)
- Check for existing overlays to avoid conflicts
- Do NOT use `disable_uboot_overlay_video=1` unless you don't need HDMI

**Save and Exit:** Ctrl+X, Y, Enter

### Step 4: Reboot and Verify

```bash
# Reboot to apply overlays
sudo reboot
```

**After reboot, verify overlays loaded:**

```bash
# Check I2S (McASP) device
aplay -l

# Expected output:
# card 0: BBGWI2S [BBGW-I2S], device 0: davinci-mcasp.0-i2s-hifi i2s-hifi-0 []

# Check UART4 device
ls -l /dev/ttyO4

# Expected output:
# crw-rw---- 1 root dialout 247, 4 Feb  7 04:00 /dev/ttyO4

# Check kernel messages
dmesg | grep -i mcasp
dmesg | grep ttyO4
```

### Device Tree Troubleshooting

**Problem:** `aplay -l` shows no BBGW-I2S device

**Solution:**
1. Check overlay loaded: `dmesg | grep -i mcasp`
2. Verify overlay file exists: `ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo`
3. Check /boot/uEnv.txt syntax (no typos)
4. Ensure no pin conflicts with other overlays

**Problem:** `/dev/ttyO4` not found

**Solution:**
1. Check overlay loaded: `dmesg | grep ttyO4`
2. Verify overlay in /boot/uEnv.txt
3. Check for UART conflicts (only one UART per pin pair)

**Problem:** "Failed to load overlay" in dmesg

**Solution:**
1. Recompile overlay: `dtc -O dtb -o /lib/firmware/BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts`
2. Check pin conflicts: `cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins`
3. Review kernel logs: `dmesg | tail -50`

---

## Physical Wiring

### I2S Connections (BBGW → ESP32)

**Pin Mapping:**

| Signal | BBGW Pin | BBGW GPIO | ESP32 Pin | Notes |
|--------|----------|-----------|-----------|-------|
| **BCLK** (Bit Clock) | P9.31 | GPIO110 | GPIO26 | I2S bit clock (1.536 MHz @ 48 kHz) |
| **WS** (Word Select) | P9.29 | GPIO111 | GPIO25 | Left/Right channel select (48 kHz) |
| **DOUT** (Data Out) | P9.28 | GPIO112 | GPIO22 | I2S audio data (BBGW → ESP32) |
| **GND** (Ground) | P9.1, P9.2 | GND | GND | Common ground (use 2 wires minimum) |

**Wiring Instructions:**

1. **BBGW P9.31 → ESP32 GPIO26** (BCLK)
   - Use female-to-male jumper wire
   - Ensure good contact on both ends

2. **BBGW P9.29 → ESP32 GPIO25** (WS / LRCLK)
   - Keep wire length similar to BCLK wire
   - Minimize length differences to reduce skew

3. **BBGW P9.28 → ESP32 GPIO22** (DOUT / DATA)
   - Data line; ensure good signal integrity
   - Keep away from power wires to reduce noise

4. **BBGW GND → ESP32 GND** (Ground)
   - **Critical:** Use at least 2 ground wires for stable I2S
   - BBGW ground pins: P9.1, P9.2, P9.43, P9.44
   - ESP32 ground pins: Multiple GND pins available

**I2S Wiring Diagram:**

```
BeagleBone P9 Header          ESP32 DevKitC
┌────────────────┐            ┌────────────────┐
│ P9.31 (BCLK)───┼───────────→│ GPIO26 (BCK)   │
│                │            │                │
│ P9.29 (WS)  ───┼───────────→│ GPIO25 (WS)    │
│                │            │                │
│ P9.28 (DOUT)───┼───────────→│ GPIO22 (DOUT)  │
│                │            │                │
│ P9.1  (GND)────┼────────────│ GND            │
│ P9.2  (GND)────┼────────────│ GND            │
└────────────────┘            └────────────────┘
```

### UART Connections (BBGW ↔ ESP32)

**Pin Mapping:**

| Signal | BBGW Pin | BBGW GPIO | ESP32 Pin | Notes |
|--------|----------|-----------|-----------|-------|
| **TX** (Transmit) | P9.13 | GPIO31 | GPIO16 (RX) | BBGW transmits to ESP32 |
| **RX** (Receive) | P9.11 | GPIO30 | GPIO17 (TX) | BBGW receives from ESP32 |
| **GND** (Ground) | P9.1, P9.2 | GND | GND | Shared with I2S ground |

**Wiring Instructions:**

1. **BBGW P9.13 (TX) → ESP32 GPIO16 (RX)**
   - **Critical:** TX on BBGW connects to RX on ESP32 (crossover)
   - 3.3V logic level compatible

2. **BBGW P9.11 (RX) → ESP32 GPIO17 (TX)**
   - **Critical:** RX on BBGW connects to TX on ESP32 (crossover)
   - 3.3V logic level compatible

3. **Common Ground**
   - Share ground with I2S connections
   - Total of 2-3 ground wires recommended

**UART Wiring Diagram:**

```
BeagleBone P9 Header          ESP32 DevKitC
┌────────────────┐            ┌────────────────┐
│ P9.13 (TX)─────┼───────────→│ GPIO16 (RX)    │
│                │            │                │
│ P9.11 (RX)←────┼────────────│ GPIO17 (TX)    │
│                │            │                │
│ P9.1  (GND)────┼────────────│ GND            │
└────────────────┘            └────────────────┘
```

### Complete Wiring Checklist

Use this checklist to verify all connections:

- [ ] **I2S Connections:**
  - [ ] P9.31 → GPIO26 (BCLK)
  - [ ] P9.29 → GPIO25 (WS)
  - [ ] P9.28 → GPIO22 (DOUT)

- [ ] **UART Connections:**
  - [ ] P9.13 → GPIO16 (TX → RX crossover)
  - [ ] P9.11 ← GPIO17 (RX ← TX crossover)

- [ ] **Ground Connections:**
  - [ ] At least 2 ground wires between BBGW and ESP32
  - [ ] Ground wires distributed (not all from same pin)

- [ ] **Power:**
  - [ ] BBGW powered (5V barrel jack or USB)
  - [ ] ESP32 powered (USB or 3.3V)
  - [ ] Both devices powered on

- [ ] **Bluetooth:**
  - [ ] Bluetooth speaker powered on
  - [ ] Speaker paired with ESP32
  - [ ] Speaker within range (~10 meters)

### Wiring Best Practices

1. **Wire Length:**
   - Keep I2S wires as short as practical (<30 cm ideal)
   - Similar lengths for BCLK, WS, DOUT to minimize skew
   - UART wires can be longer (<1 meter for 115200 baud)

2. **Wire Routing:**
   - Keep I2S wires away from power lines
   - Avoid running wires parallel to USB cables
   - Use twisted pairs for I2S signals if possible

3. **Mechanical Stability:**
   - Secure jumper wires to prevent accidental disconnection
   - Use breadboard or terminal blocks for permanent setups
   - Strain relief for wires near connectors

4. **Ground Strategy:**
   - Use multiple ground connections (2-3 wires)
   - Distribute grounds across different BBGW ground pins
   - Star ground topology preferred (all grounds meet at one point)

---

## Hardware Verification

After wiring, verify hardware configuration before running tests.

### Step 1: Verify Device Tree Overlays

```bash
# Check ALSA I2S device
aplay -l

# Expected output:
# card 0: BBGWI2S [BBGW-I2S], device 0: ...

# Check UART device
ls -l /dev/ttyO4

# Expected output:
# crw-rw---- 1 root dialout 247, 4 ... /dev/ttyO4
```

### Step 2: Test I2S Output (Loopback Not Possible)

**Note:** BeagleBone McASP doesn't support loopback mode. Use logic analyzer or ESP32 to verify I2S output.

**Quick Test with aplay:**

```bash
# Generate test tone file
speaker-test -t sine -f 1000 -c 2 -D hw:0,0 -r 48000 -F S16_LE -l 1 > /tmp/test.wav

# Play via I2S
aplay -D hw:0,0 -r 48000 -f S16_LE -c 2 /tmp/test.wav
```

**Expected:** No errors; ESP32 should receive I2S data.

### Step 3: Test UART Communication

**Loopback Test (BBGW side only):**

```bash
# Install minicom if needed
sudo apt-get install -y minicom

# Configure minicom for UART4
sudo minicom -D /dev/ttyO4 -b 115200

# Type characters; you should see them echoed back (if loopback wire installed)
# If ESP32 connected, you'll see ESP32 responses
```

**Python UART Test:**

```bash
# Add user to dialout group (if not already)
sudo usermod -a -G dialout $USER
# Log out and back in for group to take effect

# Run UART test
python3 -c "
import serial
ser = serial.Serial('/dev/ttyO4', 115200, timeout=1)
print('UART opened:', ser.name)
ser.write(b'STATUS\\n')
response = ser.readline()
print('Response:', response)
ser.close()
"
```

**Expected:** Response from ESP32 (e.g., `OK` or status JSON).

### Step 4: Verify ESP32 Firmware

```bash
# Check ESP32 serial output (if USB-to-serial adapter connected)
# Or use UART test above to query status

# Expected ESP32 behavior:
# - Bluetooth initialized
# - I2S slave mode active
# - Waiting for UART commands
```

### Step 5: End-to-End System Test

```bash
# Run Milestone 1 test (I2S tone generation)
cd ~/bbgw_i2s_source
./milestone1_test.py

# Expected output:
# [PASS] 1 kHz tone generated
# [PASS] I2S transmission active
# [PASS] Tone audible on Bluetooth speaker
```

---

## Troubleshooting

### I2S Issues

#### Problem: No I2S device (aplay -l shows nothing)

**Symptoms:**
```bash
$ aplay -l
aplay: device_list:274: no soundcards found...
```

**Solutions:**

1. **Check Device Tree overlay loaded:**
   ```bash
   dmesg | grep -i mcasp
   # Should show McASP initialization messages
   ```

2. **Verify overlay file exists:**
   ```bash
   ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

3. **Check /boot/uEnv.txt configuration:**
   ```bash
   grep "BB-BBGW-I2S" /boot/uEnv.txt
   # Should show: uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

4. **Reboot and re-check:**
   ```bash
   sudo reboot
   ```

#### Problem: I2S signals not visible on logic analyzer

**Symptoms:**
- Logic analyzer shows flat lines on BCLK, WS, DOUT
- No activity when playing audio

**Solutions:**

1. **Verify I2S is active:**
   ```bash
   aplay -D hw:0,0 -r 48000 -f S16_LE -c 2 /tmp/test.wav
   ```

2. **Check pin connections:**
   - Verify P9.31, P9.29, P9.28 connected to logic analyzer
   - Check ground connection between BBGW and analyzer

3. **Verify pin muxing:**
   ```bash
   cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins | grep -E "pin 104|pin 105|pin 103"
   # Should show McASP pins configured
   ```

#### Problem: Distorted or garbled audio

**Symptoms:**
- Audio plays but sounds wrong
- Clicking, popping, or noise

**Solutions:**

1. **Check sample rate mismatch:**
   - Ensure BBGW and ESP32 both use 48 kHz
   - Update config.yaml: `sample_rate: 48000`

2. **Check buffer underruns:**
   ```bash
   # Monitor ALSA errors
   dmesg | tail -50 | grep -i underrun
   ```

3. **Reduce CPU load:**
   ```bash
   # Stop unnecessary services
   sudo systemctl stop bluetooth  # If not using onboard BT
   ```

4. **Increase I2S buffer size:**
   - Edit config.yaml: `buffer_size: 8192` (increase from 4096)

### UART Issues

#### Problem: /dev/ttyO4 not found

**Symptoms:**
```bash
$ ls /dev/ttyO4
ls: cannot access '/dev/ttyO4': No such file or directory
```

**Solutions:**

1. **Check Device Tree overlay loaded:**
   ```bash
   dmesg | grep ttyO4
   # Should show UART4 registration
   ```

2. **Verify overlay in /boot/uEnv.txt:**
   ```bash
   grep "BB-BBGW-UART4" /boot/uEnv.txt
   ```

3. **Reboot and re-check:**
   ```bash
   sudo reboot
   ```

#### Problem: Permission denied on /dev/ttyO4

**Symptoms:**
```bash
$ python3 uart_test.py
PermissionError: [Errno 13] Permission denied: '/dev/ttyO4'
```

**Solutions:**

1. **Add user to dialout group:**
   ```bash
   sudo usermod -a -G dialout $USER
   # Log out and back in
   ```

2. **Verify group membership:**
   ```bash
   groups $USER
   # Should include "dialout"
   ```

3. **Temporary workaround (not recommended):**
   ```bash
   sudo chmod 666 /dev/ttyO4
   ```

#### Problem: No response from ESP32 via UART

**Symptoms:**
- UART opens successfully
- Commands sent, but no responses received

**Solutions:**

1. **Verify TX/RX crossover:**
   - BBGW TX (P9.13) → ESP32 RX (GPIO16)
   - BBGW RX (P9.11) → ESP32 TX (GPIO17)

2. **Check baud rate:**
   - Both BBGW and ESP32 must use 115200 baud
   - Update config.yaml: `uart.baud_rate: 115200`

3. **Test with loopback:**
   - Connect P9.13 to P9.11 (BBGW TX to RX)
   - Send data; should receive same data back

4. **Check ESP32 firmware:**
   - Verify ESP32 firmware running
   - Check ESP32 serial output for errors

### General Issues

#### Problem: Inconsistent behavior (works sometimes)

**Solutions:**

1. **Check power supply:**
   - Use 5V @ 2A supply for BBGW (not USB hub)
   - Monitor voltage: `cat /sys/class/power_supply/*/voltage_now`

2. **Check wiring:**
   - Wiggle wires to detect loose connections
   - Re-seat all jumper wires

3. **Check CPU throttling:**
   ```bash
   cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
   # Should be "performance" or "ondemand"
   ```

#### Problem: System freezes during I2S operation

**Solutions:**

1. **Check CPU usage:**
   ```bash
   top
   # Look for high CPU usage
   ```

2. **Reduce I2S activity:**
   - Lower sample rate to 44100 Hz
   - Reduce buffer size if using large buffers

3. **Update kernel:**
   ```bash
   sudo apt-get update
   sudo apt-get upgrade
   ```

---

## Pin Reference

### BeagleBone Green Wireless P9 Header

**I2S Pins (McASP):**
| Pin | Function | GPIO | Signal | Direction |
|-----|----------|------|--------|-----------|
| P9.31 | McASP0_ACLKX | GPIO110 | BCLK | Output (Master) |
| P9.29 | McASP0_FSX | GPIO111 | WS / LRCLK | Output (Master) |
| P9.28 | McASP0_AXR2 | GPIO112 | DOUT / DATA | Output (TX) |

**UART4 Pins:**
| Pin | Function | GPIO | Signal | Direction |
|-----|----------|------|--------|-----------|
| P9.13 | UART4_TX | GPIO31 | TX | Output (Transmit) |
| P9.11 | UART4_RX | GPIO30 | RX | Input (Receive) |

**Ground Pins:**
| Pin | Function |
|-----|----------|
| P9.1 | DGND (Digital Ground) |
| P9.2 | DGND (Digital Ground) |
| P9.43 | GND |
| P9.44 | GND |

### ESP32 DevKitC Pins

**I2S Pins:**
| GPIO | Function | Signal | Direction |
|------|----------|--------|-----------|
| GPIO26 | I2S BCK | BCLK | Input (Slave) |
| GPIO25 | I2S WS | WS / LRCLK | Input (Slave) |
| GPIO22 | I2S DIN | DATA IN | Input (RX) |

**UART Pins:**
| GPIO | Function | Signal | Direction |
|------|----------|--------|-----------|
| GPIO16 | UART RX | RX | Input (Receive) |
| GPIO17 | UART TX | TX | Output (Transmit) |

**Power Pins:**
| Pin | Voltage | Notes |
|-----|---------|-------|
| 5V | 5V | From USB; not regulated |
| 3V3 | 3.3V | Regulated output; max 600 mA |
| GND | 0V | Multiple pins available |

---

## Success Criteria

Hardware setup is complete when all of the following criteria are met:

### Device Tree Overlays

- [ ] `aplay -l` shows BBGW-I2S card 0
- [ ] `/dev/ttyO4` exists with correct permissions
- [ ] `dmesg | grep mcasp` shows no errors
- [ ] `dmesg | grep ttyO4` shows UART4 registered

### Physical Wiring

- [ ] I2S connections verified (BCLK, WS, DOUT)
- [ ] UART connections verified (TX→RX, RX←TX crossover)
- [ ] At least 2 ground wires between BBGW and ESP32
- [ ] All connections secure and insulated

### Hardware Verification

- [ ] I2S signals visible on logic analyzer (if available)
  - BCLK: 1.536 MHz square wave @ 48 kHz
  - WS: 48 kHz square wave
  - DOUT: Data toggles during audio playback
- [ ] UART communication working (loopback or ESP32 response)
- [ ] ESP32 firmware running and responding

### End-to-End Test

- [ ] Milestone 1 test passes (I2S tone generation)
- [ ] 1 kHz tone audible on Bluetooth speaker
- [ ] No buffer underruns (<0.1% threshold)
- [ ] System stable for 5+ minutes continuous operation

### Power and Stability

- [ ] BBGW powered with stable 5V supply
- [ ] ESP32 powered and not browning out
- [ ] No system freezes or crashes
- [ ] CPU usage <50% during I2S operation

---

## Next Steps

After completing hardware setup:

1. **Software Setup:**
   - See [SOFTWARE_SETUP_BBGW.md](SOFTWARE_SETUP_BBGW.md) for software installation

2. **Testing:**
   - Run Milestone 1: I2S tone generation test
   - Run Milestone 2: UART command interface test
   - Run Milestone 3: Web UI test
   - Run integration tests (5-30 minutes)

3. **Optimization:**
   - Tune I2S buffer sizes for your use case
   - Adjust CPU governor for performance vs. power
   - Configure web UI for LAN access

4. **Deployment:**
   - Set up systemd services for automatic startup
   - Configure network (Wi-Fi, static IP)
   - Enable logging and monitoring

---

## Additional Resources

### Official Documentation
- [BeagleBone Green Wireless Wiki](https://wiki.seeedstudio.com/BeagleBone_Green_Wireless/)
- [AM335x Technical Reference Manual](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
- [McASP User Guide](https://www.ti.com/lit/ug/sprugm2a/sprugm2a.pdf)

### Community Resources
- [BeagleBoard Forum](https://forum.beagleboard.org/)
- [ESP32 I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)

### Project Documentation
- [Architecture Document](../ARCH.md)
- [Migration Guide](../MIGRATION.md)
- [Milestone Testing](README_TESTS.md)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
