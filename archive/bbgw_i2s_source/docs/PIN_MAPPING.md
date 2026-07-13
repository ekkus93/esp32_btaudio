# Pin Mapping Reference — BBGW I2S Source

**Platform:** BeagleBone Green Wireless (AM335x)  
**Target:** ESP32 Bluetooth Audio Source  
**Last Updated:** 2026-02-07

---

## Overview

This document provides a comprehensive pin reference for the **bbgw_i2s_source** project, including:
- Complete P8/P9 header pinout
- McASP I2S pin assignments
- UART4 pin assignments
- GPIO numbering and mux modes
- Device Tree configuration references

**Quick Reference:**
- **I2S Pins**: P9.31 (BCLK), P9.29 (WS), P9.28 (Data)
- **UART Pins**: P9.11 (RX), P9.13 (TX)
- **Ground**: P9.1, P9.2

---

## BeagleBone Green Wireless P9 Header Pinout

### Complete P9 Header (46 pins)

The P9 header is a 2×23 pin header located on the side of the BBGW board.

**Orientation:** Pin 1 is closest to the DC barrel jack.

```
P9 Header Layout (Top View)

        ┌─────────────────────┐
   GND  │  1 ●  ● 2   │  GND
  3.3V  │  3 ●  ● 4   │  3.3V
  VDD5V │  5 ●  ● 6   │  VDD5V
  SYS5V │  7 ●  ● 8   │  SYS5V
   PWR  │  9 ●  ● 10  │  RST
  UART4 │ 11 ●  ● 12  │  GPIO1_28
  UART4 │ 13 ●  ● 14  │  EHRPWM1A
   GPIO │ 15 ●  ● 16  │  EHRPWM1B
   I2C1 │ 17 ●  ● 18  │  I2C1
   I2C2 │ 19 ●  ● 20  │  I2C2
  UART2 │ 21 ●  ● 22  │  UART2
   GPIO │ 23 ●  ● 24  │  UART1
   GPIO │ 25 ●  ● 26  │  UART1
   GPIO │ 27 ●  ● 28  │  McASP (AXR1) **
McASP ● │ 29 ●  ● 30  │  McASP (AXR0)
McASP ● │ 31 ●  ● 32  │  VDD_ADC
   VDD  │ 33 ●  ● 34  │  GNDA_ADC
   AIN4 │ 35 ●  ● 36  │  AIN5
   AIN2 │ 37 ●  ● 38  │  AIN3
   AIN0 │ 39 ●  ● 40  │  AIN1
  CLKOUT│ 41 ●  ● 42  │  GPIO0_7
   GND  │ 43 ●  ● 44  │  GND
   GND  │ 45 ●  ● 46  │  GND
        └─────────────────────┘
        
** Pins used in this project
```

### Detailed P9 Pin Table

