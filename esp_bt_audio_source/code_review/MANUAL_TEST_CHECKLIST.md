# Manual On-Device Testing Checklist
## CODE_REVIEW2 Phase 6, Task 6.2

**Purpose:** Verify all CODE_REVIEW2 changes work correctly on actual hardware.

---

## Test Session Information
- **Date:** _______________
- **Tester:** _______________
- **Device MAC:** _______________
- **Firmware Version:** v0.1.0-440-gdda324f3

---

## 1. Boot Sequence Verification

### 1.1 Flash and Monitor
```bash
cd esp_bt_audio_source
. $HOME/esp/esp-idf/export.sh
idf.py flash monitor
```

### 1.2 Early DIAG Markers
Expected output at boot (Phase 0):
- [ ] `DIAG: Early boot marker - UART working` appears immediately
- [ ] Marker appears **BEFORE** other subsystem init logs
- [ ] No corruption or garbled characters in DIAG marker

**WHY:** Verifies UART driver installed early for diagnostics (Phase 2, Task 2.3).  
**Location:** main.c line ~165-170

---

## 2. Initialization Order Verification

### 2.1 Platform Services
- [ ] NVS initialization logs appear: `I/NVS_STORAGE: nvs_storage_init`
- [ ] UART driver already installed (no `uart_driver_install` log)
- [ ] BLE memory release logs: `BLE controller mem release OK`

### 2.2 Command Interface
- [ ] Command task startup: `I/COMMAND: cmd_init...`
- [ ] Command task ready **BEFORE** BT manager init
- [ ] No "queue not initialized" errors

**WHY:** Verifies init order fix (Phase 2, Task 2.6): UART → NVS → CMD → BT → Audio

### 2.3 Bluetooth Subsystem
- [ ] BT manager initialization logs
- [ ] A2DP source profile registration
- [ ] Device becomes discoverable

### 2.4 Audio Subsystem
- [ ] Audio processor init with config from NVS/Kconfig
- [ ] I2S driver configuration logged
- [ ] Autostart behavior check (see Test 4.1)

---

## 3. Command Interface Testing

### 3.1 STATUS Command
```
STATUS
```

**Expected:**
- [ ] Returns system status (BT state, audio state)
- [ ] No errors or exceptions
- [ ] Response is well-formatted

**PASS/FAIL:** ______  
**Notes:** ________________________________

### 3.2 SCAN Command
```
SCAN
```

**Expected:**
- [ ] Returns list of nearby Bluetooth devices
- [ ] Shows device names and MACs
- [ ] Completes within reasonable time (~10s)

**PASS/FAIL:** ______  
**Notes:** ________________________________

### 3.3 PAIR Command (if test device available)
```
PAIR AA:BB:CC:DD:EE:FF
```

**Expected:**
- [ ] Pairing sequence initiates
- [ ] PIN request events appear (if required)
- [ ] Success or failure clearly indicated
- [ ] Can complete pairing flow with ENTER_PIN / CONFIRM_PIN commands

**PASS/FAIL:** ______  
**Notes:** ________________________________

---

## 4. Audio Configuration Testing

### 4.1 Autostart Behavior (Default Enabled)

**Test:** Boot device, connect to BT speaker

**Expected:**
- [ ] Audio starts **automatically** after BT connection established
- [ ] No manual PLAY command required
- [ ] Logs show: `Autostart flag: enabled` or `Audio starting automatically`

**WHY:** Verifies Phase 3 Task 3.2 - runtime autostart feature  
**PASS/FAIL:** ______  
**Notes:** ________________________________

### 4.2 Toggle Autostart OFF, Reboot, Verify
```
AUDIO_AUTOSTART off
```
Then reboot device.

**Expected after reboot:**
- [ ] Device connects to BT speaker
- [ ] Audio does **NOT** start automatically
- [ ] Logs show: `Autostart flag: disabled`
- [ ] Manual PLAY command starts audio successfully

**WHY:** Verifies NVS persistence of autostart flag  
**PASS/FAIL:** ______  
**Notes:** ________________________________

### 4.3 Toggle Autostart ON, Reboot, Verify
```
AUDIO_AUTOSTART on
```
Then reboot device.

**Expected after reboot:**
- [ ] Audio starts automatically again (behavior restored)
- [ ] Logs show: `Autostart flag: enabled`

**PASS/FAIL:** ______  
**Notes:** ________________________________

### 4.4 AUDIO_AUTOSTART GET Command
```
AUDIO_AUTOSTART get
```

**Expected:**
- [ ] Returns current autostart state (on/off)
- [ ] Matches last SET command
- [ ] Persists across reboots

**PASS/FAIL:** ______  
**Notes:** ________________________________

### 4.5 PLAY Command (Manual Audio Start)
```
PLAY
```

**Expected:**
- [ ] Audio starts playing
- [ ] No errors
- [ ] Works regardless of autostart setting

**PASS/FAIL:** ______  
**Notes:** ________________________________

---

## 5. NVS Pin Override Testing

### 5.1 Set Custom I2S Pins
```
SET_I2S_PINS 18 19 21 23
```
(Example: BCK=18, WS=19, DIN=21, DOUT=23)

