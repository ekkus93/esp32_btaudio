# Hardware Requirements — BBGW I2S Source

**Platform:** BeagleBone Green Wireless  
**Target Device:** ESP32 Bluetooth Audio Source  
**Last Updated:** 2026-02-07

---

## Overview

This document specifies the hardware requirements and wiring for the **bbgw_i2s_source** project, which uses a BeagleBone Green Wireless to generate I2S audio and send UART commands to an ESP32 Bluetooth audio transmitter.

**Key Interfaces:**
- **I2S Audio**: BeagleBone McASP → ESP32 I2S (48 kHz, 16-bit stereo)
- **UART Commands**: BeagleBone UART4 ↔ ESP32 UART (115200 baud)
- **Network**: Wi-Fi web UI for control

---

## BeagleBone Green Wireless Specifications

### Hardware Overview

**Processor:**
- **SoC**: Texas Instruments AM3358 (Sitara ARM Cortex-A8)
- **Clock Speed**: 1 GHz
- **Architecture**: ARMv7 32-bit

**Memory:**
- **RAM**: 512 MB DDR3
- **Flash**: 4 GB eMMC (on-board storage)
- **MicroSD**: Supports external storage

**Connectivity:**
- **Wi-Fi**: 802.11 b/g/n (2.4 GHz)
- **Bluetooth**: Bluetooth 4.1 + BLE (not used in this project)
- **USB**: 1x USB 2.0 host, 1x USB 2.0 client
- **Ethernet**: None (Wi-Fi only)

**Expansion Headers:**
- **P8 Header**: 46 pins (2x23)
- **P9 Header**: 46 pins (2x23)
- **Total GPIO**: 65+ digital I/O pins
- **Analog Inputs**: 7x 12-bit ADC (0-1.8V)

**Audio Capabilities:**
- **McASP0**: Multichannel Audio Serial Port (hardware I2S engine)
- **McASP1**: Limited availability on expansion headers

**Power:**
- **Input**: 5V DC @ 1A (barrel jack or USB)
- **I/O Voltage**: 3.3V (all digital pins)

**Dimensions:**
- **Size**: 86.36 mm × 53.34 mm (same as BeagleBone Black)
- **Weight**: ~45g

**Operating System:**
- **Supported**: Debian Linux (recommended), Ubuntu, Arch Linux
- **Kernel**: Linux 4.x/5.x with Device Tree support

### McASP (I2S Interface)

The AM3358 includes a **McASP (Multichannel Audio Serial Port)** hardware module for high-quality audio interfaces:

**Features:**
- Hardware-based I2S, TDM, and DIT formats
- Up to 16 serializers (transmit/receive data lines)
- Built-in clock generation (bit clock, frame sync)
- DMA support for low CPU overhead
- Sample rates: 8 kHz to 192 kHz
- Bit depths: 8, 16, 24, 32 bits

**For this project:**
- **Format**: I2S (Philips standard)
- **Sample Rate**: 48 kHz
- **Bit Depth**: 16-bit signed little-endian (S16_LE)
- **Channels**: 2 (stereo)
- **Direction**: Transmit only (BBGW → ESP32)

### UART Capabilities

The AM3358 includes **6 UART modules** (UART0-UART5):

**UART4 (selected for this project):**
- **Baud Rates**: 300 to 3 Mbps
- **Data Bits**: 5, 6, 7, 8
- **Parity**: None, odd, even
- **Stop Bits**: 1, 1.5, 2
- **Flow Control**: None (not needed for this project)
- **FIFO**: 64-byte transmit and receive FIFOs

**Configuration:**
- **Baud Rate**: 115200 bps
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Device**: `/dev/ttyO4` (Linux device node)

---

## ESP32 Requirements

### ESP32 Specifications (Target Device)

**Processor:**
- **SoC**: Espressif ESP32 (dual-core Xtensa LX6)
- **Clock Speed**: 80-240 MHz
- **Architecture**: 32-bit

**Memory:**
- **RAM**: 520 KB SRAM
- **Flash**: 4 MB (typical)

**Connectivity:**
- **Wi-Fi**: 802.11 b/g/n (2.4 GHz)
- **Bluetooth**: Classic + BLE (A2DP source profile required)
- **UART**: 3 hardware UARTs
- **I2S**: 2 hardware I2S interfaces