| Pin | Name | GPIO | Mode 0 | Mode 1 | Mode 2 | Mode 6 | Notes |
|-----|------|------|--------|--------|--------|--------|-------|
| 1 | GND | - | Ground | - | - | - | Digital ground |
| 2 | GND | - | Ground | - | - | - | Digital ground |
| 3 | VDD_3V3 | - | 3.3V | - | - | - | 250 mA max |
| 4 | VDD_3V3 | - | 3.3V | - | - | - | 250 mA max |
| 5 | VDD_5V | - | 5V | - | - | - | From DC input |
| 6 | VDD_5V | - | 5V | - | - | - | From DC input |
| 7 | SYS_5V | - | 5V | - | - | - | System 5V |
| 8 | SYS_5V | - | 5V | - | - | - | System 5V |
| 9 | PWR_BUT | - | Power Button | - | - | - | Power control |
| 10 | SYS_RESETn | - | Reset | - | - | - | System reset |
| **11** | **GPIO0_30** | **30** | **gpmc_wait0** | mii2_crs | gpmc_csn4 | **uart4_rxd** | **UART4 RX ●** |
| 12 | GPIO1_28 | 60 | gpmc_ben1 | mii2_col | gpmc_csn6 | mcasp0_aclkr | GPIO |
| **13** | **GPIO0_31** | **31** | **gpmc_wpn** | mii2_rxerr | gpmc_csn5 | **uart4_txd** | **UART4 TX ●** |
| 14 | GPIO1_18 | 50 | gpmc_a2 | mii2_txd3 | rgmii2_td3 | ehrpwm1A | PWM |
| 15 | GPIO1_16 | 48 | gpmc_a0 | mii2_txen | rgmii2_tctl | gpio1_16 | GPIO |
| 16 | GPIO1_19 | 51 | gpmc_a3 | mii2_txd2 | rgmii2_td2 | ehrpwm1B | PWM |
| 17 | GPIO0_5 | 5 | spi0_cs0 | mmc2_sdwp | i2c1_scl | ehrpwm0_synci | I2C1 SCL |
| 18 | GPIO0_4 | 4 | spi0_d1 | mmc1_sdwp | i2c1_sda | ehrpwm0_tripzone | I2C1 SDA |
| 19 | GPIO0_13 | 13 | uart1_rtsn | timer5 | dcan0_rx | i2c2_scl | I2C2 SCL |
| 20 | GPIO0_12 | 12 | uart1_ctsn | timer6 | dcan0_tx | i2c2_sda | I2C2 SDA |
| 21 | GPIO0_3 | 3 | spi0_d0 | uart2_txd | i2c2_scl | ehrpwm0B | UART2 TX |
| 22 | GPIO0_2 | 2 | spi0_sclk | uart2_rxd | i2c2_sda | ehrpwm0A | UART2 RX |
| 23 | GPIO1_17 | 49 | gpmc_a1 | mii2_rxdv | rgmii2_rctl | gpio1_17 | GPIO |
| 24 | GPIO0_15 | 15 | uart1_txd | mmc2_sdwp | dcan1_rx | uart1_txd | UART1 TX |
| 25 | GPIO3_21 | 117 | mcasp0_ahclkx | eQEP0_strobe | mcasp0_axr3 | gpio3_21 | GPIO |
| 26 | GPIO0_14 | 14 | uart1_rxd | mmc1_sdwp | dcan1_tx | uart1_rxd | UART1 RX |
| 27 | GPIO3_19 | 115 | mcasp0_fsr | eQEP0B_in | mcasp0_axr3 | gpio3_19 | GPIO |
| **28** | **GPIO3_17** | **113** | **mcasp0_ahclkr** | ehrpwm0_synco | **mcasp0_axr1** | spi1_cs0 | **McASP AXR1 ●** |
| **29** | **GPIO3_15** | **111** | **mcasp0_fsx** | ehrpwm0B | - | spi1_d0 | **McASP FSX ●** |
| **30** | **GPIO3_16** | **112** | **mcasp0_axr0** | ehrpwm0_tripzone | - | spi1_d1 | **McASP AXR0** |
| **31** | **GPIO3_14** | **110** | **mcasp0_aclkx** | ehrpwm0A | - | spi1_sclk | **McASP ACLKX ●** |
| 32 | VDD_ADC | - | ADC Vref | - | - | - | 1.8V |
| 33 | AIN4 | - | ADC input 4 | - | - | - | 0-1.8V |
| 34 | GNDA_ADC | - | ADC ground | - | - | - | Analog GND |
| 35 | AIN6 | - | ADC input 6 | - | - | - | 0-1.8V |
| 36 | AIN5 | - | ADC input 5 | - | - | - | 0-1.8V |
| 37 | AIN2 | - | ADC input 2 | - | - | - | 0-1.8V |
| 38 | AIN3 | - | ADC input 3 | - | - | - | 0-1.8V |
| 39 | AIN0 | - | ADC input 0 | - | - | - | 0-1.8V |
| 40 | AIN1 | - | ADC input 1 | - | - | - | 0-1.8V |
| 41 | GPIO0_20 | 20 | xdma_event_intr1 | - | tclkin | clkout2 | Clock out |
| 42 | GPIO0_7 | 7 | ecap0_in_pwm0_out | uart3_txd | spi1_cs1 | pr1_ecap0_ecap_capin | GPIO |
| 43 | GND | - | Ground | - | - | - | Digital GND |
| 44 | GND | - | Ground | - | - | - | Digital GND |
| 45 | GND | - | Ground | - | - | - | Digital GND |
| 46 | GND | - | Ground | - | - | - | Digital GND |

**Legend:**
- **●** = Pins used in this project
- Mode 0 = Default function after reset
- Mode 6 = Alternate function (UART4 for P9.11/P9.13)
- GPIO number = Linux GPIO number (for sysfs, gpiod)

---

## McASP I2S Pin Assignments

