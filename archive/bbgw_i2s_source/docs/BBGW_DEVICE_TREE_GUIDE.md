# BeagleBone Green Wireless Device Tree Guide

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Platform:** BeagleBone Green Wireless (AM335x, Debian Linux)  
**Purpose:** Comprehensive guide to Device Tree overlays for I2S and UART configuration  
**Date:** 2026-02-07

---

## Overview

This guide provides comprehensive information about Device Tree overlays for the BeagleBone Green Wireless I2S Audio Source project. Device Tree overlays are essential for enabling and configuring hardware peripherals like McASP (I2S) and UART on the AM335x processor.

**What Are Device Tree Overlays?**
- Configuration files that describe hardware to the Linux kernel
- Allow dynamic hardware configuration without kernel recompilation
- Enable/disable peripherals, configure pin muxing, set parameters
- Loaded at boot time via U-Boot bootloader

**Why Overlays Are Needed:**
- BeagleBone pins are multiplexed (one physical pin, multiple functions)
- McASP and UART peripherals disabled by default
- Pin muxing must be configured for I2S and UART operation
- Overlays provide clean, reproducible hardware configuration

**Related Documentation:**
- [HARDWARE_SETUP_BBGW.md](HARDWARE_SETUP_BBGW.md) - Hardware configuration overview
- [BBGW_PIN_REFERENCE.md](BBGW_PIN_REFERENCE.md) - Complete pin reference
- [MILESTONE1_HARDWARE_SETUP_BBGW.md](MILESTONE1_HARDWARE_SETUP_BBGW.md) - I2S overlay details

---

## Table of Contents

