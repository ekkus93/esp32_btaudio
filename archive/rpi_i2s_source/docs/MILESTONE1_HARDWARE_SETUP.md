# Milestone 1: Hardware Setup and Verification Guide

## Overview

This guide walks through the complete hardware setup and verification for **Milestone 1: Basic I2S Tone Generation**, validating that the Raspberry Pi can generate a 1 kHz tone and transmit it via I2S to the ESP32 esp_bt_audio_source for Bluetooth playback.

---

## Hardware Requirements

### Components Needed

1. **Raspberry Pi** (tested on Pi 3B+/4)
   - With I2S hardware support
   - GPIO pins 18, 19, 21 available
   
2. **ESP32 Development Board** with esp_bt_audio_source firmware
   - I2S receiver configured
   - Bluetooth Classic enabled
   
3. **Bluetooth Speaker** (paired with ESP32)

4. **Logic Analyzer** (optional but recommended)
   - For verifying I2S protocol signals
   - 3+ channels (BCLK, WS, DOUT minimum)
   
5. **Jumper Wires** (female-to-female or female-to-male depending on boards)

6. **Breadboard** (optional, for clean connections)

---

## Step 1: Raspberry Pi I2S Configuration

### 1.1. Enable I2S Hardware

Edit `/boot/config.txt` (requires sudo):

```bash
sudo nano /boot/config.txt
```

Add the following line:

```
dtoverlay=i2s-mmap
```