### McASP0 Pins for I2S

The AM335x McASP0 module has multiple pins available on the P9 header. For standard I2S (transmit only), we use:

| Signal | P9 Pin | GPIO | Mux Mode | Direction | Function | ESP32 Pin |
|--------|--------|------|----------|-----------|----------|-----------|
| **ACLKX** | **P9.31** | GPIO3_14 (110) | Mode 0 | Output | I2S Bit Clock | GPIO26 (BCLK) |
| **FSX** | **P9.29** | GPIO3_15 (111) | Mode 0 | Output | I2S Word Select | GPIO25 (WS) |
| **AXR1** | **P9.28** | GPIO3_17 (113) | Mode 2 | Output | I2S Data Out | GPIO22 (DIN) |
| **AXR0** | **P9.30** | GPIO3_16 (112) | Mode 0 | Bidir | I2S Data (alt) | GPIO22 (DIN) |

**Recommended Configuration:**
- Use **P9.28 (AXR1)** for data output (requires Mode 2 pin mux)
- Alternatively, use **P9.30 (AXR0)** for data output (Mode 0, simpler Device Tree)

### I2S Signal Descriptions

**ACLKX (Audio Clock Transmit):**
- **Function**: I2S bit clock (BCLK)
- **Frequency**: Sample Rate × Bits per Frame = 48000 × 32 = 1.536 MHz
- **Duty Cycle**: 50%
- **Generated by**: McASP (master mode)
- **Logic Level**: 3.3V CMOS

**FSX (Frame Sync Transmit):**
- **Function**: I2S word select (WS / LRCLK)
- **Frequency**: Sample Rate = 48 kHz
- **Duty Cycle**: 50% (toggles every 16 BCLK cycles)
- **Phase**: Toggles 1 BCLK before first data bit
- **Logic Level**: 3.3V CMOS

**AXR1 / AXR0 (Audio Transmit/Receive):**
- **Function**: I2S serial data output
- **Format**: MSB-first, 16-bit samples, left-justified
- **Timing**: Valid on BCLK rising edge (I2S standard)
- **Logic Level**: 3.3V CMOS

### Device Tree Pin Mux Configuration

**Pin Control Register Offsets (from AM335x TRM):**

| Pin | Offset | Register Name | Mode 0 Value | Mode 2 Value | Notes |
|-----|--------|---------------|--------------|--------------|-------|
| P9.31 | 0x190 | conf_mcasp0_aclkx | 0x00 | - | McASP ACLKX (Mode 0) |
| P9.29 | 0x194 | conf_mcasp0_fsx | 0x00 | - | McASP FSX (Mode 0) |
| P9.30 | 0x198 | conf_mcasp0_axr0 | 0x00 | - | McASP AXR0 (Mode 0) |
| P9.28 | 0x19c | conf_mcasp0_ahclkr | - | 0x02 | McASP AXR1 (Mode 2) |

**Mux Mode Value Format:**
```
Bit 6: Slew rate (0=fast, 1=slow)
Bit 5: RX enable (0=disabled, 1=enabled for input)
Bit 4: Pull type (0=pull-down, 1=pull-up)
Bit 3: Pull enable (0=disabled, 1=enabled)
Bits 2-0: Mux mode (0-7)
```

**Example Device Tree Snippet:**
```dts
mcasp0_pins: pinmux_mcasp0_pins {
    pinctrl-single,pins = <
        0x190 0x00  /* P9.31 mcasp0_aclkx, OUTPUT | MODE0 */
        0x194 0x00  /* P9.29 mcasp0_fsx, OUTPUT | MODE0 */
        0x19c 0x02  /* P9.28 mcasp0_ahclkr -> mcasp0_axr1, OUTPUT | MODE2 */
    >;
};
```

---

## UART4 Pin Assignments

### UART4 Pins

The AM335x has 6 UART modules. UART4 is available on P9.11 and P9.13.

| Signal | P9 Pin | GPIO | Mux Mode | Direction | Function | ESP32 Pin |
|--------|--------|------|----------|-----------|----------|-----------|
| **UART4_RXD** | **P9.11** | GPIO0_30 (30) | Mode 6 | Input | UART Receive | GPIO1 or GPIO17 (TXD) |
| **UART4_TXD** | **P9.13** | GPIO0_31 (31) | Mode 6 | Output | UART Transmit | GPIO3 or GPIO16 (RXD) |