**Audio Capabilities:**
- **I2S Interface**: Receive from BBGW
- **Bluetooth A2DP**: Transmit to Bluetooth speakers/headphones
- **Codecs**: SBC (mandatory), optional AAC, aptX

**Power:**
- **Input**: 3.3V or 5V (via regulator)
- **I/O Voltage**: 3.3V

### ESP32 Project: esp_bt_audio_source

The ESP32 must be running the **esp_bt_audio_source** firmware from this repository.

**Features:**
- **I2S Receiver**: Receives PCM audio from BBGW
- **Bluetooth A2DP Source**: Transmits audio to Bluetooth devices
- **UART Interface**: Receives commands (tone frequency, volume, silence)
- **Auto-connect**: Pairs with known Bluetooth devices

**Configuration:**
- **I2S Pins** (configurable in esp_bt_audio_source):
  - **BCLK**: GPIO26 (bit clock input)
  - **WS**: GPIO25 (word select input)
  - **DIN**: GPIO22 (data input from BBGW)
- **UART Pins**:
  - **TXD**: GPIO1 or GPIO17 (transmit to BBGW)
  - **RXD**: GPIO3 or GPIO16 (receive from BBGW)

**See:** `esp_bt_audio_source/README.md` for ESP32 setup instructions.

---

## Logic Level Compatibility

**BBGW I/O Voltage:** 3.3V  
**ESP32 I/O Voltage:** 3.3V  

✅ **Direct connection is safe** (no level shifters needed)

**Important Notes:**
- Both devices use 3.3V logic levels
- No voltage conversion required
- Connect grounds together (DGND)
- Use short wires (<30 cm) for I2S signals to minimize noise

**Maximum Ratings:**
- **BBGW GPIO**: 3.3V max (DO NOT exceed)
- **ESP32 GPIO**: 3.6V max (3.3V nominal)
- **Current per pin**: <8 mA (BBGW), <40 mA (ESP32)

**ESD Protection:**
- Both boards include on-board ESD protection
- Use anti-static precautions when handling
- Do not hot-plug I2S or UART connections

---

## Wiring Diagram: BBGW ↔ ESP32

### I2S Connections (Audio)

**BBGW McASP → ESP32 I2S**

```
BeagleBone Green Wireless           ESP32 Dev Board
(P9 Header)                         (GPIO Pins)

┌─────────────────────┐             ┌─────────────────────┐
│                     │             │                     │
│  P9.31 (ACLKX) ─────┼─────────────┼────→ GPIO26 (BCLK) │  Bit Clock
│                     │             │                     │
│  P9.29 (FSX)   ─────┼─────────────┼────→ GPIO25 (WS)   │  Word Select
│                     │             │                     │
│  P9.28 (AXR1)  ─────┼─────────────┼────→ GPIO22 (DIN)  │  Data In
│                     │             │                     │
│  P9.1/P9.2 (GND)────┼─────────────┼────→ GND           │  Ground
│                     │             │                     │
└─────────────────────┘             └─────────────────────┘
```

**Signal Descriptions:**
- **ACLKX (P9.31 → GPIO26)**: I2S bit clock, 1.536 MHz for 48 kHz stereo (48000 × 32)
- **FSX (P9.29 → GPIO25)**: I2S frame sync (word select), 48 kHz, toggles L/R channels
- **AXR1 (P9.28 → GPIO22)**: I2S data output from BBGW to ESP32, MSB-first, 16-bit samples
- **GND**: Common ground reference (required for proper signal integrity)

**Alternative I2S Data Pin:**
- **P9.30 (AXR0)** can be used instead of P9.28 (AXR1) with Device Tree changes

### UART Connections (Commands)

**BBGW UART4 ↔ ESP32 UART**

```
BeagleBone Green Wireless           ESP32 Dev Board
(P9 Header)                         (GPIO Pins)

┌─────────────────────┐             ┌─────────────────────┐
│                     │             │                     │
│  P9.11 (UART4_RXD)←─┼─────────────┼───── GPIO1 (TXD)   │  ESP32 → BBGW
│                     │             │      or GPIO17      │
│  P9.13 (UART4_TXD)──┼─────────────┼────→ GPIO3 (RXD)   │  BBGW → ESP32
│                     │             │      or GPIO16      │
│  P9.1/P9.2 (GND)────┼─────────────┼────→ GND           │  Ground
│                     │             │                     │
└─────────────────────┘             └─────────────────────┘
```

**Signal Descriptions:**
- **P9.11 (RXD)**: BBGW receives data from ESP32 (ESP32 TXD → BBGW RXD)
- **P9.13 (TXD)**: BBGW transmits data to ESP32 (BBGW TXD → ESP32 RXD)
- **GND**: Common ground (same as I2S ground)

**UART Configuration:**
- **Baud Rate**: 115200 bps
- **Data**: 8 bits
- **Parity**: None
- **Stop**: 1 bit
- **Flow Control**: None

**Note:** TX and RX are **crossed** (transmit on one device connects to receive on the other).

### Power Connections

**Option 1: Separate Power Supplies (Recommended)**
```
BBGW: 5V DC barrel jack (1A minimum)
ESP32: USB power or 3.3V/5V via Vin pin
Ground: Connect BBGW GND (P9.1) to ESP32 GND
```

**Option 2: Power ESP32 from BBGW (Advanced)**
```
BBGW P9.3 (3.3V) → ESP32 3V3 pin (max 250 mA from BBGW)
BBGW P9.1 (GND) → ESP32 GND
```
⚠️ **Warning:** BBGW can supply limited current (250 mA @ 3.3V). If ESP32 Wi-Fi/BT draws >250 mA, use separate power.

### Complete Wiring Summary

**Minimal Wiring (5 connections):**
1. **P9.31 → GPIO26** (I2S BCLK)
2. **P9.29 → GPIO25** (I2S WS)
3. **P9.28 → GPIO22** (I2S Data)
4. **P9.13 → ESP32 RX** (UART TX from BBGW)
5. **P9.1 → GND** (Common ground)

**Full Wiring (6 connections):**
1. **P9.31 → GPIO26** (I2S BCLK)
2. **P9.29 → GPIO25** (I2S WS)
3. **P9.28 → GPIO22** (I2S Data)
4. **P9.11 ← ESP32 TX** (UART RX to BBGW)
5. **P9.13 → ESP32 RX** (UART TX from BBGW)
6. **P9.1 → GND** (Common ground)

**Recommended Wire Types:**
- **I2S Signals**: Twisted pair or shielded cable, <30 cm length
- **UART Signals**: Standard jumper wires, <50 cm length
- **Ground**: 22-24 AWG wire, keep impedance low

---

## Pin Assignment Table

### BeagleBone Green Wireless (P9 Header)

| P9 Pin | Name | GPIO | Function | Direction | Connect To | Signal |
|--------|------|------|----------|-----------|------------|--------|
| **P9.1** | DGND | - | Ground | - | ESP32 GND | Ground reference |
| **P9.2** | DGND | - | Ground | - | ESP32 GND | Ground reference (alternate) |
| **P9.11** | GPIO0_30 | 30 | UART4_RXD | Input | ESP32 TXD (GPIO1/17) | UART receive |
| **P9.13** | GPIO0_31 | 31 | UART4_TXD | Output | ESP32 RXD (GPIO3/16) | UART transmit |
| **P9.28** | GPIO3_17 | 113 | McASP0_AXR1 | Output | ESP32 DIN (GPIO22) | I2S data out |
| **P9.29** | GPIO3_15 | 111 | McASP0_FSX | Output | ESP32 WS (GPIO25) | I2S word select |
| **P9.30** | GPIO3_16 | 112 | McASP0_AXR0 | Bidir | (Alternate for P9.28) | I2S data (alt) |
| **P9.31** | GPIO3_14 | 110 | McASP0_ACLKX | Output | ESP32 BCLK (GPIO26) | I2S bit clock |

**Pin Notes:**
- Pins use **Mode 0** for McASP (default function)
- UART pins use **Mode 6** (configured via Device Tree)
- All I/O pins are 3.3V logic level
- Maximum current per pin: 6-8 mA (see AM335x datasheet)

### ESP32 Dev Board

