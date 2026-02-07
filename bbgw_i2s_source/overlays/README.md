# BeagleBone Green Wireless I2S Device Tree Overlays

This directory contains Device Tree overlay (.dts) source files for configuring the BeagleBone Green Wireless McASP0 peripheral for I2S audio transmission.

---

## Overview

The BeagleBone Green Wireless uses Device Tree overlays to configure peripherals at boot time. Unlike the Raspberry Pi (which can bit-bang I2S via GPIO), the BBGW has dedicated hardware for I2S audio via the **McASP (Multichannel Audio Serial Port)** module.

This directory provides two overlay options:

1. **BB-BBGW-I2S-00A0.dts** — Full configuration with ALSA sound card (recommended)
2. **BB-BBGW-I2S-SIMPLE-00A0.dts** — Minimal pin mux only (fallback)

---

## Files

### 1. BB-BBGW-I2S-00A0.dts (Recommended)

**Description:** Complete Device Tree overlay with:
- McASP0 pin muxing (P9.31, P9.29, P9.28)
- McASP0 I2S master mode configuration
- ALSA simple-audio-card integration
- Dummy codec for transmit-only operation

**Features:**
- ✅ Full ALSA integration (accessible via `aplay -l`)
- ✅ Configures McASP0 as I2S master (generates BCLK and WS)
- ✅ 48 kHz sample rate support
- ✅ 16-bit stereo PCM
- ✅ Transmit on AXR1 (P9.28)

**Use When:**
- You want full ALSA integration
- You need the BBGW to be the I2S master (clock generator)
- You want the standard `hw:0,0` ALSA device

**ALSA Device Name:** `hw:CARD=BBGW-I2S,DEV=0` or `hw:0,0`

---

### 2. BB-BBGW-I2S-SIMPLE-00A0.dts (Fallback)

**Description:** Minimal overlay that only configures pin muxing.

**Features:**
- ✅ Pin mux configuration only
- ✅ Enables McASP0 peripheral
- ⚠️ No ALSA sound card created
- ⚠️ Requires manual McASP configuration via `/dev/mem` or kernel driver

**Use When:**
- The full overlay fails to load
- You need to debug pin muxing issues
- You want to configure McASP manually in userspace

**Note:** This overlay does NOT create an ALSA device. You'll need to configure McASP via other means.

---

## Pin Configuration

Both overlays configure the same pins:

| Pin   | Function       | McASP Signal | Direction | Mode | ESP32 Pin |
|-------|----------------|--------------|-----------|------|-----------|
| P9.31 | BCLK           | ACLKX        | Output    | 0    | GPIO26    |
| P9.29 | WS/LRCLK       | FSX          | Output    | 0    | GPIO25    |
| P9.28 | DOUT (Data)    | AXR1         | Output    | 2    | GPIO22    |
| P9.1  | GND            | —            | —         | —    | GND       |

**Signal Specifications:**
- **BCLK (Bit Clock):** 1.536 MHz (48 kHz × 32 bits/frame)
- **WS (Word Select):** 48 kHz, 50% duty cycle
- **DOUT (Data Out):** I2S format, MSB-first, 16-bit samples

---

## Compilation

### Prerequisites

Install the device tree compiler:

```bash
sudo apt update
sudo apt install device-tree-compiler
```

### Compile Full Overlay

```bash
cd /home/phil/work/esp32/esp32_btaudio/bbgw_i2s_source/overlays

# Compile with error checking
dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts

# Check for warnings
echo $?  # Should be 0
```

### Compile Simple Overlay

```bash
dtc -O dtb -o BB-BBGW-I2S-SIMPLE-00A0.dtbo -b 0 -@ BB-BBGW-I2S-SIMPLE-00A0.dts
```

**Compiler Options:**
- `-O dtb` — Output device tree blob (binary)
- `-o <file>` — Output filename
- `-b 0` — Boot CPU (0 for ARM)
- `-@` — Generate symbols for overlays

**Common Warnings (Can Ignore):**
- `Warning (unit_address_vs_reg): Node X has a unit name, but no reg property`
- These are normal for overlays and can be ignored

**Errors to Fix:**
- `ERROR: Syntax error` — Fix syntax in .dts file
- `ERROR: undefined reference` — Missing symbol or include

---

## Installation on BeagleBone

### 1. Copy Compiled Overlay to BBGW

**Option A: USB/Ethernet copy (if BBGW accessible)**

```bash
# From development machine
scp BB-BBGW-I2S-00A0.dtbo debian@192.168.7.2:~
scp BB-BBGW-I2S-SIMPLE-00A0.dtbo debian@192.168.7.2:~
```

**Option B: USB drive**

```bash
# Copy to USB drive, then on BBGW:
sudo cp /media/usb0/BB-BBGW-I2S-00A0.dtbo /lib/firmware/
```

**Option C: Manual compilation on BBGW**

```bash
# Copy .dts source to BBGW
scp BB-BBGW-I2S-00A0.dts debian@192.168.7.2:~

# On BBGW:
dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
```

### 2. Install Overlay to Firmware Directory

