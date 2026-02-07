# Milestone 1: Hardware Setup Guide — BeagleBone Green Wireless

**Goal:** Generate a 1 kHz tone on BeagleBone Green Wireless and transmit it to ESP32 via McASP I2S interface, with audio playback on a Bluetooth speaker.

**Deliverables:**
1. ✅ Python script generates 1 kHz sine tone
2. ✅ McASP I2S master transmits tone to P9.31/29/28
3. ✅ Continuous playback for 5+ minutes without dropouts
4. ✅ Logic analyzer verification (optional)

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Hardware Components](#hardware-components)
3. [Device Tree Overlay Setup](#device-tree-overlay-setup)
4. [Physical Wiring](#physical-wiring)
5. [Verification Steps](#verification-steps)
6. [Running Milestone 1 Test](#running-milestone-1-test)
7. [Logic Analyzer Verification](#logic-analyzer-verification)
8. [Troubleshooting](#troubleshooting)
9. [Success Criteria](#success-criteria)

---

## Prerequisites

### Software Requirements

- BeagleBone Green Wireless running Debian Linux (tested on Debian 11/Bullseye)
- Python 3.7+ installed
- ALSA utilities (`alsa-utils` package)
- Device Tree compiler (`device-tree-compiler` package)
- ESP32 running `esp_bt_audio_source` firmware
- Bluetooth speaker paired with ESP32

### Install Required Packages

```bash
sudo apt-get update
sudo apt-get install -y \
    python3 \
    python3-pip \
    alsa-utils \
    device-tree-compiler \
    git
```

### Python Dependencies

```bash
cd ~/bbgw_i2s_source
pip3 install -r requirements.txt
```

**Key dependencies:**
- `pyalsaaudio` — ALSA audio interface
- `numpy` — Audio sample generation
- `scipy` — Signal processing
- `pyyaml` — Configuration management

---

## Hardware Components

### Required Hardware

1. **BeagleBone Green Wireless**
   - AM335x SoC with McASP0 peripheral
   - Built-in Wi-Fi for network access
   - P9 header with McASP pins exposed

2. **ESP32 Development Board**
   - Running `esp_bt_audio_source` firmware
   - I2S slave mode configured
   - Bluetooth Classic enabled

3. **Bluetooth Speaker**
   - Paired with ESP32
   - Supports A2DP profile

4. **Connecting Wires**
   - 4× male-to-male jumper wires (I2S + GND)
   - Recommended: breadboard for stable connections

5. **Power Supplies**
   - 5V power for BeagleBone (USB or barrel jack)
   - 5V power for ESP32 (USB or external)
   - **Important:** Use separate power supplies, common ground only

### Optional Equipment

- **Logic Analyzer** (Saleae Logic, DSLogic, etc.)
  - For I2S signal verification
  - 3 channels minimum (BCLK, WS, DOUT)
  - 10+ MHz sampling rate recommended

- **Oscilloscope**
  - Alternative to logic analyzer
  - Verify signal integrity and timing

---

## Device Tree Overlay Setup

### Step 1: Compile McASP I2S Overlay

The BeagleBone Green Wireless requires a Device Tree overlay to configure McASP0 pins for I2S operation.

```bash
cd ~/bbgw_i2s_source/overlays

# Compile both overlays
./compile_overlays.sh --all

# Or compile just the full overlay
./compile_overlays.sh --full
```

**Expected output:**
```
Compiling BB-BBGW-I2S-00A0.dts...
  ✓ Compiled BB-BBGW-I2S-00A0.dtbo
```

### Step 2: Install Overlay to Firmware Directory

```bash
sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
```

### Step 3: Enable Overlay in `/boot/uEnv.txt`

Edit `/boot/uEnv.txt`:

```bash
sudo nano /boot/uEnv.txt
```

Find the line:
```bash
#uboot_overlay_addr4=/lib/firmware/<file4>.dtbo
```

Change it to:
```bash
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
```

**Save and exit** (Ctrl+X, Y, Enter)

### Step 4: Reboot BeagleBone

```bash
sudo reboot
```

### Step 5: Verify Overlay Loaded

After reboot, verify the overlay loaded successfully:

```bash
# Check kernel messages for McASP
dmesg | grep -i mcasp

# Expected output (example):
# [    2.123456] davinci-mcasp 48038000.mcasp: Configuring McASP in I2S mode
# [    2.234567] asoc-simple-card sound: davinci-mcasp-i2s <-> 48038000.mcasp mapping ok
```

```bash
# Check ALSA devices
aplay -l

# Expected output:
# card 0: BBGWI2S [BBGW-I2S], device 0: davinci-mcasp-i2s snd-soc-dummy-dai-0 [davinci-mcasp-i2s snd-soc-dummy-dai-0]
#   Subdevices: 1/1
#   Subdevice #0: subdevice #0
```

### Step 6: Verify McASP Configuration

Use the verification script:

```bash
cd ~/bbgw_i2s_source/overlays
./verify_mcasp.sh --verbose
```

**Expected:** All 6 checks should pass ✓

---

## Physical Wiring

### I2S Pin Connections

Connect BeagleBone Green Wireless P9 header to ESP32:

| BBGW Pin | Function | Signal | ESP32 GPIO | ESP32 Function |
|----------|----------|--------|------------|----------------|
| **P9.31** | McASP0_ACLKX | BCLK (Bit Clock) | **GPIO 26** | I2S_BCK |
| **P9.29** | McASP0_FSX | WS/LRCLK (Word Select) | **GPIO 25** | I2S_WS |
| **P9.28** | McASP0_AXR1 | DOUT (Data Out) | **GPIO 22** | I2S_DATA_IN |
| **P9.1** or **P9.2** | DGND | Ground | **GND** | Ground |

### Wiring Diagram (ASCII)

```
BeagleBone Green Wireless                        ESP32
P9 Header                                        DevKit v1

    P9.31 (BCLK/ACLKX) ──────────────────────> GPIO 26 (I2S_BCK)
    P9.29 (WS/FSX)     ──────────────────────> GPIO 25 (I2S_WS)
    P9.28 (DOUT/AXR1)  ──────────────────────> GPIO 22 (I2S_DATA_IN)
    P9.1  (GND)        ──────────────────────> GND

    (5V USB Power)                                (5V USB Power)
         |                                              |
         └──────── Common Ground (P9.1 ↔ ESP32 GND) ───┘
```

### Wiring Notes

1. **Signal Direction:**
   - BCLK, WS, DOUT are **outputs** from BBGW (McASP master mode)
   - ESP32 is in **I2S slave mode** (receives clock and data from BBGW)

2. **Voltage Levels:**
   - Both BBGW (3.3V I/O) and ESP32 (3.3V I/O) are compatible
   - **No level shifters required**

3. **Wire Length:**
   - Keep wires **< 30 cm** (< 12 inches) for reliable high-speed I2S signals
   - Use twisted pairs for BCLK/WS if possible (reduces crosstalk)

4. **Power:**
   - Use **separate 5V power supplies** for BBGW and ESP32
   - **Connect grounds together** (P9.1 or P9.2 to ESP32 GND)
   - Never backfeed power through I2S pins

5. **Pull-ups/Pull-downs:**
   - Not required for I2S signals (driven actively by McASP)
   - Device Tree overlay configures pins as outputs

---

## Verification Steps

### Step 1: Verify Device Tree Overlay

```bash
# Check if overlay is loaded
cat /sys/firmware/devicetree/base/chosen/overlays/BB-BBGW-I2S-00A0 2>/dev/null && echo "✓ Overlay loaded" || echo "✗ Overlay not loaded"
```

### Step 2: Verify ALSA Device

```bash
# List ALSA playback devices
aplay -L | grep -i bbgw

# Expected output:
# hw:CARD=BBGWI2S,DEV=0
#     BBGW-I2S, davinci-mcasp-i2s snd-soc-dummy-dai-0
#     Direct hardware device without any conversions
```

```bash
# Test ALSA device with speaker-test (optional)
# This generates a test tone via ALSA (should appear on ESP32 I2S input)
speaker-test -D hw:CARD=BBGWI2S,DEV=0 -c 2 -r 48000 -f S16_LE -t sine -l 1
```

**Expected:** No errors, tone audible on Bluetooth speaker if ESP32 is running

### Step 3: Verify Pin Mux Configuration

Check that P9.31, P9.29, P9.28 are configured for McASP:

```bash
# Check pin mux (requires debugfs)
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -E "(990|994|99c)"

# Expected output (example):
# pin 100 (PIN100): 48038000.mcasp (GPIO UNCLAIMED) function mcasp0_group group mcasp0_pins_default
# pin 101 (PIN101): 48038000.mcasp (GPIO UNCLAIMED) function mcasp0_group group mcasp0_pins_default
# pin 103 (PIN103): 48038000.mcasp (GPIO UNCLAIMED) function mcasp0_group group mcasp0_pins_default
```

### Step 4: Verify McASP Driver

```bash
# Check McASP driver module is loaded
lsmod | grep snd_soc

# Expected output includes:
# snd_soc_davinci_mcasp
# snd_soc_simple_card
```

```bash
# Check dmesg for McASP initialization
dmesg | grep -i mcasp | tail -10
```

---

## Running Milestone 1 Test

### Step 1: Prepare Configuration

Create or verify `config.yaml`:

```bash
cd ~/bbgw_i2s_source

# If config.yaml doesn't exist, create from template
if [ ! -f config.yaml ]; then
    cp config.yaml.template config.yaml
fi
```

**Verify I2S configuration in `config.yaml`:**

```yaml
i2s:
  device: "hw:CARD=BBGW-I2S,DEV=0"  # ALSA device from Device Tree
  sample_rate: 48000
  channels: 2
  format: "S16_LE"
  period_size: 1024
  buffer_size: 4096
```

### Step 2: Run Short Test (60 seconds)

```bash
cd ~/bbgw_i2s_source
python3 milestone1_tone_test.py
```

**Expected output:**

```
======================================================================
Milestone 1: Basic I2S Tone Generation Test (BBGW)
======================================================================

Configuration:
  Sample Rate:    48000 Hz
  Channels:       2
  Format:         S16_LE
  ALSA Device:    hw:CARD=BBGW-I2S,DEV=0
  Tone Frequency: 1000 Hz
  Tone Amplitude: 0.5
  Test Duration:  60 seconds

I2S McASP Pins (BeagleBone P9 Header):
  BCLK (ACLKX): P9.31 → ESP32 GPIO 26
  WS (FSX):     P9.29 → ESP32 GPIO 25
  DOUT (AXR1):  P9.28 → ESP32 GPIO 22
  GND:          P9.1  → ESP32 GND

Expected I2S Signals:
  BCLK: 1.536 MHz (48 kHz × 32 bits)
  WS:   48 kHz (left/right channel clock)
  DOUT: 16-bit PCM sine wave data

----------------------------------------------------------------------

[1/3] Starting audio engine...
      ✓ Audio engine running (1 kHz tone generation)
[2/3] Starting I2S driver (McASP)...
      ✓ I2S driver running (ALSA/McASP transmission)
[3/3] Monitoring playback for 60 seconds...

Real-time Statistics:
----------------------------------------------------------------------
Time:   60.0s | Frames:    2880000 | Rate:   48000 fps | Buffer:  75.0% | Underruns:   0

----------------------------------------------------------------------
✓ Test completed successfully!

Final Statistics:
  Total Duration:  60.0 seconds
  Total Frames:    2,880,000
  Average Rate:    48000.0 frames/sec
  Expected Rate:   48000 frames/sec
  Total Underruns: 0

Milestone 1 Success Criteria:
  [MANUAL] Tone audible on Bluetooth speaker
  [  ✓  ] Zero I2S underruns: 0
  [  -  ] Playback duration: 60.0s (milestone requires 300s)

Next Steps:
  1. Verify BCLK/WS/DOUT with logic analyzer (if available)
  2. Confirm audio output on Bluetooth speaker (manual)
  3. Run full 5-minute test: python3 milestone1_tone_test.py --duration 300
  4. Verify McASP Device Tree overlay: ./overlays/verify_mcasp.sh
```

### Step 3: Manual Verification

**During the test:**
- ✅ **Listen for 1 kHz tone** on Bluetooth speaker
- ✅ **Check console output** for zero underruns
- ✅ **Verify stable frame rate** (~48000 fps)

### Step 4: Run Full 5-Minute Test

Once short test passes, run the full milestone test:

```bash
python3 milestone1_tone_test.py --duration 300
```

**Success criteria:**
- ✅ Continuous playback for 300+ seconds
- ✅ Zero I2S underruns
- ✅ 1 kHz tone audible throughout test

---

## Logic Analyzer Verification

### Optional: Verify I2S Signals with Logic Analyzer

If you have a logic analyzer, you can verify the I2S signals are correct.

### Setup

1. **Connect logic analyzer probes:**
   - **Channel 0:** P9.31 (BCLK)
   - **Channel 1:** P9.29 (WS)
   - **Channel 2:** P9.28 (DOUT)
   - **GND:** P9.1 or P9.2

2. **Logic analyzer settings:**
   - **Sample rate:** 24 MHz or higher (to capture 1.536 MHz BCLK)
   - **Trigger:** Rising edge on BCLK
   - **Duration:** 10-100 ms

### Expected Signals

**BCLK (Bit Clock):**
- **Frequency:** 1.536 MHz (48 kHz sample rate × 32 bits/frame)
- **Duty cycle:** 50%
- **Voltage:** 3.3V TTL (0V low, 3.3V high)

**WS (Word Select / LRCLK):**
- **Frequency:** 48 kHz (left/right channel clock)
- **Duty cycle:** 50% (low = left channel, high = right channel)
- **Voltage:** 3.3V TTL

**DOUT (Data Out):**
- **Format:** I2S (MSB-first, data valid on second BCLK edge after WS change)
- **Bit width:** 16 bits (S16_LE format)
- **Pattern:** Repeating sine wave samples (1 kHz tone)

### Verification Steps

1. **Capture I2S signals** during milestone test
2. **Measure BCLK frequency:** Should be 1.536 MHz ± 0.1%
3. **Measure WS frequency:** Should be 48 kHz ± 0.1%
4. **Verify I2S timing:** Data transitions 1 BCLK cycle after WS edge
5. **Decode DOUT:** Should show 16-bit signed PCM samples
6. **Screenshot/save capture** for documentation

---

## Troubleshooting

### Problem: "ALSA device not found"

**Symptoms:**
```
ALSA lib pcm.c:XXXX:snd_pcm_open_noupdate: Unknown PCM hw:CARD=BBGW-I2S,DEV=0
```

**Solutions:**

1. **Verify overlay is loaded:**
   ```bash
   aplay -l
   ```
   If `BBGW-I2S` is not listed, overlay did not load.

2. **Check `/boot/uEnv.txt` configuration:**
   ```bash
   grep "uboot_overlay_addr4" /boot/uEnv.txt
   ```
   Should show: `uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo`

3. **Verify overlay file exists:**
   ```bash
   ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

4. **Check dmesg for errors:**
   ```bash
   dmesg | grep -i mcasp
   dmesg | grep -i error
   ```

5. **Reboot and retry:**
   ```bash
   sudo reboot
   ```

### Problem: "No audio output / silent"

**Symptoms:**
- Test runs without errors
- Zero underruns
- But no audio on Bluetooth speaker

**Solutions:**

1. **Verify ESP32 is running:**
   - ESP32 should be powered on
   - `esp_bt_audio_source` firmware should be running
   - Check ESP32 serial output for I2S activity

2. **Verify Bluetooth pairing:**
   - Bluetooth speaker should be paired with ESP32
   - Check ESP32 Bluetooth connection status

3. **Check I2S wiring:**
   - BCLK: P9.31 → ESP32 GPIO 26
   - WS: P9.29 → ESP32 GPIO 25
   - DOUT: P9.28 → ESP32 GPIO 22
   - GND connected

4. **Verify signal levels with multimeter:**
   - BCLK, WS should toggle between 0V and ~3.3V
   - DOUT should toggle during playback

5. **Use logic analyzer** to verify I2S signals (see above section)

### Problem: "High underrun count"

**Symptoms:**
```
Underruns:  23
```

**Solutions:**

1. **Increase buffer size in `config.yaml`:**
   ```yaml
   i2s:
     buffer_size: 8192  # Increase from 4096
     period_size: 2048  # Increase from 1024
   ```

2. **Reduce CPU load:**
   - Stop other processes (web browser, etc.)
   - Check CPU usage: `top` or `htop`

3. **Verify ALSA driver:**
   ```bash
   dmesg | grep -i mcasp | grep -i error
   ```

4. **Check for USB/network interrupts:**
   - Try disconnecting USB peripherals
   - Disable Wi-Fi if not needed: `sudo ifconfig wlan0 down`

### Problem: "Frame rate unstable"

**Symptoms:**
```
Rate:   47856 fps  # Should be 48000
```

**Solutions:**

1. **Verify sample rate in config.yaml:**
   ```yaml
   i2s:
     sample_rate: 48000  # Must match Device Tree
   ```

2. **Check McASP clock configuration:**
   ```bash
   dmesg | grep -i mcasp | grep -i clock
   ```

3. **Verify Device Tree overlay** was compiled correctly:
   ```bash
   cd ~/bbgw_i2s_source/overlays
   ./compile_overlays.sh --full
   sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
   sudo reboot
   ```

---

## Success Criteria

### Milestone 1 is considered successful when:

- ✅ **Test script runs without errors**
  - AudioEngine starts successfully
  - I2S driver starts successfully
  - No Python exceptions

- ✅ **1 kHz tone audible on Bluetooth speaker**
  - Clear, continuous tone
  - No dropouts or glitches
  - Consistent volume

- ✅ **Zero I2S underruns** (or < 10 over 5 minutes)
  - `Total Underruns: 0` in final statistics
  - Indicates stable ALSA/McASP operation

- ✅ **Continuous playback ≥ 300 seconds**
  - Full 5-minute test completes
  - No interruptions or crashes

- ✅ **Frame rate matches expected** (48000 fps ± 1%)
  - `Average Rate: 48000.0 frames/sec`
  - Indicates correct sample rate configuration

- ✅ **Logic analyzer verification passes** (optional)
  - BCLK = 1.536 MHz
  - WS = 48 kHz
  - DOUT shows valid I2S data

---

## Next Steps

After Milestone 1 success:

1. ✅ **Document results** in `memory.md` and `TODO.md`
2. ✅ **Proceed to Milestone 2:** UART command interface testing
3. ✅ **Capture logic analyzer screenshots** (if available) for documentation
4. ✅ **Commit and push** Milestone 1 completion to GitHub

---

## References

- [BeagleBone Green Wireless Documentation](https://beagleboard.org/green-wireless)
- [AM335x Technical Reference Manual (McASP)](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
- [ALSA Documentation](https://www.alsa-project.org/wiki/Main_Page)
- [Device Tree Overlay Guide](https://elinux.org/Beagleboard:BeagleBoneBlack_Debian#Loading_custom_capes)
- [I2S Protocol Specification](https://www.nxp.com/docs/en/user-manual/UM10732.pdf)

---

**BeagleBone Green Wireless I2S Source Project**  
**Author:** BeagleBone Green Wireless I2S Audio Source Project  
**Date:** 2026-02-07
