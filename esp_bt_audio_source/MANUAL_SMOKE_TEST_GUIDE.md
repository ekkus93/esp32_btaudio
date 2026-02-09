# Phase 7.4 Manual Smoke Test Guide

**Date:** 2026-02-08  
**Firmware:** esp_bt_audio_source v0.2.0-mainc-stable-157-ga70c63  
**Binary:** build/esp_bt_audio_source.bin (922,160 bytes, 52% of partition)

## Prerequisites

1. **Hardware:**
   - ESP32 board connected via USB
   - Bluetooth speaker (paired and discoverable)
   - Optional: I2S audio source (BBGW or similar)

2. **Firmware:**
   - Main firmware built: `build/esp_bt_audio_source.bin`
   - Flash command: `cd esp_bt_audio_source && idf.py -p PORT flash monitor`

## Test Procedures

### 1. Initial Setup

```bash
# Find your ESP32's serial port
ls -l /dev/ttyUSB* /dev/ttyACM*

# Flash firmware (replace PORT with actual port)
cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source
. $HOME/esp/esp-idf/export.sh
idf.py -p /dev/ttyUSB0 flash monitor
```

### 2. Connect to Bluetooth Speaker

**Expected behavior:**
- ESP32 boots and initializes Bluetooth
- Device becomes discoverable
- Logs show Bluetooth initialization without errors

**Check logs for:**
- ✅ No SPIFFS mount errors (SPIFFS removed in Phase 1)
- ✅ Bluetooth stack initialized successfully
- ✅ I2S driver initialized
- ✅ Audio processor ready

### 3. Test BEEP Command

**Command:** Type `BEEP` in serial monitor

**Expected:**
- ✅ Beep tone plays on Bluetooth speaker
- ✅ Duration: ~10 seconds
- ✅ Frequency: 440 Hz (A4 note)
- ✅ Can interrupt I2S audio if playing

**Serial log verification:**
- Look for: "BEEP command received"
- Look for: "Audio processor: BEEP mode"
- No errors about missing WAV files

### 4. Test PLAY Command Rejection ⚠️ CRITICAL

**Command:** Type `PLAY test.wav` in serial monitor

**Expected (THIS IS THE KEY TEST):**
- ❌ Command should be REJECTED or UNKNOWN
- ✅ Error message: "Unknown command" or "Command not found"
- ✅ NO attempt to mount SPIFFS
- ✅ NO attempt to open WAV file
- ✅ NO play_manager errors

**This confirms PLAY functionality was successfully removed.**

### 5. Test SYNTH Mode (if implemented)

**Commands:**
```
SYNTH ON
SYNTH OFF
```

**Expected:**
- `SYNTH ON`: Synthetic tone plays on Bluetooth speaker
- `SYNTH OFF`: Tone stops, returns to I2S or silence

### 6. Test I2S Capture (optional, requires I2S source)

**Setup:**
- Connect I2S audio source to ESP32
- Ensure I2S pins configured correctly

**Expected:**
- Audio from I2S source plays on Bluetooth speaker
- Quality is acceptable (no distortion, clicks, pops)
- Can switch between I2S and BEEP modes

### 7. Verify Serial Logs

**Check for these patterns:**

✅ **Expected warnings (SPIFFS removed):**
```
W: Partition 'spiffs' not found
E: SPIFFS: spiffs partition could not be found
W: SPIFFS mount failed: ESP_ERR_NOT_FOUND
```

❌ **Should NOT appear:**
```
- "play_manager" errors
- "WAV file not found"
- SPIFFS write/read errors
- File system corruption warnings
```

## Results Documentation

### Test Results Summary

| Test | Status | Notes |
|------|--------|-------|
| Bluetooth Connection | ⬜ Pass / ⬜ Fail | |
| BEEP Command | ⬜ Pass / ⬜ Fail | Duration: ___ sec |
| PLAY Command Rejection | ⬜ Pass / ⬜ Fail | Error message: |
| SYNTH Mode | ⬜ Pass / ⬜ Fail / ⬜ N/A | |
| I2S Capture | ⬜ Pass / ⬜ Fail / ⬜ N/A | |
| Serial Logs Clean | ⬜ Pass / ⬜ Fail | |

### Flash Usage Verification

Compare with Phase 1 baseline:

**Current build:**
```
Binary size: 922,160 bytes (0xe1230)
Partition: 1,769,472 bytes (1.76 MB)
Free: 847,312 bytes (48%)
```

**Expected reduction:** ~50-100 KB from SPIFFS/WAV removal

### Notes

Record any observations, issues, or unexpected behavior:

```
[Add notes here]
```

## Completion Checklist

- [ ] ESP32 flashed successfully
- [ ] Connected to Bluetooth speaker
- [ ] BEEP command tested and working
- [ ] PLAY command properly rejected ⚠️ CRITICAL
- [ ] SYNTH tested (if applicable)
- [ ] I2S capture tested (if applicable)
- [ ] No SPIFFS/play_manager errors in logs
- [ ] Results documented above

## Next Steps

After completing manual tests:

1. Update Phase 7.4 checklist in `code_review/REMOVE_PLAY_TODO.md`
2. Save this file with test results
3. Proceed to Phase 7.5: Flash Usage Check
4. Then Phase 8: Documentation Updates

---

**Generated:** 2026-02-08  
**For:** REMOVE_PLAY_TODO.md Phase 7.4 verification