```bash
# On BeagleBone Green Wireless
sudo cp BB-BBGW-I2S-00A0.dtbo /lib/firmware/
sudo chmod 644 /lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Verify installation
ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
```

### 3. Enable Overlay in /boot/uEnv.txt

**Method A: Using uboot_overlay_addr4 (Recommended)**

```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Find the line (around line 30-40):
# uboot_overlay_addr4=<file4>.dtbo

# Uncomment and change to:
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo

# Save and exit (Ctrl+O, Enter, Ctrl+X)
```

**Method B: Using enable_uboot_overlays and uboot_overlay_pru**

```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Ensure this line is uncommented:
enable_uboot_overlays=1

# Add overlay to one of the available slots:
uboot_overlay_addr4=/lib/firmware/BB-BBGW-I2S-00A0.dtbo
```

**Method C: Using cape_enable (Older method)**

```bash
# Edit /boot/uEnv.txt
sudo nano /boot/uEnv.txt

# Add this line:
cape_enable=bone_capemgr.enable_partno=BB-BBGW-I2S
```

### 4. Reboot BeagleBone

```bash
sudo reboot
```

---

## Verification

### 1. Check Kernel Messages

After reboot, verify the overlay loaded successfully:

```bash
# Check for McASP initialization
dmesg | grep -i mcasp

# Expected output (example):
# [    X.XXXXXX] omap-mcasp 48038000.mcasp: ASP revision 2.00
# [    X.XXXXXX] davinci-mcasp 48038000.mcasp: using I2S mode
```

### 2. Check Pin Muxing

Verify pins are configured correctly:

```bash
# Check pin mux configuration
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A2 990
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A2 994
sudo cat /sys/kernel/debug/pinctrl/44e10800.pinmux/pins | grep -A2 99c

# Expected output (example):
# pin 100 (PIN100) 44e10990 00000000 pinctrl-single  # P9.31 Mode 0
# pin 101 (PIN101) 44e10994 00000000 pinctrl-single  # P9.29 Mode 0
# pin 103 (PIN103) 44e1099c 00000002 pinctrl-single  # P9.28 Mode 2
```

### 3. Check ALSA Device (Full Overlay Only)

If using BB-BBGW-I2S-00A0.dtbo:

```bash
# List ALSA playback devices
aplay -l

# Expected output:
# **** List of PLAYBACK Hardware Devices ****
# card 0: BBGW-I2S [BBGW-I2S], device 0: davinci-mcasp.0-snd-soc-dummy-dai snd-soc-dummy-dai-0 []
#   Subdevices: 1/1
#   Subdevice #0: subdevice #0

# Check card information
aplay -L | grep BBGW
```

### 4. Test ALSA Playback

Generate a test tone:

```bash
# Generate 1 kHz test tone (16-bit stereo, 48 kHz)
speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 1000

# Stop with Ctrl+C

# Or use aplay with raw PCM file
aplay -D hw:0,0 -f S16_LE -c 2 -r 48000 test.wav
```

### 5. Logic Analyzer Verification

For hardware validation, connect a logic analyzer to P9.31, P9.29, P9.28:

- **BCLK (P9.31):** Should show 1.536 MHz clock (48 kHz × 32 bits)
- **WS (P9.29):** Should show 48 kHz square wave (50% duty cycle)
- **DOUT (P9.28):** Should show I2S data stream (valid during ALSA playback)

---

## Troubleshooting

### Overlay Not Loading

**Symptoms:**
- No kernel messages about McASP
- `dmesg | grep mcasp` shows nothing

**Fixes:**

1. **Check compilation errors:**
   ```bash
   dtc -O dtb -o BB-BBGW-I2S-00A0.dtbo -b 0 -@ BB-BBGW-I2S-00A0.dts
   ```
   Fix any ERROR messages (warnings OK).

2. **Verify file exists in /lib/firmware:**
   ```bash
   ls -l /lib/firmware/BB-BBGW-I2S-00A0.dtbo
   ```

3. **Check /boot/uEnv.txt syntax:**
   ```bash
   grep overlay /boot/uEnv.txt
   ```
   Ensure no typos in overlay path.

4. **Check boot messages:**
   ```bash
   dmesg | grep -i "device tree"
   dmesg | grep -i "overlay"
   ```

5. **Try simple overlay first:**
   Use `BB-BBGW-I2S-SIMPLE-00A0.dtbo` to isolate issues.

---

### Pin Mux Not Changing

**Symptoms:**
- Pins show Mode 7 (GPIO) instead of Mode 0/2
- Pin debugfs shows wrong mode

**Fixes:**

1. **Check for pin conflicts:**
   ```bash
   sudo cat /sys/devices/platform/bone_capemgr/slots
   ```
   Look for other capes that might use P9.28, P9.29, P9.31.

2. **Disable conflicting capes:**
   Edit `/boot/uEnv.txt` and comment out conflicting overlays.

3. **Use config-pin (Debian 10+):**
   ```bash
   # Manually set pin modes
   config-pin P9.31 mcasp
   config-pin P9.29 mcasp
   config-pin P9.28 mcasp
   ```

---

### ALSA Device Not Found

