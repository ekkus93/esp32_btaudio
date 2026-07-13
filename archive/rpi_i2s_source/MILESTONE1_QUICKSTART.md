# Milestone 1 Quick Start Guide

## Overview

This guide provides a quick path to running **Milestone 1: Basic I2S Tone Generation** test.

**Goal:** Verify that the Raspberry Pi can generate a 1 kHz tone and transmit it via I2S to an ESP32, which then plays it through a Bluetooth speaker.

---

## Prerequisites

### Software
- ✅ Raspberry Pi OS (Bullseye or later)
- ✅ Python 3.9+
- ✅ All Python dependencies installed (`pip3 install -r requirements.txt`)
- ✅ ALSA I2S configured (`dtoverlay=i2s-mmap` in `/boot/config.txt`)

### Hardware
- Raspberry Pi (3B+ or 4 recommended)
- ESP32 with `esp_bt_audio_source` firmware
- Bluetooth speaker (paired with ESP32)
- Jumper wires for I2S connection

---

## Quick Test (60 seconds)

### 1. Run the test script

```bash
cd /path/to/esp32_btaudio/rpi_i2s_source
./milestone1_tone_test.py
```

### 2. Expected output

```
======================================================================
Milestone 1: Basic I2S Tone Generation Test
======================================================================

Configuration:
  Sample Rate:    48000 Hz
  Tone Frequency: 1000 Hz
  Tone Amplitude: 0.5
  Test Duration:  60 seconds

[1/3] Starting audio engine...
      ✓ Audio engine running (1 kHz tone generation)
[2/3] Starting I2S driver...
      ✓ I2S driver running (ALSA transmission)
[3/3] Monitoring playback for 60 seconds...

Real-time Statistics:
----------------------------------------------------------------------
Time:   10.2s | Frames:     489600 | Rate:  48000 fps | Buffer: 45.3% | Underruns:   0
```

### 3. Verify

- **Audio:** You should hear a 1 kHz tone on the Bluetooth speaker
- **Underruns:** Should remain at 0
- **Frame rate:** Should be ~48000 fps
- **Buffer fill:** Should be stable (30-70%)

---

## Full Milestone Test (5 minutes)

Run the full 5-minute continuous playback test:

```bash
./milestone1_tone_test.py --duration 300
```

This is the official Milestone 1 requirement for "continuous playback for 5 minutes without dropouts."

---

## Troubleshooting

### "alsaaudio not available"

Install ALSA Python bindings:

```bash
sudo apt-get install python3-alsaaudio
```

### No I2S device found

1. Verify `/boot/config.txt` has `dtoverlay=i2s-mmap`
2. Reboot: `sudo reboot`
3. Check for device: `aplay -l`

Expected output should show an I2S card (e.g., `card 0: sndrpii2s`).

### No audio on Bluetooth speaker

1. **Check ESP32:** Verify it's running and paired with speaker
2. **Check I2S wiring:** GPIO 18 (BCLK), 19 (WS), 21 (DOUT) → ESP32
3. **Check GND:** Common ground between RPi and ESP32
4. **Check volume:** Send `VOLUME 75` command to ESP32 via UART

### High underrun count

1. Increase ring buffer size in code
2. Reduce CPU load (close other applications)
3. Check I2S configuration

---

## Hardware Setup

For detailed hardware setup instructions, see:

📖 **[docs/MILESTONE1_HARDWARE_SETUP.md](docs/MILESTONE1_HARDWARE_SETUP.md)**

This includes:
- Raspberry Pi I2S configuration
- ESP32 wiring diagram
- Logic analyzer verification
- Complete troubleshooting guide

---

## Success Criteria

### ✅ Milestone 1 Complete When:

1. **Tone audible** on Bluetooth speaker (manual verification)
2. **Zero underruns** in 5-minute test
3. **Continuous playback** for 5 minutes without dropouts

### Optional Verification (Logic Analyzer):

- BCLK = 1.536 MHz ± 50 ppm
- WS = 48 kHz ± 50 ppm
- DOUT = valid PCM data synchronized to BCLK

---

## Next Steps

After completing Milestone 1:

1. **Milestone 2:** UART Command Interface (already implemented, needs hardware test)
2. **Milestone 3:** Flask Web UI (already implemented, ready to deploy)
3. **Milestone 4:** Advanced Audio Sources (sweep, WAV playback)
4. **Milestone 5:** Stability and Telemetry (1-hour test, systemd service)

All software for Milestones 1-5 is **already implemented**. Hardware validation is the remaining step.

---

## Files in This Test

- **milestone1_tone_test.py** — Main test script
- **docs/MILESTONE1_HARDWARE_SETUP.md** — Complete hardware setup guide
- **audio/engine.py** — Tone generation (NumPy-based)
- **audio/i2s_driver.py** — I2S transmission (ALSA-based)
- **audio/ring_buffer.py** — Audio sample buffer
- **config/manager.py** — Configuration management

---

## Status

✅ **Software:** COMPLETE (232 unit tests passing)  
⏳ **Hardware:** Awaiting Raspberry Pi deployment  
📝 **Documentation:** COMPLETE

**Ready for hardware validation!**
