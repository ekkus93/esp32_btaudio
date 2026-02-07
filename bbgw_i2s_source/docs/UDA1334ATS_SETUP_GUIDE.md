# UDA1334ATS DAC Setup Guide for bbgw_i2s_source

**Hardware I2S Test Mode — Direct Audio Output Without ESP32**

This guide shows how to connect a **UDA1334ATS stereo DAC** (WCMCU-1334/CJMCU-1334 breakout board) directly to the BeagleBone Green Wireless for audio testing. This configuration allows you to **validate I2S output** and **hear test tones** without requiring ESP32 or Bluetooth.

---

## Overview

### What is the UDA1334ATS?

**UDA1334ATS** is a low-power, high-fidelity stereo Digital-to-Analog Converter (DAC) chip by NXP Semiconductors:
- **Interface:** I2S slave receiver (same as ESP32)
- **Formats:** I2S-bus, LSB-justified, 16/20/24-bit
- **Built-in PLL:** No MCLK (master clock) required
- **Output:** Analog stereo audio via 3.5mm jack
- **Power:** 3-5V (WCMCU-1334 breakout has onboard regulation)

### WCMCU-1334 / CJMCU-1334 Breakout Board

The **WCMCU-1334** (also sold as **CJMCU-1334**) is a breadboard-friendly breakout module that includes:
- ✅ UDA1334ATS DAC chip
- ✅ Voltage regulator (3-5V input)
- ✅ Capacitors and support components
- ✅ 3.5mm headphone jack
- ✅ Pin headers for breadboard use

---

## When to Use UDA1334ATS Test Mode

### ✅ Use UDA1334ATS for:

1. **I2S Output Validation**
   - Verify BBGW McASP is generating correct I2S signals
   - Test BCLK, WS, and DOUT waveforms without oscilloscope
   - Confirm Device Tree overlay configuration is working

2. **Audio Quality Testing**
   - Hear tone generator output directly (1 kHz, sweeps, dual-tone)
   - Validate WAV file playback and resampling
   - Check for distortion, noise, or artifacts

3. **Rapid Development/Debugging**
   - Skip ESP32 and Bluetooth complexity
   - Faster iteration on BBGW audio code
   - Isolate I2S issues from UART/Bluetooth issues

4. **Educational/Demonstration**
   - Showcase BBGW McASP audio capabilities
   - Live audio demos without pairing Bluetooth
   - Simpler setup for workshops or teaching

### ❌ Don't use UDA1334ATS for:

- **UART command testing** (DAC has no serial interface)
- **Bluetooth functionality** (DAC is wired output only)
- **ESP32 integration testing** (use actual ESP32 for that)

---

## Hardware Requirements

### Components

| Component | Quantity | Notes |
|-----------|----------|-------|
| **BeagleBone Green Wireless** | 1 | With Device Tree overlay configured |
| **WCMCU-1334 / CJMCU-1334 Module** | 1 | UDA1334ATS breakout board |
| **Jumper Wires (Female-Female)** | 4-5 | For BBGW P9 header to UDA1334ATS |
| **Headphones or Powered Speakers** | 1 | 3.5mm jack |
| **5V Power Supply** | 1 | For BBGW (barrel jack or USB) |

### Optional
- Breadboard (for cleaner wiring)
- Logic analyzer (to verify I2S signals)

---

## Wiring Diagram

### Pin Connections: BBGW ↔ UDA1334ATS

Connect the BeagleBone P9 header to the UDA1334ATS breakout module:

```
┌──────────────────────┐        ┌──────────────────────┐
│ BeagleBone P9 Header │        │  WCMCU-1334 Module   │
├──────────────────────┤        ├──────────────────────┤
│ P9.31 (McASP0_ACLKX) │───────▶│ BCLK  (Bit Clock)    │
│ P9.29 (McASP0_FSX)   │───────▶│ WSEL  (Word Select)  │
│ P9.28 (McASP0_AXR0)  │───────▶│ DIN   (Data In)      │
│ P9.3  (3.3V)         │───────▶│ VIN   (Power 3-5V)   │
│ P9.1  (GND)          │───────▶│ GND   (Ground)       │
└──────────────────────┘        └──────────────────────┘
                                          │
                                          │ 3.5mm Jack
                                          ▼
                                   Headphones/Speakers
```

### Pin Mapping Table