**Linux Device:** `/dev/ttyO4`

### UART Signal Descriptions

**UART4_RXD (Receive Data):**
- **Function**: Receive data from ESP32
- **Idle Level**: 3.3V (logic high)
- **Direction**: Input to BBGW (output from ESP32)
- **Pull**: Internal pull-up recommended

**UART4_TXD (Transmit Data):**
- **Function**: Transmit data to ESP32
- **Idle Level**: 3.3V (logic high)
- **Direction**: Output from BBGW (input to ESP32)
- **Drive**: Push-pull output

### UART Configuration

**Serial Parameters:**
- **Baud Rate**: 115200 bps
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Flow Control**: None (RTS/CTS not used)

### Device Tree Pin Mux Configuration

**Pin Control Register Offsets:**

| Pin | Offset | Register Name | Mode 6 Value | Notes |
|-----|--------|---------------|--------------|-------|
| P9.11 | 0x070 | conf_gpmc_wait0 | 0x26 | UART4 RXD (INPUT, PULLDOWN, MODE6) |
| P9.13 | 0x074 | conf_gpmc_wpn | 0x06 | UART4 TXD (OUTPUT, MODE6) |

**Mux Values Explained:**
- **0x26** = `0b00100110` = RX input, pull-down, mode 6
- **0x06** = `0b00000110` = TX output, no pull, mode 6

**Example Device Tree Snippet:**
```dts
uart4_pins: pinmux_uart4_pins {
    pinctrl-single,pins = <
        0x070 0x26  /* P9.11 gpmc_wait0.uart4_rxd, INPUT_PULLDOWN | MODE6 */
        0x074 0x06  /* P9.13 gpmc_wpn.uart4_txd, OUTPUT | MODE6 */
    >;
};
```

---

## GPIO Numbering

### GPIO Bank and Pin Calculation

The AM335x organizes GPIOs into 4 banks (GPIO0-GPIO3), each with 32 pins.

**Formula:**
```
Linux GPIO Number = (Bank × 32) + Pin
```

**Examples:**
- **P9.11** = GPIO0_30 = (0 × 32) + 30 = **GPIO 30**
- **P9.13** = GPIO0_31 = (0 × 32) + 31 = **GPIO 31**
- **P9.28** = GPIO3_17 = (3 × 32) + 17 = **GPIO 113**
- **P9.29** = GPIO3_15 = (3 × 32) + 15 = **GPIO 111**
- **P9.30** = GPIO3_16 = (3 × 32) + 16 = **GPIO 112**
- **P9.31** = GPIO3_14 = (3 × 32) + 14 = **GPIO 110**

### GPIO Access Methods

**Method 1: sysfs (legacy, deprecated):**
```bash
echo 30 > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio30/direction
echo 1 > /sys/class/gpio/gpio30/value
```

**Method 2: libgpiod (modern, recommended):**
```bash
# Command line
gpioset gpiochip0 30=1

# Python
from gpiod import chip
c = chip("gpiochip0")
line = c.get_line(30)
line.request(consumer="test", type=line.DIRECTION_OUTPUT)
line.set_value(1)
```

**Method 3: Adafruit_BBIO (Python, high-level):**
```python
import Adafruit_BBIO.GPIO as GPIO
GPIO.setup("P9_11", GPIO.OUT)
GPIO.output("P9_11", GPIO.HIGH)
```

**Note:** For this project, GPIO is **not needed** (McASP handles I2S, UART uses serial driver).

---

## Ground Connections

### Ground Pins on P9 Header

| Pin | Function | Notes |
|-----|----------|-------|
| **P9.1** | DGND | Digital ground (recommended for I2S/UART) |
| **P9.2** | DGND | Digital ground |
| **P9.43** | GND | Ground |
| **P9.44** | GND | Ground |
| **P9.45** | GND | Ground |
| **P9.46** | GND | Ground |

**Recommended Ground Strategy:**
- Use **P9.1** or **P9.2** for I2S and UART ground
- Connect to a single point on the breadboard
- Keep ground wire short and thick (22-24 AWG)
- Minimize ground loop area

### Analog vs Digital Ground

- **DGND (P9.1, P9.2)**: Digital ground for logic signals (I2S, UART, GPIO)
- **GNDA_ADC (P9.34)**: Analog ground for ADC inputs (not used in this project)

