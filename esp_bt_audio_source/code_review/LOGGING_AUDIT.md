# Logging Audit for Hot Paths (CODE_REVIEW8 Task P1.C)

**Date**: 2026-02-09  
**Auditor**: GitHub Copilot  
**Scope**: All printf() and ESP_LOG*() calls in production code paths

---

## Executive Summary

**CRITICAL FINDING**: Blocking ESP_LOGW() calls in audio data callback hot path can cause audio glitches.

**Findings**:
- **1 CRITICAL** hot path with blocking I/O (bt_audio_data_callback)
- **0 medium priority** issues (connection callbacks use ESP_LOGI which is acceptable)
- **All test/mock code** logging is properly gated

**Actions Required**:
1. Remove blocking ESP_LOGW from bt_audio_data_callback() 
2. Add compile-time logging gates for verbose paths
3. Document logging policy

---

## Detailed Findings

### CRITICAL: Audio Data Callback (HOT PATH ⚠️)

**File**: `components/bt_manager/bt_streaming_manager.c`  
**Function**: `bt_audio_data_callback()`  
**Call Frequency**: ~344 Hz (every 2.9ms at 44.1kHz, 128-byte packets)  
**Risk**: Audio glitches, timing violations

**Blocking calls found**:
```c
// Line 68 - ERROR path
ESP_LOGW(TAG, "audio_processor_read error: %d", result);

// Line 73 - UNDERRUN path
ESP_LOGW(TAG, "Audio buffer underrun (%zu/%d bytes)", bytes_read, (int)len);
```

**Analysis**:
- ESP_LOGW performs blocking UART output (~1-5ms per call)
- Can exceed 2.9ms audio frame deadline
- Underruns trigger more logging, creating death spiral
- Should use non-blocking alternatives or compile-time gates

**Recommendation**: **REMOVE** or gate with `#if CONFIG_BT_VERBOSE_AUDIO_LOGGING`

---

### ACCEPTABLE: Connection Event Callbacks

**File**: `components/bt_manager/bt_events_a2dp.c`  
**Function**: `bt_events_handle_a2dp_connection()`  
**Call Frequency**: ~1-2 per connection (rare)

**Calls found**:
```c
// Line 30 - Connection established
ESP_LOGI(TAG, "Connected to device: %s", bda_str);

// Line 48 - Auto-start result
ESP_LOGI(TAG, "Auto-start after connect -> %s", ...);
```

**Analysis**:
- Connection events are infrequent (seconds apart)
- Not in audio data path
- ESP_LOGI is acceptable for diagnostic events
- No timing constraints violated

**Recommendation**: **KEEP AS-IS** (acceptable)

---

### ACCEPTABLE: AVRC Control Callbacks

**File**: `components/bt_manager/bt_events_avrc.c`  
**Function**: `bt_events_avrc_callback()`  
**Call Frequency**: ~1-10 per user interaction (rare)

**Calls found**:
```c
// Line 22 - Connection state
ESP_LOGI(TAG, "AVRCP connection state: %d", connected ? 1 : 0);

// Line 26 - Passthrough response (debug level)
ESP_LOGD(TAG, "AVRCP passthrough rsp key=%d state=%d", ...);

// Line 29 - Remote features (debug level)
ESP_LOGD(TAG, "AVRCP remote features: 0x%x", ...);

// Line 32 - Other events (debug level)
ESP_LOGD(TAG, "AVRCP event: %d", event);
```

**Analysis**:
- Control events are infrequent
- Not in audio data path
- ESP_LOGD is compile-time disabled in production (CONFIG_LOG_MAXIMUM_LEVEL)
- ESP_LOGI for rare connection events is acceptable

**Recommendation**: **KEEP AS-IS** (acceptable, debug logs already gated)

---

### ACCEPTABLE: Command Interface Logging

**File**: `components/command_interface/commands.c`  
**Function**: Various diagnostic paths

**Calls found**:
```c
// Line 91 - Event emission diagnostic (gated by ESP_PLATFORM)
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "DIAG-EVENT: %s", buf);
#else
    printf("DIAG-EVENT: %s\n", buf);  // Host test only
#endif

// Line 101 - Test hook debug (host test only)
printf("HOOK-DEBUG: test_push_event symbol present, forwarding event\n");

// Line 106 - Test hook debug (host test only)
printf("HOOK-DEBUG: test_push_event not present or n<=0\n");

// Line 141 - Parse diagnostic
printf("PARSE-DIAG: token='%s'\n", token);
```

**Analysis**:
- Event emission happens in command handler path (not audio hot path)
- Most printf() calls are host-test only
- Line 141 printf appears to be debug leftover (should be removed or gated)

**Recommendation**: 
- **REMOVE** line 141 printf (orphaned debug statement)
- **KEEP** ESP_LOGI on line 89 (acceptable, not in hot path)
- **KEEP** test hook printfs (host-only, needed for testing)

---

### ACCEPTABLE: Mock Build Diagnostics

**File**: `components/bt_manager/bt_connection.c`  
**Function**: `bt_disconnect()` (mock builds only)

**Calls found**:
```c
// Line 170 - Mock build diagnostic
#if defined(CONFIG_BT_MOCK_TESTING)
    printf("DIAG: mgr_bt_disconnect (mock build) set connected flag to %d\n", bt_ctx.connected);
#endif
```

**File**: `components/bt_manager/bt_manager.c`  
**Functions**: Mock pairing and connection functions