**Expected:**
- [ ] Command acknowledges pin storage
- [ ] Confirmation message appears

**PASS/FAIL:** ______  
**Notes:** ________________________________

### 5.2 Reboot and Verify Pins Respected
Reboot device and check logs.

**Expected:**
- [ ] Boot logs show custom pins loaded from NVS
- [ ] Audio processor init uses custom pins (check I2S config logs)
- [ ] Audio works with new pin configuration (if hardware permits)

**WHY:** Verifies Phase 3 Task 3.1 - NVS pin override hierarchy  
**PASS/FAIL:** ______  
**Notes:** ________________________________

### 5.3 Clear Pin Overrides (Reset to Defaults)
```
CLEAR_I2S_PINS
```
Then reboot.

**Expected:**
- [ ] Boot logs show default pins (BCK=26, WS=25, DIN=22)
- [ ] Audio works with default pins

**PASS/FAIL:** ______  
**Notes:** ________________________________

---

## 6. Error Handling Verification

### 6.1 Platform Services Fail-Fast
**Test:** Manually corrupt NVS (if possible) or disconnect flash

**Expected:**
- [ ] ESP_ERROR_CHECK aborts on NVS failure
- [ ] Device resets rather than limping along
- [ ] Clear error message before reset

**WHY:** Verifies Phase 4 Task 4.5 - fail-fast policy for platform services  
**PASS/FAIL:** ______  
**Notes:** ________________________________

### 6.2 Subsystem Graceful Degradation
**Test:** Disconnect BT antenna or disable audio hardware

**Expected:**
- [ ] BT/Audio init failures logged with `esp_err_to_name()`
- [ ] Device continues running (doesn't abort)
- [ ] CMD interface remains accessible
- [ ] Can still issue commands for diagnostics

**WHY:** Verifies graceful degradation for subsystems  
**PASS/FAIL:** ______  
**Notes:** ________________________________

---

## 7. Regression Checks

### 7.1 UART Never Deleted
**Test:** Use command interface repeatedly over multiple minutes

**Expected:**
- [ ] Commands continue working indefinitely
- [ ] No "UART not installed" errors
- [ ] No command interface hangs or failures

**WHY:** Verifies Phase 2 Task 2.4 - removed uart_driver_delete() call  
**PASS/FAIL:** ______  
**Notes:** ________________________________

### 7.2 No Preprocessor Guard Issues
**Test:** Build for ESP32-S3 or ESP32-C3 (if available)

**Expected:**
- [ ] Build succeeds
- [ ] DIAG markers still appear on alternate targets
- [ ] No "undefined reference to esp_rom_printf" errors

**WHY:** Verifies Phase 1 fix - `#ifdef CONFIG_IDF_TARGET_ESP32` instead of `#ifdef esp_rom_printf`  
**PASS/FAIL:** ______  
**Notes:** ________________________________

---

## 8. Performance Spot Checks

### 8.1 Boot Time
- **Time to DIAG marker:** _______ ms
- **Time to CMD ready:** _______ ms
- **Time to BT discoverable:** _______ ms
- **Total boot time:** _______ s

### 8.2 Command Responsiveness
- **STATUS response time:** _______ ms
- **SCAN completion time:** _______ s
- **Audio start latency:** _______ ms

### 8.3 Heap Usage (from logs)
- **Free heap at boot:** _______ KB
- **Free heap after BT conn:** _______ KB
- **Free heap during audio:** _______ KB
- **Minimum free heap:** _______ KB

---

## Gate Checkpoint: Manual Tests Pass

**All critical tests passing?** YES / NO  
**Regressions detected?** YES / NO  
**Ready for production?** YES / NO

**Summary Notes:**
________________________________________________________________
________________________________________________________________
________________________________________________________________

**Tested by:** _______________  
**Date:** _______________  
**Sign-off:** _______________

---

## Quick Reference: Expected Boot Log Sequence

```
DIAG: Early boot marker - UART working
I (XXX) BT: BLE controller mem release OK
I (XXX) NVS_STORAGE: nvs_storage_init
I (XXX) COMMAND: cmd_init - Starting command task
I (XXX) BT_MANAGER: Initializing Bluetooth manager
I (XXX) AUDIO_PROCESSOR: Loading audio config (NVS → Kconfig → fallback)
I (XXX) AUDIO_PROCESSOR: Autostart flag: enabled/disabled
I (XXX) AUDIO_PROCESSOR: I2S pins - BCK:XX WS:XX DIN:XX DOUT:XX
... [BT/Audio init continues] ...
```

---

## Appendix: Test Commands Reference

```bash
# Command Interface
STATUS                      # System status
SCAN                        # Scan for BT devices
PAIR <MAC>                  # Pair with device
ENTER_PIN <MAC> <PIN>       # Enter pairing PIN
CONFIRM_PIN <MAC> <PIN>     # Confirm pairing

# Audio Control
PLAY                        # Start audio playback
STOP                        # Stop audio
AUDIO_AUTOSTART on|off|get  # Configure autostart

# NVS Pin Configuration
SET_I2S_PINS <bck> <ws> <din> <dout>  # Override I2S pins
CLEAR_I2S_PINS                         # Reset to defaults
```