| ESP32 Pin | Function | Direction | Connect To | Signal | Notes |
|-----------|----------|-----------|------------|--------|-------|
| **GPIO26** | I2S BCLK | Input | P9.31 (ACLKX) | Bit clock | 1.536 MHz @ 48 kHz |
| **GPIO25** | I2S WS | Input | P9.29 (FSX) | Word select | 48 kHz L/R toggle |
| **GPIO22** | I2S DIN | Input | P9.28 (AXR1) | Data in | PCM audio samples |
| **GPIO1** | UART TXD | Output | P9.11 (UART4_RXD) | UART transmit | 115200 baud (alt: GPIO17) |
| **GPIO3** | UART RXD | Input | P9.13 (UART4_TXD) | UART receive | 115200 baud (alt: GPIO16) |
| **GND** | Ground | - | P9.1 or P9.2 (GND) | Ground | Common reference |

**ESP32 Pin Notes:**
- GPIO1/GPIO3 are default UART0 (USB console) — prefer GPIO16/GPIO17 if available
- All pins are 3.3V tolerant
- I2S pins configured in esp_bt_audio_source firmware (see `main/i2s_config.h`)

---

## Physical Layout Recommendations

### Breadboard Setup (Prototyping)

```
┌────────────────────────────────────────────────┐
│                                                │
│  ┌───────────────┐       ┌──────────────┐     │
│  │  BeagleBone   │       │   ESP32      │     │
│  │  Green        │       │   Dev Board  │     │
│  │  Wireless     │       │              │     │
│  └───────────────┘       └──────────────┘     │
│         │                       │              │
│         └───────────────────────┘              │
│              (Jumper Wires)                    │
│                                                │
│  Keep I2S wires < 30 cm, avoid crossing       │
│  power lines. Use twisted pairs if possible.  │
│                                                │
└────────────────────────────────────────────────┘
```

**Best Practices:**
1. **Keep I2S wires short** (<30 cm) to minimize capacitance and noise
2. **Use twisted pairs** for BCLK/GND and WS/GND if possible
3. **Separate I2S and UART** routes to avoid crosstalk
4. **Common ground point**: Connect all grounds at a single point
5. **Power supply decoupling**: Use 0.1 µF caps near BBGW and ESP32 power pins

### PCB Layout (Production)

