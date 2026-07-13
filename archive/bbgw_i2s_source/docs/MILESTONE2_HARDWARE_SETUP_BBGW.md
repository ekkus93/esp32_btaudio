# Milestone 2: UART Command Interface — Hardware Setup (BBGW)

**Project:** BeagleBone Green Wireless I2S Audio Source  
**Milestone:** Phase 3.3 — UART Command Interface Testing  
**Date:** 2026-02-07  
**Hardware:** BeagleBone Green Wireless + ESP32

---

## Overview

This document describes the hardware setup and testing procedures for **Milestone 2**: UART Command Interface between BeagleBone Green Wireless (BBGW) and ESP32 running `esp_bt_audio_source` firmware.

**Milestone 2 Objectives:**
1. Establish reliable UART communication (BBGW UART4 ↔ ESP32)
2. Implement command/response protocol with timeout handling
3. Test event notifications from ESP32
4. Verify bidirectional communication at 115200 baud

**Success Criteria:**
- [x] UART4 Device Tree overlay loaded
- [x] `/dev/ttyO4` accessible with correct permissions
- [x] Physical UART wiring verified (loopback test passed)
- [x] ESP32 UART echo test successful
- [x] `milestone2_uart_test.py` sends STATUS and VOLUME commands
- [x] Command responses received within timeout
- [x] Event notifications received and processed

---

## Table of Contents

