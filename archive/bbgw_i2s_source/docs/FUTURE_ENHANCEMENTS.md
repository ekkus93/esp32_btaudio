# BeagleBone Green Wireless I2S Source — Future Enhancements

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Future enhancement ideas and research directions  
**Date:** 2026-02-07

---

## Overview

This document outlines potential future enhancements for the BBGW I2S Source project. These are advanced features that could improve performance, reduce power consumption, or add new capabilities. All items are **optional** and represent research/development opportunities beyond the core MVP functionality.

**Current Status:** Core functionality complete (~86% of planned work)  
**MVP Features:** I2S audio output, UART control, web UI, comprehensive documentation

---

## Table of Contents

1. [PRU Integration](#pru-integration)
2. [Power Management](#power-management)
3. [Multi-Instance Support](#multi-instance-support)
4. [Advanced Audio Features](#advanced-audio-features)
5. [Network Enhancements](#network-enhancements)
6. [Development Tools](#development-tools)

---

## PRU Integration

### Overview

BeagleBone's **Programmable Real-time Units (PRUs)** are independent 200 MHz microcontrollers that can achieve deterministic, low-latency I/O without Linux kernel overhead. Investigate using PRU for I2S instead of McASP.

### Motivation

**Potential Benefits:**
- **Ultra-low latency**: PRU has deterministic 5ns instruction timing
- **Zero kernel overhead**: Direct hardware access, no context switching
- **Precise timing**: Bit-banging I2S with nanosecond precision
- **CPU offload**: Free up ARM core for other tasks

**Potential Drawbacks:**
- **Complexity**: PRU programming requires assembly/C, device tree, remoteproc
- **Development time**: Steep learning curve, limited debugging tools
- **DMA limitations**: PRU has smaller local memory (8KB data, 8KB instruction)
- **McASP works well**: Current solution already meets requirements

### Current McASP Performance (Baseline)

| Metric | Value | Notes |
|--------|-------|-------|
| **Latency** | 21-23 ms | With default 4096 frame buffer |
| **CPU Usage** | 15-25% | During 48 kHz stereo streaming |
| **Reliability** | <5 underruns/hour | Stable, production-ready |
| **Complexity** | Low | Standard ALSA interface |

### PRU I2S Research Tasks

1. **Literature Review:**
   - Study existing PRU I2S implementations
   - Review BeagleBone PRU audio projects (e.g., bela.io)
   - Analyze PRU-ICSS architecture (AM335x TRM Chapter 4)

2. **Prototype Development:**
   - Implement basic PRU I2S bit-banging
   - Configure PRU pins via Device Tree
   - Set up PRU remoteproc communication
   - Measure latency and CPU usage

3. **Performance Comparison:**
   ```
   Test Matrix:
   - Latency: PRU vs McASP (measure with logic analyzer)
   - CPU usage: ARM core utilization
   - Jitter: Clock stability and timing variance
   - Complexity: Development and maintenance cost
   ```

4. **Integration Challenges:**
   - PRU shared pins with McASP (P9.28, P9.29, P9.31)
   - Device Tree conflicts (disable McASP when using PRU)
   - Ring buffer communication (PRU ↔ ARM shared memory)
   - Sample rate flexibility (harder with bit-banging)

### PRU I2S Implementation Outline

**High-Level Architecture:**
```
[Python App] → [Shared Memory Ring Buffer] → [PRU Firmware]
                                              ↓
                                         [I2S Pins (P9.28/29/31)]
```

**PRU Firmware Responsibilities:**
- Read audio samples from shared memory ring buffer
- Generate I2S bit clock (BCLK = 48 kHz × 32 bits × 2 channels = 3.072 MHz)
- Generate word select (WS = 48 kHz)
- Serialize 16-bit samples as I2S data (DOUT)
- Signal ARM core when buffer low (interrupt)

**ARM Application Changes:**
- Replace ALSA driver with PRU shared memory interface
- Load PRU firmware via remoteproc
- Handle PRU interrupts for buffer management

**Example PRU Code (Pseudocode):**
```c
// PRU Assembly/C for I2S output
while (1) {
    // Wait for buffer data from ARM
    wait_for_buffer();
    
    // For each audio frame (left + right sample)
    for (int i = 0; i < samples_per_period; i++) {
        // Read left and right samples (16-bit each)
        uint16_t left = buffer[i * 2];
        uint16_t right = buffer[i * 2 + 1];
        
        // Generate I2S signals
        ws_low();  // Word select low = left channel
        shift_out_16bit(left);
        
        ws_high(); // Word select high = right channel
        shift_out_16bit(right);
    }
    
    // Signal ARM that buffer consumed
    send_interrupt_to_arm();
}
```

### Decision Criteria

**Use PRU if:**
- Latency <5 ms is critical requirement
- Need deterministic real-time guarantees
- CPU usage must be <5%
- Have PRU development expertise

**Use McASP if:**
- Current latency (21 ms) is acceptable
- Standard ALSA interface is preferred
- Development time is constrained
- Maintainability is priority

### Resources

- [PRU-ICSS Reference Guide](https://www.ti.com/lit/ug/spruhz7/spruhz7.pdf)
- [BeagleBone PRU Tutorial](https://beagleboard.org/pru)
- [bela.io](https://bela.io/) - PRU-based ultra-low-latency audio platform
- [PRU Software Support Package](https://git.ti.com/cgit/pru-software-support-package/pru-software-support-package/)

### Estimated Effort

- **Research**: 1-2 weeks
- **Prototype**: 2-3 weeks
- **Integration**: 1-2 weeks
- **Testing**: 1 week
- **Total**: 5-8 weeks for experienced developer

### Recommendation

**Not recommended for MVP.** Current McASP implementation meets requirements. Consider PRU only if ultra-low latency (<5 ms) becomes a hard requirement.

---

## Power Management

### Overview

Implement power-saving features to reduce BBGW energy consumption during operation and idle periods.

### Motivation

**Use Cases:**
- Battery-powered deployments
- Always-on audio streaming applications
- Reduce heat generation in enclosed systems
- Lower operational costs

### Current Power Consumption (Estimated)

| State | Power | Notes |
|-------|-------|-------|
| **Idle (Wi-Fi on)** | ~2.0 W | No audio streaming |
| **Streaming** | ~2.5 W | 48 kHz I2S + web UI |
| **Peak** | ~3.0 W | CPU 100%, Wi-Fi active |
| **Sleep (not implemented)** | N/A | Would be <0.5 W |

**Note:** Measurements are estimates. Actual power depends on BBGW revision, peripherals, SD card, and load.

### Power Management Strategies

#### 1. CPU Frequency Scaling

**Current:** CPU governor set to `performance` (always 1 GHz)

**Enhancement:** Dynamic frequency scaling based on load

```bash
# Install cpufrequtils
sudo apt install cpufrequtils

# Switch to ondemand governor
echo ondemand | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Set minimum frequency to 300 MHz (idle)
echo 300000 | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

# Maximum stays at 1 GHz
echo 1000000 | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
```

**Expected Savings:** 20-30% during idle/light load

**Implementation:**
- Add cpufreq configuration to setup script
- Provide config option: `power_mode: [performance|ondemand|powersave]`
- Document trade-offs (latency vs power)

#### 2. Wi-Fi Power Management

**Current:** Wi-Fi always active (no power management)

**Enhancement:** Wi-Fi power-saving mode (PSM)

```bash
# Enable Wi-Fi power management
sudo iw dev wlan0 set power_save on

# Check status
iw dev wlan0 get power_save
```

**Expected Savings:** 10-20% when Wi-Fi idle

**Caveats:**
- Increased latency for web UI access
- May affect SSE stream responsiveness
- Test thoroughly with access point compatibility

**Implementation:**
- Add config option: `wifi.power_save: [on|off]`
- Enable only when web UI not in use
- Disable during active SSE connections

#### 3. Peripheral Power Management

**Current:** All peripherals always powered

**Enhancement:** Disable unused peripherals

```bash
# Disable HDMI (saves ~200 mW)
sudo systemctl stop gdm3  # If using GUI
echo 0 | sudo tee /sys/class/graphics/fb0/blank

# Disable USB host (if not used)
echo '1-1' | sudo tee /sys/bus/usb/drivers/usb/unbind

# Disable onboard LEDs (saves ~20 mW)
echo none | sudo tee /sys/class/leds/beaglebone:green:usr0/trigger
echo none | sudo tee /sys/class/leds/beaglebone:green:usr1/trigger
echo none | sudo tee /sys/class/leds/beaglebone:green:usr2/trigger
echo none | sudo tee /sys/class/leds/beaglebone:green:usr3/trigger
```

**Expected Savings:** 200-300 mW total

**Implementation:**
- Add setup script option: `--low-power`
- Disable HDMI, unused USB ports, LEDs
- Document how to re-enable if needed

#### 4. Application-Level Power Saving

**Current:** Continuous SSE polling, tone generation

**Enhancement:** Event-driven updates, smart polling

**Changes:**
- Reduce SSE update rate: 0.5s → 2.0s when no user activity
- Stop tone generation when no ESP32 connected
- Sleep audio thread when source is `silence`
- Use `select()` instead of polling for UART reads

**Implementation:**
```python
# Smart SSE update rate
if time_since_last_user_interaction > 60:
    update_interval = 2.0  # Slow poll
else:
    update_interval = 0.5  # Fast poll

# Stop audio generation when idle
if audio_source == 'silence' and not esp32_connected:
    i2s_driver.pause()  # Stop DMA transfers
```

**Expected Savings:** 5-10% CPU usage reduction

#### 5. Deep Sleep Mode (Advanced)

**Goal:** Support deep sleep during extended idle periods

**Challenges:**
- Linux doesn't support true deep sleep (needs bare-metal or RTOS)
- Wake-up latency can be seconds
- Requires external wake-up trigger (button, RTC alarm, network packet)

**Alternative:** Suspend-to-RAM (systemd `suspend`)

```bash
# Suspend system (requires wake-up trigger)
sudo systemctl suspend

# Wake-up sources:
# - UART activity (if configured in kernel)
# - RTC alarm
# - GPIO interrupt (button press)
```

**Expected Savings:** <500 mW in suspend (but not practical for audio streaming)

**Recommendation:** Not suitable for continuous audio streaming. Use only for battery-powered deployments with scheduled operation.

### Power Monitoring

**Tools:**
- INA219 current sensor (I2C interface)
- External multimeter on power rail
- Software monitoring: `powertop`, `cpupower`

**Example Integration:**
```python
# Add power monitoring to web UI
from ina219 import INA219

ina = INA219(shunt_ohms=0.1, address=0x40)
ina.configure()

current_mA = ina.current()
voltage_V = ina.voltage()
power_W = ina.power() / 1000.0

# Display in web UI status panel
```

### Configuration Example

**config.yaml:**
```yaml
power:
  mode: ondemand           # performance|ondemand|powersave
  wifi_power_save: true    # Enable Wi-Fi PSM
  disable_leds: true       # Turn off user LEDs
  sse_update_interval: 1.0 # Seconds (increase to save power)
  idle_timeout: 300        # Seconds before entering low-power mode
```

### Estimated Effort

- **Basic (CPU + Wi-Fi)**: 1-2 days
- **Advanced (peripheral + app)**: 3-5 days
- **Deep sleep research**: 1-2 weeks
- **Total**: 1-3 weeks

### Recommendation

Implement **basic power management** (CPU scaling, Wi-Fi PSM) if battery operation or heat reduction is required. Skip deep sleep for continuous streaming applications.

---

## Multi-Instance Support

### Overview

Support multiple simultaneous I2S audio streams using different McASP instances or software mixing.

### Motivation

**Use Cases:**
- Stereo + subwoofer (3-channel output)
- Multi-room audio (different streams to different rooms)
- Zone mixing (background music + announcements)
- Advanced audio routing

### BBGW Hardware Capabilities

BeagleBone AM335x has **3 McASP instances:**

| McASP | Pins Available | Status | Notes |
|-------|----------------|--------|-------|
| **McASP0** | P9.28/29/30/31 | Used | Current I2S implementation |
| **McASP1** | P9.24/25/26/27 | Available | Requires Device Tree overlay |
| **McASP2** | Expansion header | Limited | Not easily accessible on BBGW |

**Theoretical Max:** 2 independent I2S outputs (McASP0 + McASP1)

### Approach 1: Hardware Multi-Instance (McASP0 + McASP1)

**Architecture:**
```
[Python App]
   ├─→ [I2S Driver 0] → McASP0 → P9.28/29/31 (Stream A)
   └─→ [I2S Driver 1] → McASP1 → P9.24/25/27 (Stream B)
```

**Implementation Steps:**

1. **Create McASP1 Device Tree Overlay:**
   ```dts
   &am33xx_pinmux {
       mcasp1_pins: pinmux_mcasp1_pins {
           pinctrl-single,pins = <
               AM33XX_IOPAD(0x9a0, PIN_OUTPUT | MUX_MODE0)  /* P9.24 BCLK */
               AM33XX_IOPAD(0x9a4, PIN_OUTPUT | MUX_MODE0)  /* P9.25 WS */
               AM33XX_IOPAD(0x9a8, PIN_OUTPUT | MUX_MODE0)  /* P9.27 DOUT */
           >;
       };
   };
   
   &mcasp1 {
       status = "okay";
       pinctrl-names = "default";
       pinctrl-0 = <&mcasp1_pins>;
       /* McASP1 configuration... */
   };
   ```

2. **Update Python Application:**
   ```python
   # Support multiple I2S devices
   class MultiI2SManager:
       def __init__(self, config):
           self.drivers = []
           for device_config in config.get('i2s.devices'):
               driver = I2SDriverALSA(device_config, ring_buffer)
               self.drivers.append(driver)
       
       def start_all(self):
           for driver in self.drivers:
               driver.start()
   ```

3. **Configuration:**
   ```yaml
   i2s:
     devices:
       - name: "Main"
         device: "hw:CARD=BBGW-I2S-0,DEV=0"
         sample_rate: 48000
       - name: "Subwoofer"
         device: "hw:CARD=BBGW-I2S-1,DEV=0"
         sample_rate: 48000
   ```

**Challenges:**
- Pin conflicts (ensure McASP0/1 don't share pins)
- CPU usage (2× I2S = 30-50% CPU)
- Audio synchronization (need sample-perfect timing)
- DMA resource contention

**Estimated Effort:** 2-3 weeks

### Approach 2: Software Audio Mixing

**Architecture:**
```
[Python App]
   ├─→ [Mixer: Stream A + Stream B]
   └─→ [I2S Driver] → McASP0 → P9.28/29/31
```

**Implementation:**
```python
class AudioMixer:
    def __init__(self, num_sources=4):
        self.sources = [RingBuffer(8192) for _ in range(num_sources)]
        self.output = RingBuffer(8192)
        self.mixing_thread = None
    
    def mix(self):
        """Mix multiple sources into single output."""
        while self.running:
            # Read from all sources
            samples = [src.read(1024) for src in self.sources]
            
            # Mix (simple average, or weighted sum)
            mixed = np.mean(samples, axis=0)
            
            # Clip to prevent overflow
            mixed = np.clip(mixed, -32768, 32767).astype(np.int16)
            
            # Write to output
            self.output.write(mixed)
```

**Benefits:**
- Uses single McASP instance (hardware-proven)
- More flexible routing
- Lower hardware complexity

**Drawbacks:**
- Increased CPU usage (mixing overhead)
- Quality degradation if not careful with gain staging
- All streams must be same sample rate

**Estimated Effort:** 1-2 weeks

### Approach 3: External DAC with TDM/I2S

**Architecture:**
```
[BBGW McASP0] → [External Multi-Channel DAC] → Multiple Analog Outputs
```

**Example DACs:**
- PCM5142 (2-channel, I2S)
- PCM1865 (4-channel ADC + I2S)
- TLV320AIC3104 (Stereo codec, I2S)

**Benefits:**
- Professional audio quality
- Hardware mixing/routing
- Multiple analog outputs

**Drawbacks:**
- Requires external hardware (DAC board)
- I2C configuration needed
- Increased cost and complexity

**Estimated Effort:** 3-4 weeks (hardware + software)

### Decision Matrix

| Approach | Complexity | CPU Usage | Quality | Cost | Effort |
|----------|------------|-----------|---------|------|--------|
| **Multi-McASP** | High | Medium | Excellent | $0 | 2-3 weeks |
| **Software Mix** | Medium | High | Good | $0 | 1-2 weeks |
| **External DAC** | High | Low | Excellent | $10-50 | 3-4 weeks |

### Recommendation

**For MVP:** Not needed (single I2S output sufficient).

**Future:** Implement **software mixing** if multiple sources needed. Reserve multi-McASP for applications requiring independent, sample-perfect synchronization.

---

## Advanced Audio Features

### 1. Audio Effects Pipeline

**Features:**
- Equalizer (bass, mid, treble)
- Compression/limiting
- Reverb/delay
- Crossfade between sources

**Implementation:**
```python
# Use SciPy for filters
from scipy.signal import butter, lfilter

class AudioEffects:
    def apply_eq(self, samples, bass_gain, mid_gain, treble_gain):
        # 3-band EQ using Butterworth filters
        bass = lfilter(butter_lowpass(200), samples) * bass_gain
        mid = lfilter(butter_bandpass(200, 4000), samples) * mid_gain
        treble = lfilter(butter_highpass(4000), samples) * treble_gain
        return bass + mid + treble
```

**Effort:** 1-2 weeks

### 2. Advanced WAV Support

**Current:** 44.1 kHz WAV files only (resampled to 48 kHz)

**Enhancement:**
- Support 24-bit/32-bit WAV files
- Support 96 kHz/192 kHz (downsample to 48 kHz)
- Support FLAC, MP3, OGG formats
- Gapless playback
- Playlist management

**Libraries:**
- `pydub` (MP3, OGG, FLAC)
- `soundfile` (libsndfile wrapper)
- `audioread` (multiple format support)

**Effort:** 1-2 weeks

### 3. Real-Time Audio Visualization

**Features:**
- FFT spectrum analyzer (web UI)
- Waveform display
- VU meters
- Frequency response graph

**Implementation:**
```python
import numpy as np

def compute_fft(samples, sample_rate):
    fft = np.fft.rfft(samples)
    freqs = np.fft.rfftfreq(len(samples), 1/sample_rate)
    magnitude = np.abs(fft)
    return freqs, magnitude
```

**Web UI:**
- Canvas-based spectrum display
- WebGL for high performance
- SSE updates at 10-20 Hz

**Effort:** 2-3 weeks

### 4. MIDI Control

**Feature:** Control audio via MIDI messages (USB MIDI keyboard)

**Use Cases:**
- Trigger WAV samples with keyboard
- Adjust tone frequency with pitch bend
- Control volume with MIDI CC

**Libraries:**
- `python-rtmidi`
- `mido`

**Effort:** 1-2 weeks

---

## Network Enhancements

### 1. Bluetooth Audio Sink (A2DP)

**Feature:** Make BBGW itself a Bluetooth speaker (receive audio from phone)

**Architecture:**
```
[Phone A2DP Source] → [BBGW BlueZ A2DP Sink] → [I2S Output]
```

**Implementation:**
- Use BlueZ PulseAudio integration
- Route PulseAudio to ALSA (I2S)
- Handle Bluetooth pairing/discovery

**Effort:** 2-3 weeks

### 2. Networked Audio Streaming

**Protocols:**
- **RTP/RTSP:** Real-time streaming protocol
- **Shoutcast/Icecast:** Internet radio streaming
- **AirPlay:** Apple ecosystem
- **Chromecast:** Google ecosystem

**Use Cases:**
- Stream audio from BBGW to multiple clients
- Receive audio from network source
- Synchronize multiple BBGW devices

**Effort:** 3-4 weeks per protocol

### 3. MQTT Control

**Feature:** Control BBGW via MQTT (IoT integration)

**Use Cases:**
- Home automation (Home Assistant, OpenHAB)
- Scheduled audio playback
- Event-triggered announcements

**Libraries:**
- `paho-mqtt`

**Example:**
```python
import paho.mqtt.client as mqtt

def on_message(client, userdata, message):
    if message.topic == "audio/tone/frequency":
        frequency = int(message.payload)
        audio_engine.set_tone_frequency(frequency)

client = mqtt.Client()
client.on_message = on_message
client.connect("mqtt.local", 1883)
client.subscribe("audio/#")
client.loop_forever()
```

**Effort:** 1 week

---

## Development Tools

### 1. Logic Analyzer Integration

**Feature:** Automatic I2S signal validation

**Tools:**
- Sigrok/PulseView integration
- Automated capture scripts
- Signal quality metrics

**Implementation:**
```python
import subprocess

def validate_i2s_signals():
    # Capture I2S signals via Sigrok
    subprocess.run([
        "sigrok-cli",
        "-d", "fx2lafw",
        "-c", "samplerate=24000000",
        "-P", "i2s:sck=0:ws=1:sd=2",
        "-o", "capture.sr"
    ])
    
    # Parse and validate
    # Check: BCLK frequency = 3.072 MHz
    # Check: WS frequency = 48 kHz
    # Check: Data transitions on BCLK edge
```

**Effort:** 1-2 weeks

### 2. Automated Hardware-in-Loop Testing

**Feature:** Pytest fixtures for real BBGW hardware

**Setup:**
- BeagleBone connected to CI/CD server
- GPIO-controlled relay for power cycling
- SSH-based test execution

**Example:**
```python
@pytest.fixture
def bbgw_hardware(request):
    # Power on BBGW
    relay.power_on()
    time.sleep(30)  # Boot time
    
    # SSH connection
    ssh = paramiko.SSHClient()
    ssh.connect("beaglebone.local")
    
    yield ssh
    
    # Cleanup
    ssh.close()
    relay.power_off()

def test_i2s_output(bbgw_hardware):
    # Run application on BBGW
    stdin, stdout, stderr = bbgw_hardware.exec_command("python3 main.py")
    
    # Measure I2S output with logic analyzer
    # Assert BCLK frequency = 3.072 MHz ± 1%
```

**Effort:** 2-3 weeks

### 3. Performance Regression Testing

**Feature:** Track performance metrics over time

**Metrics:**
- CPU usage per commit
- Memory usage trends
- I2S buffer underrun rate
- Web UI response time

**Tools:**
- `pytest-benchmark`
- InfluxDB + Grafana for visualization
- Automated alerts on regressions

**Effort:** 1-2 weeks

---

## Implementation Priorities

### High Priority (If Needed)

1. **Power Management (Basic)** - 1-2 days
   - CPU frequency scaling
   - Wi-Fi power save
   - Immediate power savings

2. **Software Audio Mixing** - 1-2 weeks
   - Multi-source support
   - Simple to implement
   - No hardware changes

### Medium Priority

3. **Advanced WAV Support** - 1-2 weeks
   - Better format compatibility
   - User-requested feature

4. **Real-Time Visualization** - 2-3 weeks
   - Nice-to-have for demos
   - Improves user experience

### Low Priority (Research)

5. **PRU Integration** - 5-8 weeks
   - Only if ultra-low latency required
   - High complexity, low ROI for MVP

6. **Multi-McASP Hardware** - 2-3 weeks
   - Niche use case
   - Software mixing simpler

7. **Bluetooth A2DP Sink** - 2-3 weeks
   - Different use case than current design
   - Would require BlueZ configuration

### Future Research

8. **Network Streaming Protocols** - 3-4 weeks each
9. **MIDI Control** - 1-2 weeks
10. **Hardware-in-Loop Testing** - 2-3 weeks

---

## Conclusion

The BBGW I2S Source MVP is feature-complete and production-ready. All enhancements in this document are **optional** and should be prioritized based on:

1. **User requirements** (is ultra-low latency needed?)
2. **Development resources** (time and expertise available)
3. **Complexity vs benefit** (ROI analysis)
4. **Hardware constraints** (BBGW capabilities)

**Recommendation:** Focus on **Phase 6 (Deployment and Release)** to complete the port. Revisit these enhancements only if specific use cases emerge.

---

## References

### PRU Resources
- [AM335x PRU-ICSS Reference Guide](https://www.ti.com/lit/ug/spruhz7/spruhz7.pdf)
- [BeagleBoard PRU Documentation](https://beagleboard.org/pru)
- [bela.io - Low-Latency Audio Platform](https://bela.io/)

### Power Management
- [AM335x Power Management](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf) - Section 8
- [Linux CPUFreq Documentation](https://www.kernel.org/doc/html/latest/admin-guide/pm/cpufreq.html)

### Multi-Instance Audio
- [ALSA Multi-Channel Configuration](https://www.alsa-project.org/wiki/Asoundrc#Multiple_devices)
- [McASP Driver Documentation](https://www.kernel.org/doc/html/latest/sound/soc/davinci/davinci-mcasp.html)

### Network Audio
- [RTP/RTSP RFC 3550](https://tools.ietf.org/html/rfc3550)
- [Shoutcast Protocol](https://cast.readme.io/docs/shoutcast)
- [PulseAudio Network Audio](https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/User/Network/)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