For a permanent installation:
- **I2S Traces**: 50 Ω impedance, <10 cm length, ground plane underneath
- **UART Traces**: Less critical, standard 0.25 mm width acceptable
- **Ground Plane**: Solid pour on bottom layer, stitching vias every 5 mm
- **Decoupling**: 0.1 µF + 10 µF near BBGW and ESP32 power pins
- **Connectors**: Use 2.54 mm (0.1") headers for easy debugging

---

## Logic Analyzer / Oscilloscope Verification

### I2S Signal Verification

To verify correct I2S operation, use a logic analyzer or oscilloscope:

**Expected Signals:**

| Signal | Frequency | Duty Cycle | Notes |
|--------|-----------|------------|-------|
| **BCLK** | 1.536 MHz | 50% | 48 kHz × 32 bits/channel |
| **WS** | 48 kHz | 50% | Toggles every 16 BCLK cycles |
| **Data** | - | Varies | Valid on BCLK rising edge (I2S standard) |

**Logic Analyzer Settings:**
- **Sample Rate**: ≥10 MHz (preferably 24 MHz)
- **Channels**: BCLK, WS, Data, GND
- **Protocol**: I2S (Philips standard, MSB-first)
- **Trigger**: WS rising edge

**Oscilloscope Settings:**
- **Bandwidth**: ≥20 MHz
- **Timebase**: 10 µs/div (to see multiple WS cycles)
- **Voltage**: 1V/div
- **Coupling**: DC
- **Trigger**: BCLK or WS edge

**What to Check:**
1. **BCLK**: Clean square wave, 1.536 MHz, 3.3V amplitude
2. **WS**: Clean square wave, 48 kHz, 3.3V amplitude, 50% duty cycle
3. **Data**: Transitions on BCLK falling edge (valid on rising edge)
4. **Timing**: Data bit 0 (MSB) occurs 1 BCLK after WS transition
5. **No ringing**: Edges should be clean (use shorter wires if ringing observed)

**Example I2S Waveform (1 kHz Tone):**
```
WS:    ___________┌───────────┐___________┌───────────┐___
BCLK:  _┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐_┐
Data:  ──┐_____┐_┐_┐_____┐___┐_____┐_┐_┐_____┐───┐_____┐_
       MSB                LSB MSB                LSB
       (Left Channel)          (Right Channel)
```

---

## Audio Quality Considerations

### Jitter and Timing

**BBGW McASP Clock Source:**
- **Master mode**: BBGW generates BCLK and WS (recommended)
- **Jitter**: <100 ppm (typ) from McASP PLL
- **ESP32 I2S**: Slave mode (receives clocks from BBGW)

**For best audio quality:**
- Use BBGW as I2S master (generates clocks)
- Minimize capacitive loading on BCLK/WS (<50 pF)
- Keep I2S wires <30 cm (reduces jitter from reflections)

### Sample Rate Accuracy

**Target:** 48.000 kHz  
**BBGW McASP:** Configurable via Device Tree and ALSA  
**Tolerance:** ±50 ppm (±2.4 Hz @ 48 kHz)  

**To verify:**
```bash
# On BBGW, check actual sample rate:
aplay -l
cat /proc/asound/card0/pcm0p/sub0/hw_params
```

Expected output:
```
access: RW_INTERLEAVED
format: S16_LE
subformat: STD
channels: 2
rate: 48000 (48000/1)
period_size: 1024
buffer_size: 4096
```

### Noise and Interference

**Potential Noise Sources:**
- Wi-Fi radios (BBGW 2.4 GHz, ESP32 2.4 GHz + Bluetooth)
- Switching power supplies
- Digital ground bounce

**Mitigation:**
- Use linear power supply or well-filtered switching supply
- Keep I2S wires away from Wi-Fi antennas (>5 cm)
- Use ferrite beads on long power cables
- Add 0.1 µF decoupling caps close to BBGW P9 header

**Measured Performance (typical):**
- **SNR**: >90 dB (limited by Bluetooth A2DP codec, not I2S)
- **THD+N**: <0.01% @ 1 kHz (McASP DAC performance)
- **Latency**: 10-30 ms (BBGW to ESP32 I2S), +100-200 ms (Bluetooth A2DP)

---

## Bill of Materials (BOM)

### Required Components

| Item | Description | Quantity | Notes |
|------|-------------|----------|-------|
| **BeagleBone Green Wireless** | BBGW development board | 1 | Seeed Studio or DigiKey |
| **ESP32 Dev Board** | ESP32-DevKitC or equivalent | 1 | Must support I2S and Bluetooth |
| **MicroSD Card** | 8-32 GB, Class 10 | 1 | For BBGW OS (optional if using eMMC) |
| **5V Power Supply** | 5V @ 1A, barrel jack | 1 | For BBGW (or USB power) |
| **USB Cable (Micro-B)** | For ESP32 programming | 1 | USB to Micro-B or USB-C |
| **Jumper Wires** | Male-to-male, 10-20 cm | 10+ | For breadboard connections |
| **Breadboard** | Half-size or full-size | 1 | For prototyping (optional) |

### Optional Components

| Item | Description | Quantity | Notes |
|------|-------------|----------|-------|
| **Logic Analyzer** | 4+ channels, ≥10 MHz | 1 | For I2S signal verification (Saleae, etc.) |
| **Oscilloscope** | 2+ channels, ≥20 MHz | 1 | For analog signal verification |
| **Decoupling Capacitors** | 0.1 µF ceramic | 5+ | For breadboard power rails |
| **Ferrite Beads** | For power supply filtering | 2+ | Reduce high-frequency noise |
| **Shielded Cable** | For I2S signals | 1 m | Better noise immunity (optional) |

**Total Cost (approximate):**
- **Minimal setup**: $80-$120 (BBGW + ESP32 + cables)
- **Full setup with tools**: $200-$400 (includes logic analyzer)

---

## Safety and Handling

### ESD Precautions

⚡ **Both BBGW and ESP32 are ESD-sensitive devices**

**Best practices:**
- Use anti-static wrist strap when handling boards
- Work on anti-static mat if available
- Store boards in anti-static bags when not in use
- Do not plug/unplug connections while powered

### Electrical Safety

**Voltage Limits:**
- **BBGW GPIO**: 3.3V maximum (exceeding will damage SoC)
- **ESP32 GPIO**: 3.6V absolute maximum
- **Power Input**: 5V only (do not use 12V or higher)

**Current Limits:**
- **BBGW GPIO**: 6-8 mA per pin (see AM335x datasheet)
- **BBGW 3.3V supply**: 250 mA total
- **ESP32 max current**: 500+ mA (during Wi-Fi/BT transmission)

**Do NOT:**
- Connect 5V signals directly to BBGW GPIO
- Exceed current limits (use external power for ESP32)
- Hot-plug I2S or UART while powered (can cause latch-up)

### Thermal Considerations

**Normal Operating Temperature:**
- **BBGW**: 0°C to 70°C (ambient)
- **ESP32**: -40°C to 85°C (ambient)

**Cooling:**
- BBGW AM335x SoC may warm up during operation (40-60°C typical)
- Add heatsink if enclosure temperature >50°C
- Ensure adequate airflow in enclosed designs

---

## Troubleshooting Hardware Issues

### No I2S Output

**Check:**
1. ✓ Device Tree overlay loaded: `dmesg | grep mcasp`
2. ✓ ALSA device exists: `aplay -l`
3. ✓ Wiring correct: BCLK, WS, Data, GND connected
4. ✓ Logic levels: Use multimeter to verify 3.3V on idle signals
5. ✓ ESP32 I2S configured: Check esp_bt_audio_source logs

**Verify with oscilloscope/logic analyzer:**
- BCLK should toggle at 1.536 MHz even with silence
- WS should toggle at 48 kHz
- Data should be valid (even if silent, will show zeros)

### UART Not Working

**Check:**
1. ✓ `/dev/ttyO4` exists: `ls -l /dev/ttyO4`
2. ✓ Permissions: User in `dialout` group
3. ✓ Wiring: TX ↔ RX crossed, GND connected
4. ✓ Baud rate: 115200 on both sides
5. ✓ Loopback test: Connect P9.11 to P9.13, echo test

**Verify with multimeter:**
- UART4_TXD (P9.13) should be ~3.3V when idle
- UART4_RXD (P9.11) should be ~3.3V when idle (pulled up)

### Logic Level Mismatch

**Symptoms:**
- Intermittent communication
- Garbled data
- No signal detected

**Check:**
- Both devices should be at 3.3V (measure with multimeter)
- Ensure common ground connection
- Verify no 5V signals on any pin

### Grounding Issues

**Symptoms:**
- Noisy audio
- UART errors
- Random resets

**Fix:**
- Connect all grounds together at a single point
- Use short, thick ground wires (22-24 AWG)
- Add decoupling capacitors (0.1 µF) near BBGW and ESP32

---

## Next Steps

After completing hardware setup:

1. **Device Tree Configuration** (Phase 1):
   - Create McASP I2S overlay
   - Create UART4 overlay
   - Compile and load overlays

2. **Code Adaptation** (Phase 2):
   - Update UART device to `/dev/ttyO4`
   - Verify ALSA device name
   - Update config.yaml

3. **Hardware Validation** (Phase 3):
   - Run Milestone 1 test (I2S tone generation)
   - Run Milestone 2 test (UART commands)
   - Run Milestone 3 test (Web UI)

**See Also:**
- [PIN_MAPPING.md](PIN_MAPPING.md) — Detailed pin reference
- [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md) — Device Tree overlay creation (Phase 1)
- [RESEARCH_NOTES.md](RESEARCH_NOTES.md) — Technical research findings

---

## References

### Official Documentation
- [BeagleBone Green Wireless Product Page](https://beagleboard.org/green-wireless)
- [AM335x Technical Reference Manual](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
- [AM335x Datasheet](https://www.ti.com/lit/ds/symlink/am3358.pdf)
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)

### Pinout References
- [BeagleBone Green Pinout](https://github.com/beagleboard/beaglebone-green/wiki/System-Reference-Manual)
- [AM335x Pin Mux Spreadsheet](https://www.ti.com/tool/PROCESSOR-SDK-AM335X)
- [ESP32 Pinout Reference](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)

### I2S Standards
- [I2S Bus Specification](https://www.sparkfun.com/datasheets/BreakoutBoards/I2SBUS.pdf)
- [McASP User Guide](https://www.ti.com/lit/ug/sprugw6/sprugw6.pdf)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** AI Assistant (GitHub Copilot)  
**License:** MIT (same as parent project)