**Note:** On BBGW, DGND and GNDA_ADC are connected internally but should be kept separate on PCB.

---

## Power Pins

### Power Supply Pins on P9 Header

| Pin | Voltage | Max Current | Function |
|-----|---------|-------------|----------|
| **P9.3** | 3.3V | 250 mA | System 3.3V (from LDO) |
| **P9.4** | 3.3V | 250 mA | System 3.3V (from LDO) |
| **P9.5** | 5V | 1A | DC input passthrough |
| **P9.6** | 5V | 1A | DC input passthrough |
| **P9.7** | 5V | 1A | System 5V (regulated) |
| **P9.8** | 5V | 1A | System 5V (regulated) |

**Power Recommendations:**
- **Do NOT** power ESP32 from P9.3/P9.4 (3.3V limited to 250 mA)
- ESP32 can draw >500 mA during Wi-Fi/BT transmission
- Use **separate 5V supply** for ESP32 (via USB or Vin pin)
- Connect grounds together (BBGW GND ↔ ESP32 GND)

---

## ESP32 Pin Mapping

### ESP32 I2S Pins (Slave Mode)

| ESP32 Pin | Function | Direction | Connect To | Signal |
|-----------|----------|-----------|------------|--------|
| **GPIO26** | I2S_BCLK | Input | P9.31 (ACLKX) | Bit clock |
| **GPIO25** | I2S_WS | Input | P9.29 (FSX) | Word select |
| **GPIO22** | I2S_DIN | Input | P9.28 (AXR1) | Data in |

**ESP32 I2S Configuration (in esp_bt_audio_source firmware):**
```c
i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_RX,  // Slave mode (receives clocks)
    .sample_rate = 48000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    // ...
};

i2s_pin_config_t pin_config = {
    .bck_io_num = 26,   // BCLK
    .ws_io_num = 25,    // WS
    .data_in_num = 22,  // DIN
    .data_out_num = -1  // Not used
};
```

### ESP32 UART Pins

| ESP32 Pin | Function | Direction | Connect To | Signal | Notes |
|-----------|----------|-----------|------------|--------|-------|
| **GPIO1** | UART0_TXD | Output | P9.11 (UART4_RXD) | Transmit to BBGW | Default USB console |
| **GPIO3** | UART0_RXD | Input | P9.13 (UART4_TXD) | Receive from BBGW | Default USB console |
| **GPIO16** | UART2_RXD | Input | P9.13 (UART4_TXD) | Receive (alt) | Preferred |
| **GPIO17** | UART2_TXD | Output | P9.11 (UART4_RXD) | Transmit (alt) | Preferred |

**Recommendation:** Use **GPIO16/GPIO17 (UART2)** instead of GPIO1/GPIO3 to avoid conflict with USB console.

**ESP32 UART Configuration:**
```c
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
};
uart_param_config(UART_NUM_2, &uart_config);
uart_set_pin(UART_NUM_2, 17, 16, -1, -1);  // TXD=17, RXD=16
```

---

## Pin Mux Mode Reference

### Common Pin Mux Modes

The AM335x uses a pin mux system where each pin can have up to 8 different functions (Mode 0-7).

**Mode Selection (Bits 2-0):**
- **Mode 0**: Default function (usually peripheral like McASP, I2C, etc.)
- **Mode 1**: Alternate peripheral function
- **Mode 2**: Another alternate function
- **Mode 6**: Often UART or other alternate
- **Mode 7**: GPIO mode (software-controlled)

**Pin Configuration Bits:**
```
Bit 6: Slew rate
  0 = Fast (default)
  1 = Slow (reduces EMI)

Bit 5: RX Enable (input buffer)
  0 = Disabled (output only)
  1 = Enabled (input or bidirectional)

Bit 4: Pull type
  0 = Pull-down
  1 = Pull-up

Bit 3: Pull enable
  0 = Pull disabled
  1 = Pull enabled

Bits 2-0: Mux mode
  0-7 = Mode 0 through Mode 7
```

### Common Pin Mux Values