**Calls found**:
```c
// Line 797 - Mock pairing diagnostic
printf("DIAG: bt_manager_start_pair failed for %s: err=%d\n", ...);

// Line 875 - Mock scan result
printf("Mock BT: Device found: %s, name: %s, RSSI: %d\n", ...);

// Line 881 - Mock connection trace
printf("TRACE: bt_manager_mock_connection_established called\n");

// Line 898 - Mock connection established
printf("Mock BT: Connected to device: %s, name: %s\n", mac, name);
```

**Analysis**:
- All calls are in mock/test code paths
- Only active with CONFIG_BT_MOCK_TESTING defined
- Not compiled in production builds
- Essential for test diagnostics

**Recommendation**: **KEEP AS-IS** (test infrastructure only)

---

### ACCEPTABLE: Main Application Logging

**File**: `main/main.c`  
**Functions**: Boot diagnostics and initialization

**Calls found**:
```c
// Line 384 - NVS read error diagnostic
printf("DIAG|AUDIO|NVS_READ_ERROR|key=autostart|err=%s|fallback=kconfig_default\r\n", ...);

// Lines 393, 397, 399, 410 - Audio initialization diagnostics
printf("DIAG|AUDIO|STATUS|...\r\n", ...);
```

**Analysis**:
- Boot-time diagnostics only (not in hot paths)
- Essential for debugging production issues
- Uses DIAG_MARKER macro (printf + esp_rom_printf for reliability)
- Runs during app_main() before audio streaming starts

**Recommendation**: **KEEP AS-IS** (boot diagnostics, not in hot path)

---

## Summary by Component

| Component | File | Critical? | Action Required |
|-----------|------|-----------|-----------------|
| **bt_streaming_manager** | bt_streaming_manager.c | ⚠️ **YES** | Remove ESP_LOGW from audio callback |
| bt_events_a2dp | bt_events_a2dp.c | No | Keep as-is |
| bt_events_avrc | bt_events_avrc.c | No | Keep as-is |
| command_interface | commands.c | No | Remove line 141 debug printf |
| bt_manager (mock) | bt_manager.c | No | Keep as-is (test only) |
| bt_connection (mock) | bt_connection.c | No | Keep as-is (test only) |
| main | main.c | No | Keep as-is (boot only) |

---

## Hot Path Definition

For this audit, "hot paths" are defined as code paths executed at audio frame rate or higher:

1. **CRITICAL (>100 Hz)**: Audio data callback, ISRs
   - **Requirement**: NO blocking I/O allowed
   - **ESP_LOG policy**: Forbidden (compile-time gate only)
   
2. **HIGH (10-100 Hz)**: Audio state callbacks, frequent events
   - **Requirement**: Minimize blocking I/O
   - **ESP_LOG policy**: ESP_LOGD only (disabled in production)
   
3. **MEDIUM (1-10 Hz)**: Connection events, command processing
   - **Requirement**: Acceptable blocking I/O
   - **ESP_LOG policy**: ESP_LOGI acceptable, ESP_LOGW for errors

---

## Recommended Logging Policy

### Production Builds (CONFIG_LOG_MAXIMUM_LEVEL=ESP_LOG_INFO)

```c
// CRITICAL HOT PATHS (audio data callback, ISRs)
// ❌ FORBIDDEN: Any ESP_LOG* calls
// ✅ ALLOWED: Compile-time gated verbose logging only

#if CONFIG_BT_VERBOSE_AUDIO_LOGGING
    ESP_LOGW(TAG, "Audio underrun: %zu/%d", bytes_read, len);
#endif

// CONNECTION/EVENT CALLBACKS (infrequent)
// ✅ ALLOWED: ESP_LOGI for state changes, ESP_LOGW for errors
// ❌ AVOID: ESP_LOGD (already disabled by CONFIG_LOG_MAXIMUM_LEVEL)

ESP_LOGI(TAG, "Connected to device: %s", bda_str);  // OK
ESP_LOGW(TAG, "Connection failed: %s", esp_err_to_name(err));  // OK

// INITIALIZATION (boot-time only)
// ✅ ALLOWED: Any logging (printf, ESP_LOG*, DIAG_MARKER)

printf("DIAG|AUDIO|STATUS|initialized=1\r\n");  // OK
```

---

## Implementation Plan

### Phase 1: Fix Critical Hot Path (IMMEDIATE)
1. Remove ESP_LOGW from bt_audio_data_callback() in bt_streaming_manager.c
2. Add CONFIG_BT_VERBOSE_AUDIO_LOGGING Kconfig option (default OFF)
3. Gate verbose audio logging with compile-time check
4. Test with full test suite to ensure no regressions

### Phase 2: Add Logging Policy Documentation
1. Document logging policy in ARCH.md
2. Add hot path guidelines to component READMEs
3. Update CODE_REVIEW8_TODO.md with completion status

### Phase 3: Validation
1. Run full test suite with verbose logging OFF
2. Run full test suite with verbose logging ON (manually enable)
3. Verify production builds have clean audio (no glitches)
4. Confirm diagnostic logging still works for debugging

**Estimated Time**: 45-60 minutes total

---

## Test Validation Plan

1. **Host tests**: All 244 tests must pass (both verbose ON/OFF)
2. **Device tests**: All 141 tests must pass (both verbose ON/OFF)
3. **Manual audio test**: Play continuous audio, verify no glitches/underruns
4. **Manual underrun test**: Force underrun, verify no logging death spiral

---

## References

- CODE_REVIEW8.md: Original audit recommendation (Section on logging in hot paths)
- CODE_REVIEW8_TODO.md: Task P1.C "Audit Logging in Hot Paths"
- ESP-IDF Logging: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html
- Audio timing constraints: 44.1kHz / 128 bytes = 2.9ms frame deadline