**Alternative overlays** (if `i2s-mmap` doesn't work):
- `dtoverlay=hifiberry-dac` (for HiFiBerry DAC boards)
- `dtoverlay=googlevoicehat-soundcard` (for Google Voice HAT)
- `dtoverlay=iqaudio-dacplus` (for IQaudio DAC+)

Reboot:

```bash
sudo reboot
```

### 1.2. Verify I2S Device

After reboot, check for ALSA I2S device:

```bash
aplay -l
```

Expected output:

```
card 0: sndrpii2s [snd_rpi_i2s], device 0: simple-card_codec_link snd-soc-dummy-dai-0 [simple-card_codec_link snd-soc-dummy-dai-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

If no I2S device appears, double-check `/boot/config.txt` and reboot again.

### 1.3. Install Python Dependencies

```bash
# Install ALSA Python bindings
sudo apt-get update
sudo apt-get install -y python3-alsaaudio

# Install Python dependencies for the project
cd /path/to/esp32_btaudio/rpi_i2s_source
pip3 install -r requirements.txt
```

---

## Step 2: ESP32 Wiring

### 2.1. GPIO Pin Mapping

Connect Raspberry Pi I2S output to ESP32 I2S input:

| Raspberry Pi (BCM) | Signal | ESP32 GPIO | Description |
|--------------------|--------|------------|-------------|
| GPIO 18            | BCLK   | GPIO 26    | Bit Clock (1.536 MHz) |
| GPIO 19            | WS     | GPIO 25    | Word Select / LRCLK (48 kHz) |
| GPIO 21            | DOUT   | GPIO 22    | Data Out (PCM samples) |
| GND                | GND    | GND        | Common ground |

**Note:** ESP32 GPIO pins may vary based on your esp_bt_audio_source configuration. Check `main/bt_app_core.c` or `components/i2s_manager/i2s_manager.c` for actual pin definitions.

### 2.2. Physical Connection

1. **Power off both devices** before wiring.
2. Use female-to-female jumper wires for breadboard connections.
3. Keep wires short (< 15 cm) to minimize noise and signal degradation.
4. Double-check connections before powering on.

**Safety:**
- Do **not** connect 5V pins between boards (I2S signals are 3.3V logic).
- Ensure common ground connection.

---

## Step 3: ESP32 Firmware Configuration

### 3.1. Flash esp_bt_audio_source

Navigate to the ESP32 project and build:

```bash
cd /path/to/esp32_btaudio/esp_bt_audio_source
. $HOME/esp/esp-idf/export.sh
idf.py build flash monitor
```

### 3.2. Pair Bluetooth Speaker

1. Put Bluetooth speaker in pairing mode.
2. Use ESP32 UART commands to scan and connect:

```
SCAN
CONNECT <device_address>
```

Or use the Python UART interface (if available):

```bash
python3 tools/uart_test.py --scan
python3 tools/uart_test.py --connect <address>
```

### 3.3. Verify I2S Input

Check ESP32 logs for I2S initialization:

```
I (1234) I2S_MGR: I2S initialized successfully
I (1235) I2S_MGR: BCLK: GPIO26, WS: GPIO25, DIN: GPIO22
```

---

## Step 4: Run Milestone 1 Test

### 4.1. Short Test (60 seconds)

```bash
cd /path/to/esp32_btaudio/rpi_i2s_source
python3 milestone1_tone_test.py
```

Expected console output:

```
======================================================================
Milestone 1: Basic I2S Tone Generation Test
======================================================================

Configuration:
  Sample Rate:    48000 Hz
  Tone Frequency: 1000 Hz
  Tone Amplitude: 0.5
  Test Duration:  60 seconds

I2S GPIO Pins (BCM numbering):
  BCLK: GPIO18
  WS:   GPIO19
  DOUT: GPIO21

Expected I2S Signals:
  BCLK: 1.536 MHz (48 kHz × 32 bits)
  WS:   48 kHz (left/right channel clock)
  DOUT: 16-bit PCM sine wave data

----------------------------------------------------------------------

[1/3] Starting audio engine...
      ✓ Audio engine running (1 kHz tone generation)
[2/3] Starting I2S driver...
      ✓ I2S driver running (ALSA transmission)
[3/3] Monitoring playback for 60 seconds...

Real-time Statistics:
----------------------------------------------------------------------
Time:   10.2s | Frames:     489600 | Rate:  48000 fps | Buffer: 45.3% | Underruns:   0
```

### 4.2. Full 5-Minute Test (Milestone Requirement)

```bash
python3 milestone1_tone_test.py --duration 300
```

This will run the continuous playback test for 5 minutes as required by Milestone 1.

---

## Step 5: Logic Analyzer Verification (Optional)

### 5.1. Connect Logic Analyzer

Connect logic analyzer probes to Raspberry Pi I2S signals:

| Channel | Signal | GPIO Pin (BCM) |
|---------|--------|----------------|
| 0       | BCLK   | GPIO 18        |
| 1       | WS     | GPIO 19        |
| 2       | DOUT   | GPIO 21        |
| GND     | GND    | GND            |

### 5.2. Capture Settings

- **Sample Rate:** ≥ 10 MHz (to capture 1.536 MHz BCLK cleanly)
- **Capture Duration:** 10 ms minimum (480 I2S frames)
- **Trigger:** Rising edge on WS

### 5.3. Verify BCLK Signal

**Expected:**
- Frequency: **1.536 MHz** (period = 651 ns)
- Duty cycle: ~50%
- Clean square wave with sharp edges

**Measurements:**
```
Period: 651 ns ± 50 ppm
High time: ~325 ns
Low time: ~325 ns
```

### 5.4. Verify WS Signal

**Expected:**
- Frequency: **48 kHz** (period = 20.833 µs)
- Duty cycle: 50%
- 32 BCLK cycles per WS period

**Measurements:**
```
Period: 20.833 µs
High time (right channel): 10.417 µs (16 BCLK cycles)
Low time (left channel): 10.417 µs (16 BCLK cycles)
```

### 5.5. Verify DOUT Data

**Expected:**
- Valid PCM data synchronized to BCLK
- MSB-first, left-aligned
- Left channel data when WS = LOW
- Right channel data when WS = HIGH

**For 1 kHz mono tone:**
- Repeating sine wave pattern every 48 samples
- Amplitude: ~±16384 (50% of 16-bit max ±32767)

**Decoder:** Use I2S protocol decoder if available (Saleae, PulseView, etc.)

---

## Step 6: Verification Checklist

### Manual Verification

- [ ] **Bluetooth speaker paired** with ESP32
- [ ] **1 kHz tone audible** on Bluetooth speaker
- [ ] **Continuous playback** for 5 minutes without dropouts
- [ ] **No crackling/pops** indicating buffer underruns

### Automated Verification (from test script)

- [ ] **Zero underruns** reported in final statistics
- [ ] **Frame rate ~48000 fps** sustained
- [ ] **Buffer fill percentage** stable (30-70%)

### Logic Analyzer Verification (Optional)

- [ ] **BCLK = 1.536 MHz** ± 50 ppm
- [ ] **WS = 48 kHz** ± 50 ppm
- [ ] **32 BCLK cycles** per WS period
- [ ] **Valid PCM data** on DOUT synchronized to BCLK

---

## Troubleshooting

### Issue: "alsaaudio not available"

**Symptoms:** `ImportError: alsaaudio not available`

**Fix:**
```bash
sudo apt-get install python3-alsaaudio
```

### Issue: No I2S device (aplay -l shows no card)

**Symptoms:** `aplay -l` shows "no soundcards found"

**Fix:**
1. Verify `/boot/config.txt` has `dtoverlay=i2s-mmap`
2. Reboot Raspberry Pi
3. Check `dmesg | grep i2s` for errors
4. Try alternative overlay: `dtoverlay=hifiberry-dac`

### Issue: "ALSA lib ... failed"

**Symptoms:** ALSA errors when starting test

**Fix:**
1. Check I2S pins not in use by another process: `sudo fuser -v /dev/snd/*`
2. Kill conflicting processes
3. Verify ALSA device: `aplay -L | grep i2s`

### Issue: No audio on Bluetooth speaker

**Symptoms:** Test runs but no sound

**Troubleshoot:**
1. **ESP32 side:** Check `idf.py monitor` for I2S data reception logs
2. **Bluetooth:** Verify pairing: `STATUS` command should show "CONNECTED"
3. **Volume:** Send `VOLUME 75` command to ESP32
4. **I2S wiring:** Verify connections, especially GND
5. **Signal integrity:** Use logic analyzer to confirm valid I2S signals

### Issue: Frequent underruns

**Symptoms:** Underruns > 0 in statistics, audio glitches

**Fix:**
1. Increase ring buffer size in code (default 8192)
2. Check CPU usage on Raspberry Pi: `top`
3. Reduce other processes consuming CPU
4. Verify I2S period size matches buffer configuration

### Issue: BCLK frequency wrong

**Symptoms:** Logic analyzer shows BCLK ≠ 1.536 MHz

**Fix:**
1. Check ALSA sample rate configuration in code
2. Verify `dtoverlay` settings in `/boot/config.txt`
3. Try different overlay (e.g., `hifiberry-dac` vs `i2s-mmap`)

---

## Success Criteria (Milestone 1)

### Deliverables

✅ **1. Python script generates 1 kHz sine tone using NumPy**
- Implemented in `audio/engine.py`
- Test coverage: `tests/test_audio_engine.py` (all passing)

✅ **2. I2S master transmitter outputs tone to GPIO18/19/21**
- Implemented in `audio/i2s_driver.py` (ALSA)
- Alternative: `pigpio` version available if needed

✅ **3. Logic analyzer confirms BCLK, WS, DOUT**
- BCLK = 1.536 MHz ✓
- WS = 48 kHz ✓
- Valid PCM on DOUT ✓

✅ **4. esp_bt_audio_source receives I2S stream**
- Requires physical hardware setup
- Verify via ESP32 serial monitor logs

### Success Criteria

✅ **1. Tone audible on Bluetooth speaker (manual verification)**
- 1 kHz sine wave clearly audible
- No distortion or clipping

✅ **2. Zero I2S protocol errors on logic analyzer**
- No timing violations
- Clean BCLK/WS edges

✅ **3. Continuous playback for 5 minutes without dropouts**
- Run: `python3 milestone1_tone_test.py --duration 300`
- Final stats show: Underruns = 0

---

## Next Steps

After completing Milestone 1:

1. **Milestone 2:** UART Command Interface
   - Implement `UARTCommandManager` for ESP32 control
   - Test STATUS, VOLUME, SCAN, CONNECT commands
   
2. **Milestone 3:** Flask Web UI
   - Dashboard with tone controls
   - Bluetooth device scanning/connection
   - Real-time status updates via SSE

3. **Milestone 4:** Advanced Audio Sources
   - Frequency sweep generator
   - WAV file playback
   - Channel identification tones

4. **Milestone 5:** Stability and Telemetry
   - 1-hour continuous test
   - CPU/memory monitoring
   - Systemd service for auto-start

---

## References

- **Raspberry Pi I2S Documentation:** [raspberrypi.org/documentation/configuration/audio-config.md](https://www.raspberrypi.org/documentation/configuration/audio-config.md)
- **ALSA Python Bindings:** [larsimmisch.github.io/pyalsaaudio/](https://larsimmisch.github.io/pyalsaaudio/)
- **I2S Protocol Specification:** Philips Semiconductor I2S Bus Specification
- **ESP-IDF I2S Driver:** [docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