| Value | Binary | Mode | Pull | RX | Slew | Use Case |
|-------|--------|------|------|----|----- |----------|
| 0x00 | 0b00000000 | 0 | None | Off | Fast | Output (McASP, SPI CLK) |
| 0x01 | 0b00000001 | 1 | None | Off | Fast | Alternate output |
| 0x02 | 0b00000010 | 2 | None | Off | Fast | Alternate output |
| 0x06 | 0b00000110 | 6 | None | Off | Fast | UART TX (output) |
| 0x20 | 0b00100000 | 0 | None | On | Fast | Input (no pull) |
| 0x26 | 0b00100110 | 6 | None | On | Fast | UART RX (input) |
| 0x27 | 0b00100111 | 7 | None | On | Fast | GPIO input |
| 0x30 | 0b00110000 | 0 | PD | On | Fast | Input with pull-down |
| 0x37 | 0b00110111 | 7 | PU | On | Fast | GPIO in with pull-up |

**Legend:**
- PD = Pull-down
- PU = Pull-up
- RX = Receiver enabled (input buffer)

---

## Device Tree Overlay Complete Example

### Complete I2S + UART4 Overlay

**File:** `BB-BBGW-I2S-UART4-00A0.dts`

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-black", "ti,beaglebone-green";
    part-number = "BB-BBGW-I2S-UART4";
    version = "00A0";
    
    /* Declare exclusive use of pins */
    exclusive-use =
        /* McASP I2S pins */
        "P9.28",  /* mcasp0_axr1 */
        "P9.29",  /* mcasp0_fsx */
        "P9.31",  /* mcasp0_aclkx */
        /* UART4 pins */
        "P9.11",  /* uart4_rxd */
        "P9.13",  /* uart4_txd */
        /* Peripherals */
        "mcasp0",
        "uart4";
    
    /*
     * Pin mux configuration
     */
    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            /* McASP0 I2S pins */
            mcasp0_pins: pinmux_mcasp0_pins {
                pinctrl-single,pins = <
                    /* Offset | Mode (hex) | Description */
                    0x190 0x00  /* P9.31 mcasp0_aclkx, OUTPUT | MODE0 */
                    0x194 0x00  /* P9.29 mcasp0_fsx, OUTPUT | MODE0 */
                    0x19c 0x02  /* P9.28 mcasp0_ahclkr -> mcasp0_axr1, OUTPUT | MODE2 */
                >;
            };
            
            /* UART4 pins */
            uart4_pins: pinmux_uart4_pins {
                pinctrl-single,pins = <
                    0x070 0x26  /* P9.11 gpmc_wait0.uart4_rxd, INPUT_PULLDOWN | MODE6 */
                    0x074 0x06  /* P9.13 gpmc_wpn.uart4_txd, OUTPUT | MODE6 */
                >;
            };
        };
    };
    
    /*
     * McASP0 configuration for I2S
     */
    fragment@1 {
        target = <&mcasp0>;
        __overlay__ {
            status = "okay";
            pinctrl-names = "default";
            pinctrl-0 = <&mcasp0_pins>;
            
            op-mode = <0>;  /* MCASP_IIS_MODE */
            tdm-slots = <2>;
            num-serializer = <16>;
            
            /* Serial directions: 0=INACTIVE, 1=TX, 2=RX */
            serial-dir = <
                0 1 0 0  /* AXR0=inactive, AXR1=TX, AXR2-3=inactive */
                0 0 0 0
                0 0 0 0
                0 0 0 0
            >;
            
            tx-num-evt = <32>;
            rx-num-evt = <32>;
        };
    };
    
    /*
     * UART4 configuration
     */
    fragment@2 {
        target = <&uart4>;
        __overlay__ {
            status = "okay";
            pinctrl-names = "default";
            pinctrl-0 = <&uart4_pins>;
        };
    };
};
```

**Compilation:**
```bash
dtc -O dtb -o BB-BBGW-I2S-UART4-00A0.dtbo -b 0 -@ BB-BBGW-I2S-UART4-00A0.dts
sudo cp BB-BBGW-I2S-UART4-00A0.dtbo /lib/firmware/
```

**Loading (in /boot/uEnv.txt):**
```bash
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-UART4-00A0.dtbo
```

---

## Pin Conflict Check

### Potential Pin Conflicts

Some pins have multiple functions that may conflict:

**P9.28 (McASP AXR1):**
- Conflicts with: EHRPWM0_SYNCO (if using PWM)
- **Solution:** Don't use PWM on this pin

**P9.29 (McASP FSX):**
- Conflicts with: EHRPWM0B (if using PWM)
- **Solution:** Don't use PWM on this pin

**P9.31 (McASP ACLKX):**
- Conflicts with: EHRPWM0A (if using PWM)
- **Solution:** Don't use PWM on this pin

**P9.11, P9.13 (UART4):**
- Conflicts with: GPMC signals (if using external memory)
- **Solution:** BBGW doesn't use GPMC, no conflict

### Checking Pin Conflicts

**Method 1: Check loaded overlays:**
```bash
cat /sys/devices/platform/bone_capemgr/slots
```

**Method 2: Check pin mux configuration:**
```bash
# Requires debugfs
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A1 "pin 70\|pin 74"
```

**Method 3: Check Device Tree:**
```bash
dtc -I fs /sys/firmware/devicetree/base > current.dts
grep -A5 "pinctrl-0" current.dts
```

---

## Verification Procedures

### After Device Tree Loading

**Check McASP:**
```bash
# Check kernel messages
dmesg | grep -i mcasp