| BBGW Pin | Signal Name | Function | UDA1334ATS Pin | Notes |
|----------|-------------|----------|----------------|-------|
| **P9.31** | McASP0_ACLKX | I2S Bit Clock | **BCLK** | 1.536 MHz @ 48 kHz |
| **P9.29** | McASP0_FSX | I2S Word Select | **WSEL** (or WS/LRCLK) | 48 kHz frame sync |
| **P9.28** | McASP0_AXR0 | I2S Data Out | **DIN** | Serial audio data |
| **P9.3** | 3.3V Power | Power Supply | **VIN** | 3.3V or 5V (regulator onboard) |
| **P9.1** | DGND | Ground | **GND** | Common ground |

### UDA1334ATS Pinout (WCMCU-1334 Breakout)

Typical WCMCU-1334 breakout pinout (verify with your module):

```
┌─────────────────────┐
│  WCMCU-1334 Module  │
├─────────────────────┤
│ VIN  ← 3.3V or 5V   │  Power input (regulated onboard)
│ GND  ← Ground       │  Ground
│ BCLK ← Bit Clock    │  I2S bit clock (from BBGW)
│ WSEL ← Word Select  │  I2S word select / LRCLK (from BBGW)
│ DIN  ← Data In      │  I2S data input (from BBGW)
│ (DOUT) Not used     │  I2S loopback (leave disconnected)
│ (FMT)  Not used     │  Format select (tie low for I2S)
│ (DEMP) Not used     │  De-emphasis (leave floating)
│ (PLL)  Not used     │  PLL filter (leave floating)
└─────────────────────┘
         │
    3.5mm Audio Jack
         │
     Headphones
```

**Notes:**
- Some modules label WSEL as **WS** or **LRCLK** (all equivalent)
- **FMT pin:** If available, tie to GND for I2S mode (some modules have this hardwired)
- **DOUT, DEMP, PLL:** Leave disconnected (not needed for basic operation)

---

## Software Configuration

### Step 1: Verify Device Tree Overlay

Ensure McASP I2S overlay is loaded (should already be done if you followed BBGW setup):

```bash
# Check if McASP overlay is loaded
dmesg | grep -i mcasp
# Should show: davinci-mcasp 48038000.mcasp: configured BCLK/WS, etc.

# Check ALSA device
aplay -l
# Should show: card 0: BBGW-I2S [BBGW-I2S], device 0
```

If not configured, see [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md) or [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md).

### Step 2: Update config.yaml

Edit `bbgw_i2s_source/config.yaml`:

```yaml
# Target device: 'esp32' (default) or 'uda1334' (DAC test mode)
target_device: 'uda1334'

i2s:
  device: "hw:0,0"        # ALSA device (same for both targets)
  sample_rate: 48000      # 48 kHz (UDA1334ATS supports up to 96 kHz)
  channels: 2             # Stereo
  format: "S16_LE"        # 16-bit signed little-endian PCM
  period_size: 1024       # Buffer size (tune for latency)

uart:
  # UART is ignored in UDA1334ATS mode (no serial interface on DAC)
  device: "/dev/ttyO4"
  baudrate: 115200
  timeout: 5.0

web:
  bind_address: "0.0.0.0" # Listen on all interfaces (Wi-Fi)
  port: 5000
  debug: false
```

**Key change:** Set `target_device: 'uda1334'` to document test mode.

**Note:** The Python application **doesn't need code changes** — the `target_device` setting is documentation-only. UART commands will fail gracefully (no device connected), but I2S audio output works identically.

### Step 3: Run the Application

Start bbgw_i2s_source normally:

```bash
cd /home/debian/esp32_btaudio/bbgw_i2s_source
source venv/bin/activate
python3 main.py
```

**Expected log output:**
```
2026-02-07 12:00:00 - INFO - ConfigManager initialized
2026-02-07 12:00:00 - INFO - AudioEngine initialized
2026-02-07 12:00:00 - INFO - I2SDriverALSA initialized (device=hw:0,0)
2026-02-07 12:00:00 - INFO - Started AudioEngine
2026-02-07 12:00:00 - INFO - Started I2SDriver
2026-02-07 12:00:00 - WARNING - UART device /dev/ttyO4 not found (expected in UDA1334ATS mode)
2026-02-07 12:00:00 - INFO - Starting Flask server on 0.0.0.0:5000
```

---

## Testing Audio Output

### Web UI Access

From any device on the same network:

```
http://<bbgw-ip>:5000
```

Find BBGW IP: `hostname -I` on the BBGW.

### Test Scenarios

#### 1. **1 kHz Tone Test** (30 seconds)

1. Open web UI dashboard
2. Set:
   - Frequency: **1000 Hz**
   - Amplitude: **0.5** (50%)
   - Mode: **Mono** (both channels)
3. Click **"Start Tone"**
4. **Expected:** Hear clean 1 kHz sine wave in both ears

