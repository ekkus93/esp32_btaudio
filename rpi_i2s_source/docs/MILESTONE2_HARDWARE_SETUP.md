# Milestone 2: UART Command Interface — Hardware Setup Guide

## Overview

This guide walks through the hardware setup and testing for **Milestone 2: UART Command Interface**, validating UART communication between Raspberry Pi and ESP32 esp_bt_audio_source.

---

## Hardware Requirements

### Components Needed

1. **Raspberry Pi** (tested on Pi 3B+/4)
   - With UART enabled on GPIO14/GPIO15
   
2. **ESP32 Development Board** with esp_bt_audio_source firmware
   - UART configured (typically GPIO1/GPIO3 or hardware UART)
   
3. **Jumper Wires** (female-to-female)

4. **USB-to-TTL Serial Adapter** (optional, for debugging)

---

## Step 1: Raspberry Pi UART Configuration

### 1.1. Enable UART

Edit `/boot/config.txt`:

```bash
sudo nano /boot/config.txt
```

Add or modify these lines:

```
# Enable UART on GPIO14/GPIO15
enable_uart=1

# Disable Bluetooth to free up hardware UART (recommended)
dtoverlay=disable-bt
```

**Note:** On Raspberry Pi 3/4, Bluetooth uses the hardware UART by default. Disabling Bluetooth frees GPIO14/15 for your use.

### 1.2. Disable Serial Console

Disable the login console on serial (to prevent interference):

```bash
sudo raspi-config
```

Navigate to:
- **3 Interface Options** → **I6 Serial Port**
- "Would you like a login shell accessible over serial?" → **No**
- "Would you like the serial port hardware enabled?" → **Yes**

Reboot:

```bash
sudo reboot
```

### 1.3. Verify UART Device

After reboot, check for UART device:

```bash
ls -l /dev/serial0
```

Expected output:

```
lrwxrwxrwx 1 root root 5 Feb  6 14:00 /dev/serial0 -> ttyAMA0
```

`/dev/serial0` is a symlink to the primary UART (`ttyAMA0` on Pi 4, `ttyS0` on Pi 3 if BT disabled).

### 1.4. Install Python Dependencies

```bash
# Install pyserial
sudo apt-get install -y python3-serial

# Or via pip
pip3 install pyserial
```

---

## Step 2: ESP32 UART Configuration

### 2.1. Identify ESP32 UART Pins

Check your esp_bt_audio_source firmware configuration for UART pins. Common defaults:

| ESP32 Pin | Function | Raspberry Pi Pin |
|-----------|----------|------------------|
| GPIO3 (U0RXD) | UART RX | GPIO14 (TXD) |
| GPIO1 (U0TXD) | UART TX | GPIO15 (RXD) |
| GND | Ground | GND |

**Note:** ESP32's default UART0 (GPIO1/3) may conflict with USB programming. Consider using UART1 or UART2 if available.

### 2.2. Flash esp_bt_audio_source

Ensure your ESP32 has the esp_bt_audio_source firmware with UART command interface enabled:

```bash
cd /path/to/esp32_btaudio/esp_bt_audio_source
. $HOME/esp/esp-idf/export.sh
idf.py build flash monitor
```

Check ESP32 logs for UART initialization:

```
I (1234) UART: UART initialized on GPIO1/GPIO3, 115200 baud
I (1235) CMD_IF: Command interface ready
```

---

## Step 3: Wiring

### 3.1. Physical Connection

Connect Raspberry Pi UART to ESP32 UART:

| Raspberry Pi (BCM) | ESP32 GPIO | Description |
|--------------------|------------|-------------|
| GPIO 14 (TXD)      | GPIO 3 (RXD) | Raspberry Pi transmits, ESP32 receives |
| GPIO 15 (RXD)      | GPIO 1 (TXD) | Raspberry Pi receives, ESP32 transmits |
| GND                | GND        | Common ground |

**CRITICAL:**
- **TX → RX** and **RX → TX** (crossover)
- **Do not** connect 5V to ESP32 (ESP32 is 3.3V only)
- **Verify** ESP32 GPIO levels are 3.3V (Raspberry Pi GPIO is also 3.3V, so safe)

### 3.2. Verify Wiring

Use a multimeter to verify connections before powering on:
- Continuity between RPi TXD and ESP32 RXD
- Continuity between RPi RXD and ESP32 TXD
- Common ground

---

## Step 4: Run Milestone 2 Test

### 4.1. Basic Test

```bash
cd /path/to/esp32_btaudio/rpi_i2s_source
chmod +x milestone2_uart_test.py
./milestone2_uart_test.py
```

Expected output:

```
======================================================================
Milestone 2: UART Command Interface Test
======================================================================

Configuration:
  Serial Device: /dev/serial0
  Baudrate:      115200
  Timeout:       5.0 seconds

----------------------------------------------------------------------

[1/5] Starting UART manager...
      ✓ UART manager started

[2/5] Testing STATUS command...
      ✓ STATUS command successful
        Response: OK|STATUS|IDLE

[3/5] Testing VOLUME command...
      ✓ VOLUME 75 command successful
        Response: OK|VOLUME|SET

[4/5] Testing timeout handling...
      ✓ Timeout handling works correctly
        (Command timed out as expected with 1s timeout)

[5/5] Testing event callbacks...
      ✓ Event callback registered
        Waiting 3 seconds for events...
      ℹ No events received (normal if ESP32 idle)

----------------------------------------------------------------------
Test Summary:

UART Statistics:
  Commands sent: 2
  OK responses:  2
  ERR responses: 0
  Events:        0
  Reconnects:    0

Milestone 2 Success Criteria:
  [  ✓  ] STATUS command sent and response received
  [  ✓  ] VOLUME 75 command sent
  [  ✓  ] Timeout handling verified

✓ Milestone 2 test completed
```

