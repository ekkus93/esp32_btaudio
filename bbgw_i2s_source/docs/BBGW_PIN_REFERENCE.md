# BeagleBone Green Wireless Pin Reference

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Complete pin reference for I2S and UART configuration  
**Date:** 2026-02-07

---

## Overview

This document provides a comprehensive pin reference for the BeagleBone Green Wireless, focusing on pins used for I2S audio output and UART communication with the ESP32.

**BeagleBone Green Wireless Expansion Headers:**
- **P8 Header:** 46 pins (2×23 pins)
- **P9 Header:** 46 pins (2×23 pins)
- **Total:** 92 expansion pins

**This Project Uses:**
- **I2S (McASP0):** P9.31, P9.29, P9.28 (3 pins)
- **UART4:** P9.13, P9.11 (2 pins)
- **Ground:** P9.1, P9.2, P9.43, P9.44 (multiple ground pins)
- **Total:** 5 signal pins + grounds

**Related Documentation:**
- [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md) - Hardware wiring guide
- [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md) - Device Tree configuration
- [MILESTONE1_HARDWARE_SETUP_BBGW.md](MILESTONE1_HARDWARE_SETUP_BBGW.md) - I2S setup details

---

## Table of Contents

1. [P9 Header Complete Pinout](#p9-header-complete-pinout)
2. [I2S (McASP) Pins](#i2s-mcasp-pins)
3. [UART4 Pins](#uart4-pins)
4. [GPIO Numbering](#gpio-numbering)
5. [Pin Multiplexing](#pin-multiplexing)
6. [ESP32 Pin Reference](#esp32-pin-reference)
7. [Quick Reference Tables](#quick-reference-tables)

---

## P9 Header Complete Pinout

### P9 Header Layout (Top View)

```
         ┌───────────────────────────────┐
         │  BeagleBone Green Wireless    │
         │         P9 Header             │
         ├─────┬─────┬─────┬─────┬───────┤
    P9.1 │ GND │     │     │     │ DGND  │ P9.2
    P9.3 │ 3V3 │     │     │     │ 3V3   │ P9.4
    P9.5 │ VDD │     │     │     │ VDD   │ P9.6
    P9.7 │ SYS │     │     │     │ SYS   │ P9.8
    P9.9 │ PWR │     │     │     │ RST   │ P9.10
   P9.11 │ RX4 │     │     │     │ I2C2  │ P9.12
   P9.13 │ TX4 │     │     │     │ EHRP  │ P9.14
   P9.15 │ GPIO│     │     │     │ EHRP  │ P9.16
   P9.17 │ I2C │     │     │     │ I2C   │ P9.18
   P9.19 │ I2C │     │     │     │ I2C   │ P9.20
   P9.21 │ SPI │     │     │     │ SPI   │ P9.22
   P9.23 │ GPIO│     │     │     │ CAN   │ P9.24
   P9.25 │ GPIO│     │     │     │ CAN   │ P9.26
   P9.27 │ GPIO│     │     │     │ DOUT  │ P9.28 *
   P9.29 │ WS  │*    │     │     │ SPI   │ P9.30
   P9.31 │ BCK │*    │     │     │ VDD   │ P9.32
   P9.33 │ AIN │     │     │     │ AGND  │ P9.34
   P9.35 │ AIN │     │     │     │ AIN   │ P9.36
   P9.37 │ AIN │     │     │     │ AIN   │ P9.38
   P9.39 │ AIN │     │     │     │ AIN   │ P9.40
   P9.41 │ CLK │     │     │     │ GPIO  │ P9.42
   P9.43 │ GND │     │     │     │ GND   │ P9.44
   P9.45 │ GND │     │     │     │ GND   │ P9.46
         └─────┴─────┴─────┴─────┴───────┘

* = Used for I2S Audio
```

### P9 Header Complete Pin Table

| Pin | Name | Function | GPIO | Notes |
|-----|------|----------|------|-------|
| P9.1 | DGND | Digital Ground | - | Ground reference |
| P9.2 | DGND | Digital Ground | - | Ground reference |
| P9.3 | VDD_3V3 | 3.3V Power | - | 250 mA max |
| P9.4 | VDD_3V3 | 3.3V Power | - | 250 mA max |
| P9.5 | VDD_5V | 5V Power | - | From USB/barrel |
| P9.6 | VDD_5V | 5V Power | - | From USB/barrel |
| P9.7 | SYS_5V | System 5V | - | From PMIC |
| P9.8 | SYS_5V | System 5V | - | From PMIC |
| P9.9 | PWR_BUT | Power Button | - | System reset |
| P9.10 | SYS_RESETN | System Reset | - | Reset signal |
| **P9.11** | **UART4_RXD** | **UART4 RX** | **GPIO0_30** | **Used for ESP32 UART** |
| P9.12 | I2C2_SDA | I2C2 Data | GPIO1_28 | Alt: GPIO |
| **P9.13** | **UART4_TXD** | **UART4 TX** | **GPIO0_31** | **Used for ESP32 UART** |
| P9.14 | EHRPWM1A | PWM Output | GPIO1_18 | Alt: GPIO |
| P9.15 | GPIO1_16 | GPIO | GPIO1_16 | General purpose |
| P9.16 | EHRPWM1B | PWM Output | GPIO1_19 | Alt: GPIO |
| P9.17 | I2C1_SCL | I2C1 Clock | GPIO0_5 | Alt: SPI |
| P9.18 | I2C1_SDA | I2C1 Data | GPIO0_4 | Alt: SPI |
| P9.19 | I2C2_SCL | I2C2 Clock | GPIO0_13 | Alt: UART |
| P9.20 | I2C2_SDA | I2C2 Data | GPIO0_12 | Alt: UART |
| P9.21 | SPI0_D0 | SPI MISO | GPIO0_3 | Alt: UART |
| P9.22 | SPI0_SCLK | SPI Clock | GPIO0_2 | Alt: UART |
| P9.23 | GPIO1_17 | GPIO | GPIO1_17 | General purpose |
| P9.24 | UART1_TXD | UART1 TX | GPIO0_15 | Alt: CAN |
| P9.25 | GPIO3_21 | GPIO | GPIO3_21 | General purpose |
| P9.26 | UART1_RXD | UART1 RX | GPIO0_14 | Alt: CAN |
| P9.27 | GPIO3_19 | GPIO | GPIO3_19 | General purpose |
| **P9.28** | **McASP0_AXR1** | **I2S DOUT** | **GPIO3_17** | **Used for I2S Data** |
| **P9.29** | **McASP0_FSX** | **I2S WS** | **GPIO3_15** | **Used for I2S Word Select** |
| P9.30 | SPI1_D1 | SPI MOSI | GPIO3_16 | Alt: GPIO |
| **P9.31** | **McASP0_ACLKX** | **I2S BCLK** | **GPIO3_14** | **Used for I2S Bit Clock** |
| P9.32 | VDD_ADC | ADC Power | - | 1.8V reference |
| P9.33 | AIN4 | Analog Input | - | ADC channel 4 |
| P9.34 | GNDA_ADC | Analog Ground | - | ADC ground |
| P9.35 | AIN6 | Analog Input | - | ADC channel 6 |
| P9.36 | AIN5 | Analog Input | - | ADC channel 5 |
| P9.37 | AIN2 | Analog Input | - | ADC channel 2 |
| P9.38 | AIN3 | Analog Input | - | ADC channel 3 |
| P9.39 | AIN0 | Analog Input | - | ADC channel 0 |
| P9.40 | AIN1 | Analog Input | - | ADC channel 1 |
| P9.41 | CLKOUT2 | Clock Output | GPIO0_20 | Alt: GPIO |
| P9.42 | GPIO0_7 | GPIO | GPIO0_7 | General purpose |
| P9.43 | GND | Ground | - | Ground reference |
| P9.44 | GND | Ground | - | Ground reference |
| P9.45 | GND | Ground | - | Ground reference |
| P9.46 | GND | Ground | - | Ground reference |

---

## I2S (McASP) Pins

### McASP0 Pin Configuration

| Pin | Signal | Function | GPIO | Mode | Direction |
|-----|--------|----------|------|------|-----------|
| **P9.31** | **BCLK** | Bit Clock (ACLKX) | GPIO3_14 (GPIO110) | Mode 4 | Output (Master) |
| **P9.29** | **WS** | Word Select (FSX) | GPIO3_15 (GPIO111) | Mode 4 | Output (Master) |
| **P9.28** | **DOUT** | Data Out (AXR1) | GPIO3_17 (GPIO112) | Mode 4 | Output (TX) |

### I2S Signal Characteristics

**Bit Clock (BCLK) - P9.31:**
- **Frequency:** 3.072 MHz @ 48 kHz sample rate (32-bit slots)
- **Formula:** `BCLK = Sample Rate × Channels × Slot Width`
  - `48000 Hz × 2 channels × 32 bits = 3.072 MHz`
- **Waveform:** Square wave, 50% duty cycle
- **Voltage:** 3.3V logic level
- **Load:** Drives ESP32 GPIO26 input

**Word Select (WS / LRCLK) - P9.29:**
- **Frequency:** 48 kHz (sample rate)
- **Function:** Left/Right channel select
  - Low = Left channel
  - High = Right channel
- **Waveform:** Square wave, toggles each sample
- **Voltage:** 3.3V logic level
- **Load:** Drives ESP32 GPIO25 input

**Data Out (DOUT) - P9.28:**
- **Format:** I2S standard format
- **Bit Depth:** 16-bit samples (S16_LE) in 32-bit slots
- **Data Rate:** 3.072 Mbps (bit clock)
- **Alignment:** MSB first
- **Voltage:** 3.3V logic level
- **Load:** Drives ESP32 GPIO22 input

### Pin Mux Settings (Device Tree)

```c
mcasp0_pins: pinmux_mcasp0_pins {
    pinctrl-single,pins = <
        0x190 0x20  /* P9.31: mcasp0_aclkx, MODE0 | OUTPUT | PULLDOWN */
        0x194 0x20  /* P9.29: mcasp0_fsx, MODE0 | OUTPUT | PULLDOWN */
        0x19c 0x22  /* P9.28: mcasp0_axr1, MODE2 | OUTPUT | PULLDOWN */
    >;
};
```

**Pin Control Register Offsets:**
- P9.31 (BCLK): 0x190
- P9.29 (WS): 0x194
- P9.28 (DOUT): 0x19c

**Pin Control Values:**
- `0x20`: Mode 0, Output, Pulldown, Receiver disabled
- `0x22`: Mode 2, Output, Pulldown, Receiver disabled

---

## UART4 Pins

### UART4 Pin Configuration

| Pin | Signal | Function | GPIO | Mode | Direction |
|-----|--------|----------|------|------|-----------|
| **P9.13** | **TX** | UART4 Transmit | GPIO0_31 (GPIO31) | Mode 6 | Output |
| **P9.11** | **RX** | UART4 Receive | GPIO0_30 (GPIO30) | Mode 6 | Input |

### UART4 Signal Characteristics

**Transmit (TX) - P9.13:**
- **Function:** Transmits data from BBGW to ESP32
- **Connection:** BBGW P9.13 → ESP32 GPIO16 (RX)
- **Baud Rate:** 115200 bps (software configurable)
- **Format:** 8N1 (8 data bits, no parity, 1 stop bit)
- **Voltage:** 3.3V logic level
- **Idle State:** High (mark)

**Receive (RX) - P9.11:**
- **Function:** Receives data from ESP32 to BBGW
- **Connection:** BBGW P9.11 ← ESP32 GPIO17 (TX)
- **Baud Rate:** 115200 bps (must match ESP32)
- **Format:** 8N1
- **Voltage:** 3.3V logic level
- **Idle State:** High (mark)

### Pin Mux Settings (Device Tree)

```c
uart4_pins: pinmux_uart4_pins {
    pinctrl-single,pins = <
        0x070 0x26  /* P9.11: uart4_rxd, MODE6 | INPUT_PULLUP */
        0x074 0x06  /* P9.13: uart4_txd, MODE6 | OUTPUT_PULLDOWN */
    >;
};
```

**Pin Control Register Offsets:**
- P9.11 (RX): 0x070
- P9.13 (TX): 0x074

**Pin Control Values:**
- `0x26`: Mode 6, Input, Pullup enabled, Receiver enabled
- `0x06`: Mode 6, Output, Pulldown, Receiver disabled

### UART Wiring (TX/RX Crossover)

**Critical:** UART TX and RX must be crossed over between BBGW and ESP32:

```
BeagleBone P9                ESP32
┌────────────────┐          ┌─────────────┐
│ P9.13 (TX) ────┼─────────→│ GPIO16 (RX) │
│                │          │             │
│ P9.11 (RX) ←───┼──────────│ GPIO17 (TX) │
│                │          │             │
│ P9.1  (GND)────┼──────────│ GND         │
└────────────────┘          └─────────────┘
```

---

## GPIO Numbering

### GPIO Numbering Scheme

BeagleBone uses multiple GPIO numbering schemes:

**1. Physical Pin Number:** P9.31, P9.29, etc.  
**2. GPIO Chip/Bank:** GPIO0, GPIO1, GPIO2, GPIO3  
**3. GPIO Offset:** 0-31 within each bank  
**4. Linux GPIO Number:** Calculated as `(Bank × 32) + Offset`

### I2S Pin GPIO Numbers

| Pin | Bank | Offset | Linux GPIO | Calculation |
|-----|------|--------|------------|-------------|
| P9.31 | GPIO3 | 14 | **110** | (3 × 32) + 14 = 110 |
| P9.29 | GPIO3 | 15 | **111** | (3 × 32) + 15 = 111 |
| P9.28 | GPIO3 | 17 | **112** | (3 × 32) + 17 = 112 |

### UART4 Pin GPIO Numbers

| Pin | Bank | Offset | Linux GPIO | Calculation |
|-----|------|--------|------------|-------------|
| P9.13 | GPIO0 | 31 | **31** | (0 × 32) + 31 = 31 |
| P9.11 | GPIO0 | 30 | **30** | (0 × 32) + 30 = 30 |

### Using GPIO Numbers in Linux

```bash
# Export GPIO (for manual control)
echo 110 > /sys/class/gpio/export

# Set direction
echo out > /sys/class/gpio/gpio110/direction

# Set value
echo 1 > /sys/class/gpio/gpio110/value

# Read value
cat /sys/class/gpio/gpio110/value

# Unexport GPIO
echo 110 > /sys/class/gpio/unexport
```

**Note:** When using Device Tree overlays for McASP/UART, GPIO control is handled by kernel drivers. Manual GPIO export not needed.

---

## Pin Multiplexing

### AM335x Pin Multiplexing Modes

Each physical pin on the AM335x can be configured for up to 8 different functions (modes 0-7):

**Example: P9.31 Pin Multiplexing**

| Mode | Function | Description |
|------|----------|-------------|
| 0 | SPI1_SCLK | SPI1 clock |
| 1 | ECAP0_IN_PWM0_OUT | Enhanced Capture / PWM |
| 2 | UART3_TXD | UART3 transmit |
| 3 | UART2_RXD | UART2 receive |
| **4** | **McASP0_ACLKX** | **McASP0 bit clock (I2S)** ← **Used** |
| 5 | MMC0_SDCD | SD card detect |
| 6 | UART1_TXD | UART1 transmit |
| 7 | GPIO3_14 | General purpose I/O |

### Pin Mux Configuration Bits

Pin mux is configured via control module registers (32-bit values):

```
Bits [2:0] - Mode Select
  000 = Mode 0
  001 = Mode 1
  010 = Mode 2
  011 = Mode 3
  100 = Mode 4  ← McASP I2S
  101 = Mode 5
  110 = Mode 6  ← UART
  111 = Mode 7 (GPIO)

Bit [3] - Pull Up/Down Enable
  0 = Pull disabled
  1 = Pull enabled

Bit [4] - Pull Type Select
  0 = Pull down
  1 = Pull up

Bit [5] - Receiver Enable
  0 = Receiver disabled (output pin)
  1 = Receiver enabled (input pin)

Bit [6] - Slew Rate
  0 = Fast
  1 = Slow
```

**Example:** `0x20` = `0b00100000`
- Mode 0 (bits [2:0] = 000)
- Pull disabled (bit [3] = 0)
- Pull down (bit [4] = 0)
- Receiver disabled (bit [5] = 1)
- Fast slew (bit [6] = 0)

---

## ESP32 Pin Reference

### ESP32 DevKitC Pins Used

| ESP32 Pin | Function | Signal | Direction | Connects To BBGW |
|-----------|----------|--------|-----------|------------------|
| **GPIO26** | I2S BCK | Bit Clock | Input (Slave) | P9.31 (BCLK) |
| **GPIO25** | I2S WS | Word Select | Input (Slave) | P9.29 (WS) |
| **GPIO22** | I2S DIN | Data In | Input (RX) | P9.28 (DOUT) |
| **GPIO16** | UART RX | Receive | Input | P9.13 (TX) |
| **GPIO17** | UART TX | Transmit | Output | P9.11 (RX) |
| **GND** | Ground | Ground | - | P9.1, P9.2 |

### ESP32 I2S Configuration

```c
i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_RX,  // Slave mode, receive
    .sample_rate = 48000,                   // Must match BBGW
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
};

i2s_pin_config_t pin_config = {
    .bck_io_num = 26,      // Bit clock
    .ws_io_num = 25,       // Word select
    .data_out_num = -1,    // Not used (BBGW transmits)
    .data_in_num = 22      // Data input from BBGW
};
```

### ESP32 UART Configuration

```c
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
};

// UART pins
#define UART_RX_PIN 16  // Receives from BBGW TX
#define UART_TX_PIN 17  // Transmits to BBGW RX
```

---

## Quick Reference Tables

### Pins Used in This Project

| Function | BBGW Pin | BBGW GPIO | ESP32 Pin | Signal | Direction |
|----------|----------|-----------|-----------|--------|-----------|
| I2S BCLK | P9.31 | GPIO110 | GPIO26 | Bit Clock | BBGW → ESP32 |
| I2S WS | P9.29 | GPIO111 | GPIO25 | Word Select | BBGW → ESP32 |
| I2S DOUT | P9.28 | GPIO112 | GPIO22 | Data Out | BBGW → ESP32 |
| UART TX | P9.13 | GPIO31 | GPIO16 | Transmit | BBGW → ESP32 |
| UART RX | P9.11 | GPIO30 | GPIO17 | Receive | BBGW ← ESP32 |
| Ground | P9.1, P9.2 | - | GND | Ground | Common |

### Wiring Checklist

- [ ] P9.31 → GPIO26 (BCLK)
- [ ] P9.29 → GPIO25 (WS)
- [ ] P9.28 → GPIO22 (DOUT)
- [ ] P9.13 → GPIO16 (TX→RX crossover)
- [ ] P9.11 ← GPIO17 (RX←TX crossover)
- [ ] P9.1 → GND (at least 2 ground wires)
- [ ] P9.2 → GND

### Signal Levels

| Parameter | BBGW | ESP32 | Compatible? |
|-----------|------|-------|-------------|
| Logic High | 3.3V | 3.3V | ✅ Yes |
| Logic Low | 0V | 0V | ✅ Yes |
| Max Voltage | 3.3V | 3.6V | ✅ Yes |
| Input Current | - | <40 mA | ✅ Yes |

**No level shifters required** — both BBGW and ESP32 use 3.3V logic.

---

## References

### BeagleBone Documentation
- [BeagleBone Green Wireless System Reference](https://github.com/SeeedDocument/BeagleBone_Green_Wireless/blob/master/res/BBGW_SRM.pdf)
- [AM335x Pin Mux Utility](https://dev.ti.com/pinmux/)
- [BeagleBone Pin Reference](https://beagleboard.org/Support/bone101)

### ESP32 Documentation
- [ESP32 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)

### Project Documentation
- [Hardware Setup Guide](HARDWARE_SETUP_BBGW.md)
- [Device Tree Guide](BBGW_DEVICE_TREE_GUIDE.md)
- [Milestone 1 I2S Setup](MILESTONE1_HARDWARE_SETUP_BBGW.md)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