# Expected output:
# davinci-mcasp 48038000.mcasp: _new_: version 1.0
# davinci-mcasp 48038000.mcasp: I2S mode
```

**Check UART4:**
```bash
# Check device exists
ls -l /dev/ttyO4

# Expected: crw-rw---- 1 root dialout

# Check kernel messages
dmesg | grep -i uart4

# Expected: serial8250.2: ttyO4 at MMIO 0x481a8000 (irq = 45) is a 8250
```

**Check Pin Mux:**
```bash
# Check specific pins (requires debugfs)
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep "pin 110\|pin 111\|pin 113"

# Expected (example):
# pin 110 (44e10990.0): 44e10990.0 (GPIO UNCLAIMED) function pinmux_mcasp0_pins group pinmux_mcasp0_pins
```

**Check ALSA Device:**
```bash
# List playback devices
aplay -l

# Expected output:
# card 0: BBB [TI BeagleBone Black], device 0: ...
#   Subdevices: 1/1
```

---

## Troubleshooting

### Pin Not Configured Correctly

**Symptom:** Device not working, signals not toggling

**Check:**
1. Verify Device Tree overlay loaded: `cat /sys/devices/platform/bone_capemgr/slots`
2. Check pin mux mode: `cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins`
3. Verify no other overlay using same pins

**Fix:**
- Ensure overlay compiled correctly (no errors from `dtc`)
- Check `/boot/uEnv.txt` for correct overlay path
- Reboot after changing `/boot/uEnv.txt`

### Pin Conflict Error

**Symptom:** Kernel error "pin already claimed"

**Check:**
```bash
dmesg | grep -i conflict
dmesg | grep -i "already claimed"
```

**Fix:**
- Disable conflicting overlays in `/boot/uEnv.txt`
- Remove conflicting cape definitions
- Use different pins if possible

### Signal Not Toggling

**Symptom:** Pin voltage stuck at 0V or 3.3V

**Check:**
1. Multimeter: Measure voltage at pin (should be ~3.3V for idle UART, varying for I2S)
2. Oscilloscope: Check for toggling signals
3. Pin direction: Ensure configured as output (for BCLK, WS, Data, UART TX)

**Fix:**
- Verify Device Tree fragment loaded
- Check peripheral is enabled (`status = "okay"` in DT)
- Verify application is driving the peripheral

---

## References

### Pin Mux Spreadsheet
- [AM335x Pin Mux Utility](https://www.ti.com/tool/PROCESSOR-SDK-AM335X)
- Download: `AM335x_pinmux_v1.xlsx`

### Device Tree Documentation
- [Linux Device Tree Overlays](https://www.kernel.org/doc/Documentation/devicetree/overlay-notes.txt)
- [BeagleBone Overlay Guide](https://elinux.org/Beagleboard:BeagleBoneBlack_Debian#Loading_custom_capes)

### Datasheets
- [AM335x Datasheet (Pin Specs)](https://www.ti.com/lit/ds/symlink/am3358.pdf)
- [AM335x TRM (Pin Mux Tables)](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf) — Chapter 9

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**See Also:**
- [HARDWARE_REQUIREMENTS.md](HARDWARE_REQUIREMENTS.md) — Complete hardware specs and wiring
- [BBGW_DEVICE_TREE_GUIDE.md](BBGW_DEVICE_TREE_GUIDE.md) — Device Tree overlay creation guide (Phase 1)
- [RESEARCH_NOTES.md](RESEARCH_NOTES.md) — Technical research findings