### 4.2. Custom Serial Device

If using a different serial device (e.g., USB-to-TTL adapter):

```bash
./milestone2_uart_test.py --device /dev/ttyUSB0
```

---

## Step 5: Manual UART Testing (Optional)

### 5.1. Using `screen` or `minicom`

Test UART communication manually:

```bash
# Using screen
sudo screen /dev/serial0 115200

# Or using minicom
sudo minicom -D /dev/serial0 -b 115200
```

Type commands manually:

```
STATUS
```

Expected ESP32 response:

```
OK|STATUS|IDLE
```

### 5.2. Using Python Serial Console

Create a simple Python script:

```python
import serial

ser = serial.Serial('/dev/serial0', 115200, timeout=1)

# Send STATUS command
ser.write(b'STATUS\n')

# Read response
response = ser.readline()
print(f"Response: {response.decode('utf-8').strip()}")

ser.close()
```

---

## Step 6: Verification Checklist

### Manual Verification

- [ ] **Raspberry Pi UART enabled** (`/dev/serial0` exists)
- [ ] **ESP32 firmware running** (check with `idf.py monitor`)
- [ ] **Wiring correct** (TX ↔ RX crossover, GND connected)
- [ ] **STATUS command works** (returns valid response)
- [ ] **VOLUME command works** (acknowledged by ESP32)

### Automated Verification (from test script)

- [ ] **Commands sent** (> 0)
- [ ] **OK responses** (> 0)
- [ ] **Timeout handling** verified
- [ ] **Zero ERR responses** (indicates clean communication)

---

## Troubleshooting

### Issue: "Permission denied" on /dev/serial0

**Symptoms:** `PermissionError: [Errno 13] Permission denied: '/dev/serial0'`

**Fix:**
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Logout and login again, or:
newgrp dialout
```

### Issue: No response from ESP32

**Symptoms:** Command timeout, no OK/ERR response

**Troubleshoot:**
1. **Check ESP32 is running:** `idf.py monitor` should show logs
2. **Check wiring:** Verify TX/RX crossover, GND connected
3. **Check baudrate:** Both sides must use 115200 (default)
4. **Check UART pins:** Verify ESP32 using correct GPIO (check firmware)
5. **Test loopback:** Disconnect ESP32, short RPi TX to RX, send command (should echo back)

### Issue: Garbled/corrupt data

**Symptoms:** Invalid characters, parse errors

**Fix:**
1. **Check baudrate:** Must match (115200)
2. **Check grounding:** Ensure solid GND connection
3. **Check cable length:** Keep wires < 30 cm
4. **Check interference:** Move away from power supplies, motors

### Issue: "Serial device not found"

**Symptoms:** `/dev/serial0` doesn't exist

**Fix:**
1. **Check config.txt:** Ensure `enable_uart=1`
2. **Disable Bluetooth:** Add `dtoverlay=disable-bt`
3. **Reboot:** `sudo reboot`
4. **Check alternatives:** Try `/dev/ttyAMA0` or `/dev/ttyS0`

### Issue: ESP32 not responding after commands

**Symptoms:** First command works, subsequent timeout

**Fix:**
1. **Check ESP32 command parser:** May be stuck/crashed
2. **Reset ESP32:** Press RESET button
3. **Check ESP32 logs:** `idf.py monitor` for errors
4. **Increase timeout:** Use longer timeout (10s) for slow ESP32

---

## Success Criteria (Milestone 2)

### Deliverables

✅ **1. pyserial UART communication to esp_bt_audio_source**
- Implemented in `uart/command_manager.py`
- Test coverage: `tests/test_uart_command_manager.py` (33 tests passing)

✅ **2. UARTCommandManager with methods**
- `send_command()` — blocking command/response
- `parse_response()` — OK/ERR parsing
- `wait_for_event()` — event callbacks

✅ **3. Command queue with timeout handling**
- Future-based response tracking
- Configurable timeout (default 5s)

✅ **4. Simple CLI test**
- `milestone2_uart_test.py` — automated test script

### Success Criteria

✅ **1. `STATUS` command returns valid response**
- Send `STATUS\n`
- Receive `OK|STATUS|...\n`
- Parse response correctly

✅ **2. `VOLUME 75` command changes volume**
- Send `VOLUME 75\n`
- Receive `OK|VOLUME|SET\n`
- No errors

✅ **3. Timeout handling works**
- Unplug ESP32
- Send command with 5s timeout
- TimeoutError raised after 5 seconds

---

## Next Steps

After completing Milestone 2:

1. **Milestone 3:** Flask Web UI
   - Dashboard with tone controls
   - Bluetooth device scanning/connection
   - Real-time status via SSE

2. **Milestone 4:** Advanced Audio Sources
   - Frequency sweep, WAV playback
   - Already implemented, needs hardware test

3. **Milestone 5:** Stability and Telemetry
   - 1-hour continuous test
   - CPU/memory monitoring
   - Systemd service

---

## References

- **Raspberry Pi UART:** [raspberrypi.org/documentation/configuration/uart.md](https://www.raspberrypi.org/documentation/configuration/uart.md)
- **pyserial Documentation:** [pyserial.readthedocs.io](https://pyserial.readthedocs.io/)
- **ESP-IDF UART Driver:** [docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html)
- **esp_bt_audio_source Command Protocol:** See `esp_bt_audio_source/docs/FS.md` Section 2.1