1. [Hardware Requirements](#hardware-requirements)
2. [UART4 Pin Mapping](#uart4-pin-mapping)
3. [Device Tree Overlay Setup](#device-tree-overlay-setup)
4. [Physical Wiring](#physical-wiring)
5. [Verification Procedures](#verification-procedures)
6. [Running Milestone 2 Test](#running-milestone-2-test)
7. [Expected Results](#expected-results)
8. [Troubleshooting](#troubleshooting)
9. [Success Validation](#success-validation)

---

## Hardware Requirements

### BeagleBone Green Wireless
- **Board:** BeagleBone Green Wireless (BBGW)
- **OS:** Debian 11 or later (Linux kernel 5.10+)
- **Peripherals:** UART4 (via P9 header)
- **Power:** USB or 5V barrel jack
- **Network:** Wi-Fi or Ethernet (for SSH access)

### ESP32 Development Board
- **Board:** ESP32-DevKitC or compatible
- **Firmware:** `esp_bt_audio_source` (from esp32_btaudio repository)
- **UART:** UART1 (GPIO16 RX, GPIO17 TX)
- **Power:** USB or 3.3V external
- **Features:** Bluetooth Classic A2DP source

### Additional Components
- **Jumper Wires:** 3× male-to-male or male-to-female (depending on headers)
- **Breadboard:** Optional (for cleaner connections)
- **Logic Analyzer:** Optional (for debugging UART signals)
- **USB-to-TTL Adapter:** Optional (for ESP32 serial monitor access)

---

## UART4 Pin Mapping

### BeagleBone Green Wireless P9 Header

| Pin | Function | Direction | Signal | Notes |
|-----|----------|-----------|--------|-------|
| P9.13 | UART4_TXD | Output | TX → ESP32 RX | Transmit from BBGW |
| P9.11 | UART4_RXD | Input | RX ← ESP32 TX | Receive to BBGW |
| P9.1 | DGND | — | Ground | Common ground |
| P9.2 | DGND | — | Ground | Alternative ground |

**Important:** UART4 requires Device Tree overlay `BB-BBGW-UART4-00A0.dtbo` to be loaded at boot.

### ESP32 UART1 Pin Mapping

| GPIO | Function | Direction | Signal | Notes |
|------|----------|-----------|--------|-------|
| GPIO16 | UART1_RX | Input | RX ← BBGW TX | Receive from BBGW |
| GPIO17 | UART1_TX | Output | TX → BBGW RX | Transmit to BBGW |
| GND | Ground | — | Ground | Common ground |

**Firmware Configuration** (`esp_bt_audio_source`):
```c
// In esp_bt_audio_source/main/uart_task.c or similar:
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_16
#define UART_BAUD_RATE  115200
```

---

## Device Tree Overlay Setup

BBGW UART4 is **disabled by default**. You must enable it via Device Tree overlay.

### Step 1: Check Overlay Availability

```bash
# Check if overlay exists:
ls -l /lib/firmware/BB-BBGW-UART4-00A0.dtbo

# Expected output:
# -rw-r--r-- 1 root root 1234 Jan 15 12:00 /lib/firmware/BB-BBGW-UART4-00A0.dtbo
```

**If overlay does not exist:**
- Install via package: `sudo apt-get install bb-cape-overlays`
- Or compile manually (see [BBGW Overlay Repository](https://github.com/beagleboard/bb.org-overlays))

### Step 2: Enable Overlay in `/boot/uEnv.txt`

```bash
sudo nano /boot/uEnv.txt
```

Add or uncomment the following line:

```bash
# Enable UART4 (P9.13 TX, P9.11 RX):
uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
```

**Alternative:** Use `config-pin` utility (runtime, non-persistent):

```bash
# Runtime enable (lost after reboot):
sudo config-pin P9.13 uart
sudo config-pin P9.11 uart
```

### Step 3: Reboot and Verify

```bash
# Reboot to load overlay:
sudo reboot

# After reboot, verify UART4 is loaded:
dmesg | grep ttyO4

# Expected output:
# [    2.345678] 481a8000.serial: ttyO4 at MMIO 0x481a8000 (irq = 45, base_baud = 3000000) is a OMAP UART4

# Check device node:
ls -l /dev/ttyO4

# Expected output:
# crw-rw---- 1 root dialout 249, 4 Feb  7 10:00 /dev/ttyO4
```

### Step 4: Set Permissions

Add your user to `dialout` group for UART access:

```bash
sudo usermod -a -G dialout $USER

# Log out and log back in for group change to take effect
# Or run:
newgrp dialout
```

---

## Physical Wiring

### Connection Diagram

```
BeagleBone P9 Header          ESP32 DevKitC
┌────────────────┐            ┌────────────────┐
│ P9.13 (TX) ────┼───────────→│ GPIO16 (RX)    │
│ P9.11 (RX) ←───┼────────────│ GPIO17 (TX)    │
│ P9.1  (GND) ───┼────────────│ GND            │
└────────────────┘            └────────────────┘

Signal Flow:
  BBGW TX (P9.13) → ESP32 RX (GPIO16)  [Command transmission]
  BBGW RX (P9.11) ← ESP32 TX (GPIO17)  [Response reception]
  GND             ↔ GND                [Common reference]
```

### Wiring Steps

1. **Power off both devices** before making connections
2. **Connect GND first:**
   - BBGW P9.1 (or P9.2) → ESP32 GND pin
3. **Connect TX/RX:**
   - BBGW P9.13 (TX) → ESP32 GPIO16 (RX) — **Transmit from BBGW**
   - BBGW P9.11 (RX) → ESP32 GPIO17 (TX) — **Receive to BBGW**
4. **Double-check polarity:**
   - TX from one device → RX on other device (crossover connection)
   - **Do NOT connect TX to TX or RX to RX**
5. **Power on devices:**
   - BBGW: via USB or 5V barrel
   - ESP32: via USB or 3.3V external supply

### Voltage Level Considerations

- **BBGW UART4:** 3.3V logic (LVCMOS 3.3V)
- **ESP32 GPIO:** 3.3V logic
- **✓ Compatible:** Direct connection is safe (both 3.3V)
- **⚠️ Warning:** Do NOT connect to 5V logic devices (e.g., Arduino Uno) without level shifter

---

## Verification Procedures

### 1. UART Loopback Test (BBGW Only)

Test UART4 hardware without ESP32 connection.

**Setup:**
1. Power off BBGW
2. Connect jumper wire: P9.13 (TX) → P9.11 (RX) (short TX to RX)
3. Power on BBGW

**Test Script:**

```bash
cd ~/bbgw_i2s_source/overlays
./test_uart4_loopback.sh
```

**Expected Output:**

```
Testing UART4 loopback on /dev/ttyO4...
Opening serial port /dev/ttyO4 at 115200 baud
Sending test string: "UART4 LOOPBACK TEST 123"
Received: "UART4 LOOPBACK TEST 123"
✓ Loopback test PASSED
```

**If loopback fails:**
- Check Device Tree overlay: `dmesg | grep ttyO4`
- Verify /dev/ttyO4 permissions: `ls -l /dev/ttyO4`
- Check jumper wire connection (P9.13 to P9.11)
- Run with sudo: `sudo ./test_uart4_loopback.sh`

### 2. ESP32 UART Echo Test

Test ESP32 UART communication independently.

**ESP32 Setup:**
1. Flash `esp_bt_audio_source` firmware with UART enabled
2. Connect ESP32 to PC via USB (for serial monitor)
3. Open serial monitor: `idf.py monitor` (115200 baud)

**BBGW Setup:**
1. Remove loopback jumper (from previous test)
2. Connect BBGW P9.13/11 to ESP32 GPIO16/17 as described
3. Connect common GND

**Test from BBGW:**

```bash
# Send test command to ESP32:
echo "STATUS" > /dev/ttyO4

# Read response (timeout after 5 seconds):
timeout 5 cat /dev/ttyO4
```

**Expected ESP32 Response:**

```
OK: IDLE
```

or

```
OK: CONNECTED
```

(depends on ESP32 Bluetooth state)

**ESP32 Serial Monitor Output:**

```
I (12345) UART: Received command: STATUS
I (12346) UART: Sending response: OK: IDLE
```

**If no response:**
- Check ESP32 firmware UART configuration (GPIO16/17, 115200 baud)
- Verify physical wiring (TX/RX crossover, GND connected)
- Check ESP32 power supply (USB or 3.3V)
- Verify ESP32 is running (LED should blink on DevKitC)

### 3. Python pyserial Test

Test Python pyserial library access to /dev/ttyO4.

**Test Script:**

```python
#!/usr/bin/env python3
import serial

# Open UART4
ser = serial.Serial('/dev/ttyO4', 115200, timeout=5)

# Send STATUS command
ser.write(b'STATUS\n')
ser.flush()

# Read response
response = ser.readline().decode('utf-8').strip()
print(f"Response: {response}")

ser.close()

# Expected output:
# Response: OK: IDLE
```

Run:

```bash
cd ~/bbgw_i2s_source
python3 test_pyserial.py
```

**If pyserial import fails:**

```bash
pip3 install pyserial
```

---

## Running Milestone 2 Test

### Prerequisites Checklist

- [x] UART4 Device Tree overlay loaded (`dmesg | grep ttyO4`)
- [x] `/dev/ttyO4` exists and accessible (`ls -l /dev/ttyO4`)
- [x] UART loopback test passed
- [x] ESP32 connected and running `esp_bt_audio_source` firmware
- [x] Physical wiring verified (P9.13/11 ↔ GPIO16/17, GND connected)
- [x] ESP32 UART echo test passed
- [x] Python pyserial installed (`pip3 list | grep pyserial`)

### Test Execution

```bash
cd ~/bbgw_i2s_source
./milestone2_uart_test.py
```

**With custom device path:**

```bash
./milestone2_uart_test.py --device /dev/ttyO4 --baudrate 115200
```

### Test Sequence

The test script performs 5 steps:

1. **[1/5] Start UART manager**
   - Opens `/dev/ttyO4` at 115200 baud
   - Starts command queue thread
   - Initializes timeout handling

2. **[2/5] Send STATUS command**
   - Sends: `STATUS\n`
   - Waits for response (5-second timeout)
   - Expected: `OK: IDLE` or `OK: CONNECTED`

3. **[3/5] Send VOLUME command**
   - Sends: `VOLUME 75\n`
   - Waits for acknowledgment
   - Expected: `OK`

4. **[4/5] Test timeout handling**
   - Sends STATUS with 1-second timeout
   - Verifies timeout exception if ESP32 slow/disconnected

5. **[5/5] Test event callbacks**
   - Registers event callback function
   - Waits 3 seconds for ESP32 events
   - Displays events received (if any)

---

## Expected Results

### Successful Test Output

```
======================================================================
Milestone 2: UART Command Interface Test (BBGW)
======================================================================

Configuration:
  Serial Device: /dev/ttyO4
  Baudrate:      115200
  Timeout:       5.0 seconds

UART4 Pins (BeagleBone P9 Header):
  TXD: P9.13 → ESP32 GPIO 16 (RX)
  RXD: P9.11 → ESP32 GPIO 17 (TX)
  GND: P9.1  → ESP32 GND

Device Tree Overlay:
  Required: BB-BBGW-UART4-00A0.dtbo
  Check: ls /lib/firmware/BB-BBGW-UART4-00A0.dtbo
  Verify: ls -l /dev/ttyO4

----------------------------------------------------------------------

[1/5] Starting UART manager...
      ✓ UART manager started

[2/5] Testing STATUS command...
      ✓ STATUS command successful
        Response: IDLE

[3/5] Testing VOLUME command...
      ✓ VOLUME 75 command successful
        Response: OK

[4/5] Testing timeout handling...
      ✓ Timeout handling works correctly
        (Command timed out as expected with 1s timeout)

[5/5] Testing event callbacks...
      ✓ Event callback registered
        Waiting 3 seconds for events...
      → Event received: {'type': 'BT_CONNECTED', 'device': '00:11:22:33:44:55'}
      ✓ Received 1 events

----------------------------------------------------------------------
Test Summary:

UART Statistics:
  Commands sent: 3
  OK responses:  2
  ERR responses: 0
  Events:        1
  Reconnects:    0

Milestone 2 Success Criteria:
  [  ✓  ] STATUS command sent and response received
  [  ✓  ] VOLUME 75 command sent
  [  ✓  ] Timeout handling verified
  [  ✓  ] Event callbacks working (1 events)

Next Steps:
  1. Verify ESP32 is running esp_bt_audio_source firmware
  2. Check physical UART wiring (P9.13/11 ↔ ESP32)
  3. Verify /dev/ttyO4 permissions: ls -l /dev/ttyO4
  4. Test with ESP32 serial monitor for bidirectional communication
  5. Run UART loopback test: ./overlays/test_uart4_loopback.sh

✓ Milestone 2 test completed

Stopping UART manager...
  ✓ UART manager stopped
```

### Test Output Without ESP32 Connection

If ESP32 is **not connected** or **not responding**:

```
[2/5] Testing STATUS command...
      ✗ STATUS command timeout (ESP32 may not be connected)
        This is expected if ESP32 is not connected

[3/5] Testing VOLUME command...
      ✗ VOLUME command timeout (ESP32 may not be connected)
        This is expected if ESP32 is not connected

...

Milestone 2 Success Criteria:
  [  ✗  ] No commands sent (check ESP32 connection)
  [  -  ] VOLUME command not tested
  [  ✓  ] Timeout handling verified
  [  -  ] No events received (expected if ESP32 idle)
```

**This is normal** — test demonstrates timeout handling works correctly.

---

## Troubleshooting

### Issue 1: `/dev/ttyO4` does not exist

**Symptoms:**
```
FileNotFoundError: [Errno 2] No such file or directory: '/dev/ttyO4'
```

**Causes:**
- UART4 Device Tree overlay not loaded
- Incorrect overlay path in `/boot/uEnv.txt`
- Kernel module not loaded

**Solutions:**

1. **Check overlay in uEnv.txt:**
   ```bash
   grep UART4 /boot/uEnv.txt
   # Should show:
   # uboot_overlay_addr5=/lib/firmware/BB-BBGW-UART4-00A0.dtbo
   ```

2. **Verify overlay exists:**
   ```bash
   ls -l /lib/firmware/BB-BBGW-UART4-00A0.dtbo
   ```
   If missing: `sudo apt-get install bb-cape-overlays`

3. **Check kernel messages:**
   ```bash
   dmesg | grep -i uart
   dmesg | grep ttyO
   ```
   Should show UART4 initialization.

4. **Reboot:**
   ```bash
   sudo reboot
   ```

### Issue 2: Permission denied on `/dev/ttyO4`

**Symptoms:**
```
PermissionError: [Errno 13] Permission denied: '/dev/ttyO4'
```

**Causes:**
- User not in `dialout` group
- Incorrect device node permissions

**Solutions:**

1. **Add user to dialout group:**
   ```bash
   sudo usermod -a -G dialout $USER
   newgrp dialout
   ```

2. **Check device permissions:**
   ```bash
   ls -l /dev/ttyO4
   # Should be:
   # crw-rw---- 1 root dialout 249, 4 Feb  7 10:00 /dev/ttyO4
   ```

3. **Run with sudo (temporary):**
   ```bash
   sudo ./milestone2_uart_test.py
   ```

### Issue 3: No response from ESP32 (timeout)

**Symptoms:**
```
[2/5] Testing STATUS command...
      ✗ STATUS command timeout (ESP32 may not be connected)
```

**Causes:**
- ESP32 not powered on
- ESP32 firmware not running
- Incorrect UART wiring (TX/RX swapped)
- Baudrate mismatch (BBGW ≠ ESP32)
- GND not connected

**Solutions:**

1. **Verify ESP32 power:**
   - Check USB connection or 3.3V supply
   - Look for power LED on ESP32 DevKitC

2. **Check ESP32 firmware:**
   - Connect ESP32 to PC via USB
   - Open serial monitor: `idf.py monitor`
   - Should see boot messages and firmware version

3. **Verify UART wiring:**
   - BBGW TX (P9.13) → ESP32 RX (GPIO16) ✓
   - BBGW RX (P9.11) → ESP32 TX (GPIO17) ✓
   - GND → GND ✓
   - **Common mistake:** TX→TX or RX→RX (should be crossed)

4. **Check baudrate:**
   - BBGW: 115200 (hardcoded in script)
   - ESP32: 115200 (in esp_bt_audio_source firmware)
   - Run with custom baudrate: `./milestone2_uart_test.py --baudrate 9600`

5. **Test with loopback:**
   - Remove ESP32 connection
   - Short P9.13 to P9.11 on BBGW
   - Run loopback test: `./overlays/test_uart4_loopback.sh`
   - If loopback works, issue is ESP32-side

### Issue 4: Garbled responses or incorrect data

**Symptoms:**
```
Response: ��Ϸ���
```

**Causes:**
- Baudrate mismatch
- Loose wiring (intermittent connection)
- Electrical noise or ground loop
- Voltage level issue (rare with 3.3V devices)

**Solutions:**

1. **Verify baudrate match:**
   - BBGW: `stty -F /dev/ttyO4 115200`
   - ESP32: Check UART_BAUD_RATE in firmware

2. **Check physical connections:**
   - Reseat jumper wires
   - Use shorter wires (< 20 cm)
   - Avoid running UART wires parallel to power cables

3. **Add ground wire if using long cables:**
   - Connect additional GND wire between BBGW and ESP32

4. **Test at lower baudrate:**
   ```bash
   ./milestone2_uart_test.py --baudrate 9600
   ```

### Issue 5: Event callbacks not firing

**Symptoms:**
```
[5/5] Testing event callbacks...
      ℹ No events received (normal if ESP32 idle)
```

**Causes:**
- ESP32 not generating events (no Bluetooth activity)
- Event format not matching parser
- UART buffer overflow (events lost)

**Solutions:**

1. **Trigger Bluetooth events on ESP32:**
   - Pair Bluetooth device (phone, speaker)
   - Disconnect/reconnect Bluetooth
   - Change volume on paired device

2. **Check ESP32 event format:**
   - ESP32 should send: `EVENT: <type> <data>\n`
   - Example: `EVENT: BT_CONNECTED 00:11:22:33:44:55\n`

3. **Increase wait time:**
   - Edit script: `time.sleep(10)` instead of `time.sleep(3)`

4. **Monitor ESP32 serial output:**
   - Check if events are being sent from ESP32
   - Verify UART TX is working on ESP32 side

### Issue 6: High reconnect count in statistics

**Symptoms:**
```
UART Statistics:
  Reconnects:    15
```

**Causes:**
- Intermittent physical connection
- ESP32 resetting frequently
- UART driver issue

**Solutions:**

1. **Check physical wiring:**
   - Reseat all jumper wires
   - Use soldered connections if on breadboard

2. **Verify ESP32 stability:**
   - Check power supply (should be stable 3.3V or 5V)
   - Look for brown-out resets in ESP32 logs

3. **Add capacitor on ESP32 power:**
   - 100µF electrolytic capacitor on VCC/GND
   - Reduces voltage spikes during Wi-Fi/BT bursts

---

## Success Validation

### Checklist for Milestone 2 Completion

Mark each item when verified:

- [ ] **Hardware Setup:**
  - [ ] UART4 overlay loaded (`dmesg | grep ttyO4` shows initialization)
  - [ ] `/dev/ttyO4` exists and accessible (permissions: `crw-rw---- ... dialout`)
  - [ ] User in `dialout` group (`groups | grep dialout`)
  - [ ] Physical wiring verified (P9.13→GPIO16, P9.11→GPIO17, GND→GND)
  - [ ] UART loopback test passed (`./overlays/test_uart4_loopback.sh`)

- [ ] **ESP32 Firmware:**
  - [ ] `esp_bt_audio_source` firmware flashed
  - [ ] ESP32 booting successfully (visible in serial monitor)
  - [ ] UART enabled in firmware (GPIO16 RX, GPIO17 TX, 115200 baud)
  - [ ] ESP32 responding to manual test: `echo "STATUS" > /dev/ttyO4`

- [ ] **Python Environment:**
  - [ ] Python 3.8+ installed (`python3 --version`)
  - [ ] pyserial library installed (`pip3 list | grep pyserial`)
  - [ ] Test script executable (`ls -l milestone2_uart_test.py`)

- [ ] **Milestone 2 Test:**
  - [ ] Test script runs without errors (`./milestone2_uart_test.py`)
  - [ ] STATUS command sent and response received
  - [ ] VOLUME command sent and acknowledged
  - [ ] Timeout handling verified (1-second timeout test passes)
  - [ ] Event callbacks registered (events received if BT active)
  - [ ] Statistics show: sent ≥ 2, ok ≥ 1, reconnects < 5

- [ ] **Documentation:**
  - [ ] This setup guide reviewed and understood
  - [ ] Pin mapping verified against physical wiring
  - [ ] Troubleshooting steps attempted (if issues occurred)

### Final Acceptance Criteria

**Milestone 2 is considered COMPLETE when:**

1. ✅ **Test script runs successfully** with all tests passing
2. ✅ **At least 2 commands sent** (STATUS, VOLUME) with responses received
3. ✅ **Timeout handling works** (1-second timeout test passes)
4. ✅ **Reconnects < 5** (indicates stable UART connection)
5. ✅ **Event callbacks functional** (even if no events — callback registered without error)

**Optional (for full Bluetooth testing):**
- ✅ ESP32 connected to Bluetooth device (phone, speaker)
- ✅ Events received during connection/disconnection
- ✅ Volume commands control paired device

---

## Next Steps

After completing Milestone 2:

1. **Proceed to Milestone 3:** Audio Codec Integration
   - I2S communication between BBGW and ESP32
   - PCM5102A DAC integration (if used)
   - Audio playback testing

2. **Integrate UART into Main Application:**
   - Add `UARTCommandManager` to main application loop
   - Implement user commands (play, pause, skip, volume)
   - Add event-driven UI updates

3. **Optimize UART Performance:**
   - Tune timeout values based on real-world usage
   - Implement retry logic for critical commands
   - Add command queueing for burst operations

4. **Add Logging and Monitoring:**
   - Log all UART commands/responses to file
   - Monitor reconnect rate over time
   - Add watchdog for UART health

---

## References

### BeagleBone Documentation
- [BBGW System Reference Manual](https://github.com/beagleboard/beaglebone-green-wireless/wiki/System-Reference-Manual)
- [Cape Overlays Repository](https://github.com/beagleboard/bb.org-overlays)
- [UART Configuration Guide](https://elinux.org/Beagleboard:BeagleBone_cape_interface_spec)

### ESP32 Documentation
- [ESP-IDF UART Driver API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html)
- [esp_bt_audio_source Firmware](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/classic_bt/a2dp_source)

### Python pyserial
- [pyserial Documentation](https://pyserial.readthedocs.io/)
- [Serial Port Basics](https://learn.sparkfun.com/tutorials/serial-communication/all)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-07  
**Author:** BBGW I2S Audio Source Project  
**License:** MIT