1. [Device Tree Basics](#device-tree-basics)
2. [Required Overlays](#required-overlays)
3. [Overlay Installation](#overlay-installation)
4. [U-Boot Configuration](#u-boot-configuration)
5. [Overlay Compilation](#overlay-compilation)
6. [Debugging Overlays](#debugging-overlays)
7. [Common Issues](#common-issues)
8. [References](#references)

---

## Device Tree Basics

### What is a Device Tree?

A **Device Tree** is a data structure that describes hardware components and their relationships to the operating system. It allows a single kernel binary to support multiple hardware platforms by providing hardware configuration at boot time.

**Key Concepts:**
- **Nodes:** Represent hardware devices (e.g., McASP, UART, GPIO)
- **Properties:** Describe device characteristics (e.g., compatible strings, register addresses)
- **Overlays:** Modify or extend the base Device Tree
- **Pin Muxing:** Configure physical pins for specific functions

### Device Tree on BeagleBone Green Wireless

**Base Device Tree:**
- Location: `/boot/dtbs/$(uname -r)/am335x-bonegreen-wireless.dtb`
- Describes core AM335x hardware (CPU, memory, buses)
- Compiled from `am335x-bonegreen-wireless.dts` in kernel source

**Overlays:**
- Location: `/lib/firmware/*.dtbo`
- Loaded by U-Boot at boot time
- Configure specific peripherals (McASP, UART, GPIO, etc.)
- Can be enabled/disabled without kernel recompilation

**File Extensions:**
- `.dts` — Device Tree Source (human-readable text)
- `.dtsi` — Device Tree Source Include (reusable components)
- `.dtb` — Device Tree Blob (compiled binary for base tree)
- `.dtbo` — Device Tree Blob Overlay (compiled binary for overlays)

### Pin Multiplexing on AM335x

The AM335x processor uses pin multiplexing to support multiple functions per physical pin:

**Example: P9.31 (McASP BCLK)**
- Mode 0: SPI1_SCLK (SPI clock)
- Mode 1: ECAP0_IN_PWM0_OUT (PWM/capture)
- Mode 2: UART3_TXD (UART transmit)
- Mode 3: UART2_RXD (UART receive)
- **Mode 4: McASP0_ACLKX (I2S bit clock)** ← Used for this project
- Mode 5: MMC0_SDCD (SD card detect)
- Mode 6: UART1_TXD (UART transmit)
- Mode 7: GPIO3_14 (general purpose I/O)

Device Tree overlays configure the pin mux to select the desired mode.

---

## Required Overlays

This project requires two Device Tree overlays:

### 1. BB-BBGW-I2S-00A0.dtbo (McASP I2S)

**Purpose:** Enable McASP0 peripheral for I2S audio output

**Configured Pins:**
| Pin | Function | Mode | Signal |
|-----|----------|------|--------|
| P9.31 | McASP0_ACLKX | Mode 4 | BCLK (Bit Clock) |
| P9.29 | McASP0_FSX | Mode 4 | WS (Word Select / LRCLK) |
| P9.28 | McASP0_AXR1 | Mode 4 | DOUT (Data Out) |

**Peripheral Configuration:**
- McASP0 in master mode (generates BCLK and WS)
- I2S format (standard stereo audio)
- Sample rate: 48 kHz (configurable)
- Sample format: 16-bit signed (S16_LE) in 32-bit slots

**ALSA Device Created:**
- Card name: `BBGW-I2S`
- Device: `hw:0,0` (typically card 0, device 0)

### 2. BB-BBGW-UART4-00A0.dtbo (UART4)

**Purpose:** Enable UART4 for communication with ESP32

**Configured Pins:**
| Pin | Function | Mode | Signal |
|-----|----------|------|--------|
| P9.13 | UART4_TXD | Mode 6 | TX (Transmit) |
| P9.11 | UART4_RXD | Mode 6 | RX (Receive) |

**Peripheral Configuration:**
- UART4 enabled
- Baud rate: 115200 bps (configured by software)
- 8 data bits, no parity, 1 stop bit (8N1)

**Device Created:**
- Device node: `/dev/ttyO4`
- Permissions: `crw-rw---- root:dialout`

---

## Overlay Installation

### Step 1: Check for Pre-installed Overlays

```bash
# List all Device Tree overlays
ls /lib/firmware/BB-*.dtbo

# Check if required overlays exist
ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
ls -l /lib/firmware/BB-BBGW-UART4-00A0.dtbo
```

**If overlays exist:** Proceed to [U-Boot Configuration](#u-boot-configuration)

**If overlays missing:** Continue to overlay compilation or installation.

### Step 2: Install Pre-compiled Overlays (Recommended)

If overlays are provided in this repository:

```bash
# Copy overlays to firmware directory
sudo cp overlays/BB-BBGW-I2S-00A0.dtbo /lib/firmware/
sudo cp overlays/BB-BBGW-UART4-00A0.dtbo /lib/firmware/

# Set correct permissions
sudo chmod 644 /lib/firmware/BB-BBGW-I2S-00A0.dtbo
sudo chmod 644 /lib/firmware/BB-BBGW-UART4-00A0.dtbo

# Verify installation
ls -l /lib/firmware/BB-BBGW-*.dtbo
```

### Step 3: Alternative - Use BeagleBone Overlays Repository

```bash
# Clone BeagleBone overlay repository
cd ~
git clone https://github.com/beagleboard/bb.org-overlays.git
cd bb.org-overlays

# Build overlays (requires device-tree-compiler)
sudo apt-get install device-tree-compiler
./dtc-overlay.sh

# Install overlays
sudo make install

# Verify installation
ls /lib/firmware/BB-*.dtbo
```

---

## U-Boot Configuration

Device Tree overlays are loaded by the U-Boot bootloader using configuration in `/boot/uEnv.txt`.

### Step 1: Edit /boot/uEnv.txt

```bash
# Backup original file
sudo cp /boot/uEnv.txt /boot/uEnv.txt.backup

# Edit configuration
sudo nano /boot/uEnv.txt
```

### Step 2: Add Overlay Configuration

**Locate the overlay section** (around line 20-40):

```bash
###Custom Cape
#dtb_overlay=/lib/firmware/<file8>.dtbo
#dtb_overlay=/lib/firmware/<file9>.dtbo
###
###Additional custom capes
#uboot_overlay_addr0=/lib/firmware/<file0>.dtbo
#uboot_overlay_addr1=/lib/firmware/<file1>.dtbo
#uboot_overlay_addr2=/lib/firmware/<file2>.dtbo
#uboot_overlay_addr3=/lib/firmware/<file3>.dtbo
```

**Add these lines** (use next available addresses):

```bash
# Enable McASP I2S overlay
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Enable UART4 overlay
uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
```

**Important Notes:**
- Use sequential addresses (`addr4`, `addr5`, etc.)
- Do not skip addresses (e.g., don't use `addr4` and `addr7`)
- Check for existing overlays to avoid conflicts
- Remove leading `#` to uncomment

### Step 3: Save and Reboot

```bash
# Save file
# Ctrl+X, Y, Enter in nano

# Reboot to apply overlays
sudo reboot
```

### Step 4: Verify Overlays Loaded

After reboot:

```bash
# Check kernel messages for overlay loading
dmesg | grep -i overlay
# Should show "OF: overlay: ..." messages

# Check McASP (I2S) loaded
dmesg | grep -i mcasp
# Should show McASP initialization

# Check UART4 loaded
dmesg | grep ttyO4
# Should show UART4 registration

# Verify ALSA device
aplay -l
# Should show "BBGW-I2S" card

# Verify UART device
ls -l /dev/ttyO4
# Should show device node
```

---

## Overlay Compilation

If you need to modify overlays or compile from source:

### Prerequisites

```bash
# Install Device Tree compiler
sudo apt-get update
sudo apt-get install -y device-tree-compiler
```

### McASP I2S Overlay Source (BB-BBGW-I2S-00A0.dts)

```c
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-green", "ti,beaglebone-green-wireless", "ti,beaglebone-black";

    /* identification */
    part-number = "BB-BBGW-I2S";
    version = "00A0";

    /* state the resources this cape uses */
    exclusive-use =
        /* mcasp0 pins */
        "P9.31",    /* mcasp0: ACLKX (bit clock) */
        "P9.29",    /* mcasp0: FSX (word select) */
        "P9.28",    /* mcasp0: AXR2 (data out) */
        /* mcasp0 hardware */
        "mcasp0";

    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            mcasp0_pins: pinmux_mcasp0_pins {
                pinctrl-single,pins = <
                    0x190 0x20  /* P9.31: mcasp0_aclkx.mcasp0_aclkx, MODE0 | OUTPUT | PULLDOWN */
                    0x194 0x20  /* P9.29: mcasp0_fsx.mcasp0_fsx, MODE0 | OUTPUT | PULLDOWN */
                    0x19c 0x22  /* P9.28: mcasp0_axr2.mcasp0_axr2, MODE2 | OUTPUT | PULLDOWN */
                >;
            };
        };
    };

    fragment@1 {
        target = <&mcasp0>;
        __overlay__ {
            #sound-dai-cells = <0>;
            pinctrl-names = "default";
            pinctrl-0 = <&mcasp0_pins>;
            status = "okay";
            op-mode = <0>;  /* MCASP_IIS_MODE */
            tdm-slots = <2>;
            serial-dir = <  /* 0: INACTIVE, 1: TX, 2: RX */
                0 0 1 0
            >;
            tx-num-evt = <1>;
            rx-num-evt = <1>;
        };
    };

    fragment@2 {
        target = <&ocp>;
        __overlay__ {
            sound {
                compatible = "simple-audio-card";
                simple-audio-card,name = "BBGW-I2S";
                simple-audio-card,format = "i2s";
                simple-audio-card,bitclock-master = <&sound_master>;
                simple-audio-card,frame-master = <&sound_master>;
                sound_master: simple-audio-card,cpu {
                    sound-dai = <&mcasp0>;
                    system-clock-frequency = <24576000>;
                };
                simple-audio-card,codec {
                    sound-dai = <&codec>;
                };
            };

            codec: codec {
                #sound-dai-cells = <0>;
                compatible = "linux,spdif-dit";
            };
        };
    };
};
```

### UART4 Overlay Source (BB-BBGW-UART4-00A0.dts)

```c
/dts-v1/;
/plugin/;

/ {
    compatible = "ti,beaglebone", "ti,beaglebone-green", "ti,beaglebone-green-wireless", "ti,beaglebone-black";

    /* identification */
    part-number = "BB-BBGW-UART4";
    version = "00A0";

    /* state the resources this cape uses */
    exclusive-use =
        /* uart4 pins */
        "P9.13",    /* uart4_txd */
        "P9.11",    /* uart4_rxd */
        /* uart4 hardware */
        "uart4";

    fragment@0 {
        target = <&am33xx_pinmux>;
        __overlay__ {
            uart4_pins: pinmux_uart4_pins {
                pinctrl-single,pins = <
                    0x070 0x26  /* P9.11: uart4_rxd_mux2, MODE6 | INPUT_PULLUP */
                    0x074 0x06  /* P9.13: uart4_txd_mux2, MODE6 | OUTPUT_PULLDOWN */
                >;
            };
        };
    };

    fragment@1 {
        target = <&uart4>;
        __overlay__ {
            status = "okay";
            pinctrl-names = "default";
            pinctrl-0 = <&uart4_pins>;
        };
    };
};
```

### Compilation Commands

```bash
# Compile I2S overlay
sudo dtc -O dtb -o /lib/firmware/BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts

# Compile UART4 overlay
sudo dtc -O dtb -o /lib/firmware/BB-BBGW-UART4-00A0.dtbo -b 0 -@ BB-BBGW-UART4-00A0.dts

# Verify compilation
ls -l /lib/firmware/BB-BBGW-*.dtbo
```

**Compiler Options:**
- `-O dtb`: Output format (Device Tree Blob)
- `-o <file>`: Output file path
- `-b 0`: Set boot CPU to 0
- `-@`: Enable symbols (required for overlays)

---

## Debugging Overlays

### Check if Overlay Loaded

```bash
# View loaded Device Tree overlays
cat /proc/device-tree/chosen/overlays/name

# Check for specific overlay
ls /proc/device-tree/chosen/overlays/
```

### View Kernel Messages

```bash
# All overlay-related messages
dmesg | grep -i overlay

# McASP-specific messages
dmesg | grep -i mcasp

# UART-specific messages
dmesg | grep -i uart
dmesg | grep ttyO
```

### Inspect Pin Muxing

```bash
# View current pin configuration
cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins

# View pin mux for specific pins
cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins | grep -E "pin 103|pin 104|pin 105"
# P9.31 = pin 104 (BCLK)
# P9.29 = pin 105 (WS)
# P9.28 = pin 103 (DOUT)
```

### Test ALSA Device

```bash
# List ALSA devices
aplay -l

# Test I2S output
speaker-test -D hw:0,0 -r 48000 -c 2 -t sine -f 1000
```

### Test UART Device

```bash
# Check device exists
ls -l /dev/ttyO4

# Test with minicom
minicom -D /dev/ttyO4 -b 115200

# Test with Python
python3 -c "import serial; s = serial.Serial('/dev/ttyO4', 115200); print('OK')"
```

---

## Common Issues

### Issue 1: Overlay not loading

**Symptoms:**
```bash
$ dmesg | grep "BB-BBGW-I2S"
# No output
```

**Solutions:**

1. **Check overlay file exists:**
   ```bash
   ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

2. **Check /boot/uEnv.txt syntax:**
   ```bash
   grep "BB-BBGW-I2S" /boot/uEnv.txt
   # Should show: uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
   # No leading # (comment)
   ```

3. **Check for typos:**
   - Overlay filename must match exactly
   - Path must be `/lib/firmware/`
   - Extension must be `.dtbo`

4. **Reboot:**
   ```bash
   sudo reboot
   ```

### Issue 2: Pin conflict

**Symptoms:**
```bash
$ dmesg | grep overlay
OF: overlay: Failed to apply overlay: -16
```

**Solutions:**

1. **Identify conflicting overlays:**
   ```bash
   cat /proc/device-tree/chosen/overlays/name
   ```

2. **Disable conflicting overlays in /boot/uEnv.txt:**
   - Comment out HDMI overlay if using McASP pins
   - Comment out cape overlays that use same pins

3. **Check pin conflicts:**
   ```bash
   cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pinmux-pins
   ```

### Issue 3: Wrong pin mode after overlay loads

**Symptoms:**
- Overlay loads (dmesg shows loading)
- Pin not in correct mode (not McASP/UART)

**Solutions:**

1. **Verify overlay source:**
   - Check pin mux settings in .dts file
   - Ensure correct mode numbers

2. **Recompile overlay:**
   ```bash
   sudo dtc -O dtb -o /lib/firmware/BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
   sudo reboot
   ```

### Issue 4: ALSA device not created

**Symptoms:**
```bash
$ aplay -l
aplay: device_list:274: no soundcards found...
```

**Solutions:**

1. **Check McASP overlay loaded:**
   ```bash
   dmesg | grep -i mcasp
   ```

2. **Check ALSA module loaded:**
   ```bash
   lsmod | grep snd
   ```

3. **Manually load ALSA modules:**
   ```bash
   sudo modprobe snd-soc-davinci-mcasp
   sudo modprobe snd-soc-simple-card
   ```

### Issue 5: UART device not created

**Symptoms:**
```bash
$ ls /dev/ttyO4
ls: cannot access '/dev/ttyO4': No such file or directory
```

**Solutions:**

1. **Check UART overlay loaded:**
   ```bash
   dmesg | grep ttyO4
   ```

2. **Check UART enabled:**
   ```bash
   cat /proc/tty/drivers | grep omap
   ```

3. **Verify UART4 in device tree:**
   ```bash
   ls /proc/device-tree/ocp/serial@481a8000/
   ```

---

## References

### BeagleBone Documentation
- [BeagleBone Green Wireless Wiki](https://wiki.seeedstudio.com/BeagleBone_Green_Wireless/)
- [BeagleBoard Device Tree Reference](https://github.com/beagleboard/linux/wiki/Device-Tree)
- [BeagleBone Overlays Repository](https://github.com/beagleboard/bb.org-overlays)

### Device Tree Documentation
- [Linux Kernel Device Tree Documentation](https://www.kernel.org/doc/Documentation/devicetree/)
- [Device Tree Specification](https://www.devicetree.org/specifications/)
- [Device Tree Compiler Manual](https://manpages.debian.org/testing/device-tree-compiler/dtc.1.en.html)

### AM335x Documentation
- [AM335x Technical Reference Manual](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
- [AM335x Pin Mux Utility](https://dev.ti.com/pinmux/)
- [McASP User Guide](https://www.ti.com/lit/ug/sprugm2a/sprugm2a.pdf)

### Project Documentation
- [Hardware Setup Guide](HARDWARE_SETUP_BBGW.md)
- [Pin Reference](BBGW_PIN_REFERENCE.md)
- [Milestone 1 I2S Setup](MILESTONE1_HARDWARE_SETUP_BBGW.md)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT
