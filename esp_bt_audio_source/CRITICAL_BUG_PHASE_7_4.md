# CRITICAL BUG DISCOVERED IN PHASE 7.4 - Watchdog Timeout

**Date**: February 8, 2026  
**Phase**: 7.4 Manual Smoke Tests  
**Severity**: CRITICAL - System Unusable  
**Status**: Root cause identified, fix applied, rebuild required

---

## Summary

The main firmware triggers continuous task watchdog timeouts, making the system completely unusable. The audio_engine task on CPU 1 prevents the IDLE1 task from running, causing watchdog triggers every 5 seconds.

---

## Root Cause Analysis

### Trigger Sequence
1. `CONFIG_AUDIO_AUTOSTART_DEFAULT=y` in sdkconfig
2. During boot, `main.c:385` calls `audio_processor_start()`
3. `audio_processor_start()` creates `audio_engine_task` at priority `(configMAX_PRIORITIES - 2)` (very high)
4. Task runs continuously on CPU 1, even with NO active audio source
5. High-priority task starves IDLE1 task (priority 0) on CPU 1
6. IDLE1 cannot run → cannot reset watchdog → watchdog triggers every 5s

### Evidence
```
E (28290) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (28290) task_wdt:  - IDLE1 (CPU 1)
E (28290) task_wdt: Tasks currently running:
E (28290) task_wdt: CPU 0: IDLE0
E (28290) task_wdt: CPU 1: audio_engine
E (28290) task_wdt: Print CPU 1 backtrace

Backtrace: ... audio_engine_task at audio_processor.c:232
```

### Code Locations
- **Config**: `sdkconfig:448` - `CONFIG_AUDIO_AUTOSTART_DEFAULT=y`
- **Boot call**: `main.c:385` - `audio_processor_start()` called if autostart enabled
- **Task creation**: `audio_processor.c:400` - `xTaskCreate(audio_engine_task, ...)`
- **Task priority**: `audio_processor_internal.h:65` - `#define AUDIO_ENGINE_TASK_PRIORITY (configMAX_PRIORITIES - 2)`
- **Stuck loop**: `audio_processor.c:232` - `vTaskDelay(delay_ticks)` in main loop

---

## Why This Wasn't Caught in Tests

**Test configurations (test_app, test_app_audio)**:
- Use Unity test framework with different initialization sequences
- Either don't enable autostart OR initialize with proper audio sources configured
- Tests run with controlled audio source states

**Main firmware**:
- Boots with autostart enabled
- No I2S hardware connected
- No Bluetooth A2DP connection established
- audio_engine_task runs with NO valid audio source → tight loop → watchdog

---

## Design Flaw

`audio_processor_start()` should **NOT** be called during boot without:
1. A Bluetooth A2DP connection established AND
2. I2S capture hardware connected/configured

OR the `audio_engine_task` should:
1. Suspend itself when no audio source is active
2. Use much longer delays when idle (not 2ms tight loop)
3. Run at lower priority to allow IDLE task CPU time

---

## Fix Applied

**File**: `esp_bt_audio_source/sdkconfig`  
**Line**: 448  
**Change**:
```diff
-CONFIG_AUDIO_AUTOSTART_DEFAULT=y
+# CONFIG_AUDIO_AUTOSTART_DEFAULT is not set
```

This prevents automatic audio processor start during boot. Audio will need to be started explicitly via command interface after Bluetooth connection.

---

## Rebuild and Test Procedure

### 1. Disconnect ESP32 USB
```bash
# Physical cable disconnect to stop watchdog spam in terminal
```

### 2. Clean Rebuild
```bash
cd ~/work/esp32/esp32_btaudio/esp_bt_audio_source
. $HOME/esp/esp-idf/export.sh
idf.py fullclean
idf.py build
```

### 3. Reconnect ESP32 and Flash
```bash
idf.py -p /dev/ttyUSB0 flash
```

### 4. Monitor Boot (Should be clean - NO watchdog errors)
```bash
idf.py -p /dev/ttyUSB0 monitor
```

**Expected boot output:**
- Bluetooth initialization SUCCESS
- NO audio processor start messages
- NO watchdog errors
- System ready but audio NOT running

### 5. Test Command Interface

#### Test 1: BEEP Command (Should FAIL - audio not started)
```
Command: BEEP
Expected: Error or message indicating audio not started
```

#### Test 2: START Audio Manually
```
Command: START
Expected: Audio processor initializes and starts
```

#### Test 3: BEEP After START (Should WORK)
```
Command: BEEP
Expected: 10-second tone plays over Bluetooth
```

#### Test 4: PLAY Command (CRITICAL - Must REJECT)
```
Command: PLAY test.wav
Expected: "Unknown command" or "Command not recognized"
Result: [DOCUMENT HERE]
```

### 6. Verify No Watchdog Errors
```
Expected: Clean serial output, no task_wdt errors
Actual: [DOCUMENT HERE]
```

---

## Long-Term Fix Required

**Option A: Conditional Task Creation**
- Only create `audio_engine_task` when Bluetooth A2DP connected
- Delete task when connection lost
- Modify `audio_processor_start()` to check for Bluetooth connection

**Option B: Smart Task Suspension**
- Have `audio_engine_task` check for active audio sources
- If no source active for N iterations, call `vTaskSuspend(NULL)`
- Resume task when audio source becomes available

**Option C: Priority Adjustment**
- Lower `AUDIO_ENGINE_TASK_PRIORITY` to allow IDLE task to run
- Risk: May cause audio underruns if Bluetooth takes too much CPU
- Needs careful tuning

**Recommended**: **Option A** - Only run audio engine when actually needed

---

## Impact on Test Results

**Automated Tests**: ✅ ALL PASSING (108/108)
- Phase 7.1 Host Tests: 33/33 ✅
- Phase 7.2 Component Tests: 46/46 ✅
- Phase 7.3 Integration Tests: 29/29 ✅

**Manual Tests**: ⚠️ **BLOCKED** by this bug
- Cannot test BEEP command
- Cannot test PLAY rejection
- Cannot test Bluetooth streaming
- System completely unusable due to watchdog spam

---

## Next Steps

1. **IMMEDIATE**: User disconnects ESP32 USB cable
2. **BUILD**: User runs clean rebuild with fix applied
3. **FLASH**: User flashes fixed firmware
4. **TEST**: User executes Phase 7.4 manual tests per MANUAL_SMOKE_TEST_GUIDE.md
5. **DOCUMENT**: User records test results
6. **DESIGN FIX**: Create proper long-term solution (Option A recommended)
7. **CONTINUE**: Phase 7.5 Flash Usage Check
8. **CONTINUE**: Phase 7.6 Regression Testing

---

## Files Modified

- `esp_bt_audio_source/sdkconfig` - Disabled CONFIG_AUDIO_AUTOSTART_DEFAULT

## Files Created

- `CRITICAL_BUG_PHASE_7_4.md` (this document)
- `MANUAL_SMOKE_TEST_GUIDE.md` (manual test procedures)

---

## Lessons Learned

1. **Test coverage gap**: Autostart path not tested in automated test suites
2. **Integration testing needed**: Main firmware boot sequence needs end-to-end test
3. **Idle task monitoring**: Should have watchdog safeguards for high-priority tasks
4. **Design review**: Starting subsystems without resources is dangerous pattern

---

**Status**: Fix applied, awaiting rebuild and manual verification.