**Symptoms:**
- `aplay -l` doesn't show BBGW-I2S card
- `/dev/snd/` is empty

**Fixes:**

1. **Check sound card creation:**
   ```bash
   dmesg | grep -i "sound\|audio\|alsa"
   ```

2. **Verify McASP loaded:**
   ```bash
   lsmod | grep mcasp
   dmesg | grep davinci
   ```

3. **Load ALSA modules manually:**
   ```bash
   sudo modprobe snd-soc-davinci-mcasp
   sudo modprobe snd-soc-simple-card
   ```

4. **Check for errors:**
   ```bash
   dmesg | grep -i error
   ```

5. **Use simple overlay + manual ALSA config:**
   Create `/etc/asound.conf` with custom ALSA configuration (advanced).

---

### No Audio Output

**Symptoms:**
- ALSA device exists
- `aplay` runs without errors
- No I2S signals on logic analyzer

**Fixes:**

1. **Check McASP clock configuration:**
   ```bash
   dmesg | grep -i "clock\|mcasp"
   ```

2. **Verify system clocks:**
   McASP needs proper clock input. Check if system clocks are enabled.

3. **Test with speaker-test:**
   ```bash
   speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine -f 1000 -l 1
   ```

4. **Check for DMA errors:**
   ```bash
   dmesg | grep -i dma
   ```

5. **Try different sample rates:**
   ```bash
   # Try 44.1 kHz
   speaker-test -D hw:0,0 -c 2 -r 44100 -F S16_LE -t sine -f 1000
   ```

---

### Pin Conflicts with Other Capes

**Symptoms:**
- Overlay loads but pins don't work
- Other cape stops working after loading I2S overlay

**Common Conflicts:**
- **P9.28, P9.29, P9.31:** Used by some audio capes, display capes
- **HDMI:** Uses some McASP pins (disable if not needed)

**Fixes:**

1. **Disable HDMI (if not needed):**
   ```bash
   # Edit /boot/uEnv.txt
   sudo nano /boot/uEnv.txt
   
   # Uncomment:
   disable_uboot_overlay_video=1
   ```

2. **Check loaded capes:**
   ```bash
   sudo cat /sys/devices/platform/bone_capemgr/slots
   ```

3. **Unload conflicting cape:**
   ```bash
   # If using cape manager (older kernels)
   echo -4 > /sys/devices/platform/bone_capemgr/slots
   # (Replace -4 with slot number from slots file)
   ```

---

## Advanced Configuration

### Changing Sample Rate

To support different sample rates (e.g., 44.1 kHz), modify the overlay:

```dts
/* In fragment@2, sound section: */
sound_master: simple-audio-card,cpu {
    sound-dai = <&mcasp0>;
    system-clock-frequency = <22579200>;  /* 44.1 kHz * 512 */
};
```

Recompile and reinstall.

### Using Different Serializer (AXR0 instead of AXR1)

Modify fragment@1 in overlay:

```dts
/* Use AXR0 (P9.30) instead of AXR1 (P9.28) */
serial-dir = <
    1 /* AXR0: transmit (P9.30) */
    0 /* AXR1: unused */
    0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
>;

/* Also update pin mux in fragment@0: */
pinctrl-single,pins = <
    0x990 0x00  /* P9.31: ACLKX */
    0x994 0x00  /* P9.29: FSX */
    0x998 0x00  /* P9.30: AXR0 (Mode 0) */
>;
```

### I2S Slave Mode (BBGW receives clock from ESP32)

If ESP32 is the I2S master, modify fragment@2:

```dts
simple-audio-card,bitclock-master = <&sound_codec>;
simple-audio-card,frame-master = <&sound_codec>;
```

And add external clock configuration (advanced, requires ESP32 hardware changes).

---

## References

### Documentation
- [AM335x Technical Reference Manual (Chapter 22: McASP)](https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
- [BeagleBone Device Tree Overlays](https://elinux.org/Beagleboard:BeagleBoneBlack_Debian#Loading_custom_capes)
- [Linux ALSA simple-audio-card](https://www.kernel.org/doc/html/latest/sound/soc/index.html)

### Example Overlays
- [BB.org Overlays Repository](https://github.com/beagleboard/bb.org-overlays)
- [BeagleBone Audio Overlays](https://github.com/beagleboard/bb.org-overlays/tree/master/src/arm)

### Tools
- Device Tree Compiler: `sudo apt install device-tree-compiler`
- Logic Analyzer: Saleae Logic, DSLogic, PulseView/sigrok

---

## Next Steps

After successfully loading the overlay:

1. ✅ Verify ALSA device: `aplay -l`
2. ✅ Update `bbgw_i2s_source/config.yaml` with correct ALSA device name
3. ✅ Test with `speaker-test`
4. ✅ Connect ESP32 hardware
5. ✅ Run Milestone 1 test (1 kHz tone generation)
6. ✅ Verify with logic analyzer
7. ✅ Proceed to Phase 2: Code Adaptations

---

**Last Updated:** 2026-02-07  
**Status:** Phase 1.1 in progress  
**Next:** Compile overlays on BBGW and test