**Validates:** Basic I2S output, DAC functionality, clean audio path

---

#### 2. **Dual-Tone Stereo Test** (Channel Separation)

1. Web UI dashboard
2. Set:
   - Frequency: **1000 Hz** (left: 1 kHz, right: 440 Hz in dual-tone mode)
   - Amplitude: **0.5**
   - Mode: **Dual-Tone** (if available) or generate two tones
3. Click **"Start Tone"**
4. **Expected:** 
   - Left ear: 1000 Hz
   - Right ear: 440 Hz (or different frequency)

**Validates:** Stereo separation, correct channel assignment (L/R)

---

#### 3. **Frequency Sweep** (20 Hz → 20 kHz, 10 seconds)

1. Web UI → Audio Sources → Frequency Sweep
2. Click **"Start Sweep"**
3. **Expected:** 
   - Smooth frequency ramp from low (rumble) to high (hiss)
   - No pops, clicks, or discontinuities

**Validates:** Audio engine sweep generation, DAC frequency response

---

#### 4. **WAV File Playback**

1. Upload a 44.1 kHz WAV file to `/home/debian/audio/test.wav`
2. Web UI → Audio Sources → WAV File
3. Select file, click **"Play"**
4. **Expected:** Hear audio (resampled to 48 kHz automatically)

**Validates:** WAV loading, resampling (SciPy), continuous playback

---

#### 5. **Silence Test** (Noise Floor)

1. Web UI → Audio Sources → Silence
2. Click **"Start Silence"**
3. Turn up headphone volume
4. **Expected:** Complete silence (no hiss, hum, or noise)

**Validates:** DAC noise floor, ground loop issues, EMI

---

## Expected Results

### ✅ Success Indicators

- **Clean audio:** No distortion, pops, or clicks
- **Correct frequency:** 1 kHz tone sounds like 1 kHz (use tuner app to verify)
- **Stereo separation:** Left and right channels distinct in dual-tone mode
- **Smooth sweep:** Frequency sweep has no discontinuities
- **Silent silence:** No hiss or hum during silence mode
- **Web UI responsive:** Tone changes <30 ms latency

### ⚠️ Warning Signs

- **Distortion:** Check amplitude (reduce to 0.3), verify power supply is stable
- **Pops/clicks:** Increase I2S buffer size (`period_size: 2048` in config.yaml)
- **One channel only:** Check WSEL (word select) wiring, verify stereo config
- **Hiss/noise:** Check ground connection, avoid ground loops, use shielded cable

---

## Troubleshooting

### Problem: No Audio Output

**Symptoms:** Headphones silent, no sound

**Check:**

1. **Power:**
   ```bash
   # Verify UDA1334ATS has power (LED on some modules)
   # Check BBGW 3.3V pin with multimeter: should be 3.3V ±5%
   ```

2. **I2S signals:**
   ```bash
   # Check ALSA is playing
   aplay -D hw:0,0 -f S16_LE -r 48000 -c 2 /dev/zero &
   # Kill after 5 sec: killall aplay
   # Use logic analyzer on BCLK/WS/DIN (should see activity)
   ```

3. **Wiring:**
   - Verify BCLK → BCLK (P9.31 → UDA1334ATS BCLK)
   - Verify WSEL → WSEL (P9.29 → UDA1334ATS WSEL)
   - Verify DIN → DIN (P9.28 → UDA1334ATS DIN)
   - Check ground connection (P9.1 → GND)

4. **Headphones:**
   - Test with different headphones/speakers
   - Verify 3.5mm jack is fully inserted
   - Check volume (some modules have output level control)

**Fix:** See [TROUBLESHOOTING_BBGW.md](TROUBLESHOOTING_BBGW.md) Section: UDA1334ATS DAC Output.

---

### Problem: Distorted Audio

**Symptoms:** Fuzzy, clipped, or harsh sound

**Check:**

1. **Amplitude too high:**
   ```yaml
   # In web UI, reduce amplitude
   Amplitude: 0.3  # Try 30% instead of 100%
   ```

2. **Power supply:**
   ```bash
   # Check BBGW power (should be 5V ±5%)
   # Weak USB power can cause issues
   # Use barrel jack 5V 2A adapter
   ```

3. **Sample rate mismatch:**
   ```yaml
   # Verify config.yaml
   sample_rate: 48000  # Not 44100 (unless you changed ALSA config)
   ```

**Fix:** Reduce amplitude to 0.3, use quality 5V power supply.

---

### Problem: Only One Channel (Left or Right)

**Symptoms:** Mono audio, or silent on one side

**Check:**

1. **WSEL (Word Select) wiring:**
   ```bash
   # Verify P9.29 (McASP0_FSX) → UDA1334ATS WSEL
   # This signal toggles left/right channels
   ```

2. **Config channels:**
   ```yaml
   i2s:
     channels: 2  # Must be 2 for stereo
   ```

3. **Headphone cable:**
   - Test with different headphones
   - Check 3.5mm jack is not partially inserted

**Fix:** Rewire WSEL, verify stereo config.

---

### Problem: Pops/Clicks During Playback

**Symptoms:** Intermittent pops or clicks

**Check:**

1. **Buffer underruns:**
   ```yaml
   i2s:
     period_size: 2048  # Increase from 1024
     buffer_size: 8192  # Increase from 4096
   ```

2. **CPU load:**
   ```bash
   top
   # Check if Python process >80% CPU (unlikely on BBGW)
   ```

3. **Wi-Fi interference:**
   - Disable Wi-Fi: `sudo ifconfig wlan0 down`
   - Test again (if pops stop, Wi-Fi was interfering)

**Fix:** Increase buffer sizes, reduce CPU load.

---

## Performance Benchmarks

**Expected Performance (BeagleBone Green Wireless + UDA1334ATS):**

| Metric | Target | Typical |
|--------|--------|---------|
| **CPU Usage** | <25% | 15-20% |
| **Audio Latency** | <50 ms | 20-30 ms (web UI → audio out) |
| **I2S Underruns** | <5/hour | 1-2/hour (with 1024 period) |
| **Noise Floor** | <-90 dB | -85 to -95 dB (UDA1334ATS spec) |
| **Frequency Response** | 20 Hz - 20 kHz | Flat ±0.5 dB (DAC spec) |

---

## Comparison: UDA1334ATS vs ESP32

| Feature | UDA1334ATS Test Mode | ESP32 Integration |
|---------|----------------------|-------------------|
| **Purpose** | I2S validation, audio testing | Full Bluetooth audio sink |
| **Output** | Analog (3.5mm jack) | Bluetooth → Speaker |
| **UART Commands** | ❌ No (DAC only) | ✅ Yes (volume, connect, etc.) |
| **Latency** | <30 ms | ~100-200 ms (Bluetooth) |
| **Complexity** | Low (3 I2S wires) | High (I2S + UART + Bluetooth pairing) |
| **Use Case** | Development, debugging | Production, wireless playback |

**Recommendation:** Use UDA1334ATS for **initial I2S testing**, then switch to ESP32 for **full system validation**.

---

## Next Steps After Validation

Once UDA1334ATS audio output is working:

1. ✅ **I2S confirmed working** → Device Tree overlay configured correctly
2. ✅ **Audio quality verified** → McASP driver and ALSA settings correct
3. ➡️ **Switch to ESP32:**
   - Disconnect UDA1334ATS
   - Connect ESP32 (see [README.md](../README.md) wiring section)
   - Update `config.yaml`: `target_device: 'esp32'`
   - Test UART commands and Bluetooth output

---

## Hardware Notes

### UDA1334ATS Module Variants

Different sellers may label the breakout as:
- **WCMCU-1334** (original)
- **CJMCU-1334** (common clone)
- **UDA1334A Breakout** (generic name)

All use the same UDA1334ATS chip and should work identically. Verify pinout matches your module.

### Power Consumption

- **BBGW:** ~2W (idle), ~2.5W (active audio)
- **UDA1334ATS:** ~10 mW (module total)
- **Total:** ~2.5W (can run from USB or barrel jack)

### Audio Quality

**UDA1334ATS Specs (from datasheet):**
- **SNR:** 95 dB (A-weighted)
- **THD+N:** -85 dB (0.006%)
- **Output:** 2V RMS (headphone out)
- **Frequency Response:** 20 Hz - 20 kHz ±0.5 dB

**Subjective Quality:** Excellent for test/debug, comparable to mid-range sound cards.

---

## References

- **UDA1334ATS Datasheet:** `/home/phil/work/esp32/esp32_btaudio/bbgw_i2s_source/docs/UDA1334ATS.pdf`
- **BeagleBone McASP Guide:** [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md)
- **I2S Format Spec:** Philips I2S Bus Specification (industry standard)
- **Adafruit Tutorial:** [I2S Breakouts](https://learn.adafruit.com/adafruit-i2s-stereo-decoder-uda1334a) (similar module)

---

## License

Same as parent project (esp32_btaudio).

---

**✅ Ready to test I2S output?** Wire up the UDA1334ATS, set `target_device: 'uda1334'` in config.yaml, and run the application. You should hear test tones immediately!
