# NVS Write Rate / Flash Wear Audit

**Date:** 2026-02-09  
**Task:** CODE_REVIEW8 Task D (P0 - Critical)  
**Objective:** Audit all NVS write/commit operations to prevent premature flash wear

---

## Executive Summary

**CRITICAL FINDING**: Volume changes write to NVS flash on **every single adjustment** with NO debouncing.

**Risk Level**: 🔴 **HIGH** - Can cause premature flash wear and reduced device lifetime  
**Recommended Action**: Implement debouncing with delayed commit strategy

---

## Background: NVS Flash Wear

- **ESP32 NVS Storage**: Backed by SPI flash memory
- **Flash Write Cycles**: Typical SPI flash rated for ~100,000 erase cycles per sector
- **NVS Wear Leveling**: ESP-IDF NVS uses wear leveling, but excessive writes still degrade lifespan
- **Concern**: Rapid repeated commits to same key can exhaust write cycles prematurely

**Example Scenario**:
- User adjusts volume using encoder knob from 0→100 (100 steps)
- Each step triggers immediate NVS commit
- **Result**: 100 flash writes in ~10 seconds

**Lifetime Calculation** (worst case):
- If user adjusts volume 10 times/day (1000 writes/day)
- Flash lifecycle: ~100,000 writes ÷ 1000 writes/day = 100 days
- **Actual lifetime likely 1-2 years** (wear leveling helps, but still concerning)

---

## Audit Results

### 📊 NVS Write Inventory

| Caller Path | Trigger | Frequency | Flash Writes | Risk | Debounced? |
|------------|---------|-----------|--------------|------|------------|
| **Volume** | VOLUME command | **HIGH** | **Every change** | 🔴 **CRITICAL** | ❌ **NO** |
| I2S Pins | I2S_CONFIG command | Very Low | Once per config | ✅ Low | N/A (infrequent) |
| Audio Autostart | AUDIO_AUTOSTART command | Very Low | Toggle only | ✅ Low | N/A (infrequent) |
| Default PIN | SET_DEFAULT_PIN command | Very Low | Set once | ✅ Low | N/A (infrequent) |
| Device Name | SET_NAME command | Very Low | Set once | ✅ Low | N/A (infrequent) |
| Paired Devices (Add) | Successful pairing | Low | Per pairing | ✅ Low | N/A (event-driven) |
| Paired Devices (Remove) | UNPAIR command | Low | Per removal | ✅ Low | N/A (event-driven) |
| Paired Devices (Clear) | UNPAIR_ALL command | Very Low | Once | ✅ Low | N/A (event-driven) |

---

## Detailed Findings

### 🔴 CRITICAL: Volume Changes (Unbounded Write Rate)

**Call Chain:**
```
VOLUME <0-100>
  └─> cmd_handlers_audio.c:252 → cmd_handle_volume()
      └─> audio_processor.c:722 → audio_processor_set_volume()
          └─> nvs_storage.c:68 → nvs_storage_set_volume()
              └─> nvs_storage.c:76 → nvs_set_i32("volume", ...)
                  └─> nvs_storage.c:78 → nvs_commit() ⚠️ IMMEDIATE COMMIT
```

**Current Behavior:**
- `audio_processor_set_volume()` **immediately commits** to NVS on line 722
- NO delay, NO debouncing, NO rate limiting
- Every VOLUME command triggers flash write

**Problem Scenarios:**
1. **Volume Encoder/Slider**: User sweeps volume 0→100 = 100 flash writes
2. **Automated Control**: Script/automation sending rapid VOLUME commands
3. **Remote Control**: Bluetooth AVRCP volume up/down repeatedly pressed

**Code Evidence** (audio_processor.c:705-725):
```c
esp_err_t audio_processor_set_volume(uint8_t volume)
{
    // ... validation ...
    
    s_volume_gain = volume;
    s_audio_config.volume = volume;

    // ⚠️ CRITICAL: Persist new volume IMMEDIATELY - NO DEBOUNCING
    nvs_storage_set_volume(s_volume_gain);  // Line 722

    ESP_LOGI(TAG, "Audio volume set to %d%%", volume);
    return ESP_OK;
}
```

**Recommendation**: Implement delayed commit with debounce timer (500-1000ms)

---

### ✅ Non-Critical: I2S Pin Configuration

**Frequency**: Very Low (user configuration command, typically once per lifetime)  
**Caller**: `audio_processor.c:848` (I2S_CONFIG command)  
**Justification**: Pin configuration is infrequent setup operation  
**Verdict**: **No action needed** ✅

---

### ✅ Non-Critical: Audio Autostart Setting

**Frequency**: Very Low (user toggle command, rare)  
**Callers**: 
- `cmd_handlers_audio.c:347` (AUDIO_AUTOSTART ON)
- `cmd_handlers_audio.c:361` (AUDIO_AUTOSTART OFF)
**Justification**: Feature toggle, not adjusted frequently  
**Verdict**: **No action needed** ✅

---

### ✅ Non-Critical: Default PIN Setting

**Frequency**: Very Low (set once during initial setup)  
**Callers**:
- `cmd_handlers_bt.c:398` (SET_DEFAULT_PIN command - ESP_PLATFORM)
- `cmd_handlers_bt.c:405` (SET_DEFAULT_PIN command - host builds)
**Justification**: One-time or very rare configuration  
**Verdict**: **No action needed** ✅

---

### ✅ Non-Critical: Device Name Setting

**Frequency**: Very Low (typically set once)  
**Callers**:
- `cmd_handlers_bt.c:482` (SET_NAME command - ESP_PLATFORM)
- `cmd_handlers_bt.c:491` (SET_NAME command - host builds)
**Justification**: Device identity, rarely changed  
**Verdict**: **No action needed** ✅

---

### ✅ Acceptable: Paired Device Operations

**Add Pairing** (Low-Medium Frequency):
- **Callers**: 
  - `bt_manager.c:240` (successful authentication with device name)
  - `bt_manager.c:1732` (successful pairing without name)
- **Trigger**: Bluetooth pairing success event
- **Frequency**: Only when user pairs new device (typically <10 times in device lifetime)
- **Commit**: Line 394 in `nvs_storage_add_paired_device()`
- **Justification**: Event-driven, infrequent operation
- **Verdict**: **No action needed** ✅

**Remove Pairing** (Low Frequency):
- **Function**: `nvs_storage_remove_paired_device()`
- **Trigger**: UNPAIR command
- **Frequency**: User-initiated, infrequent
- **Commit**: Line 467 (after shifting entries)
- **Verdict**: **No action needed** ✅

**Clear All Pairings** (Very Low Frequency):
- **Function**: `nvs_storage_clear_paired_devices()`
- **Trigger**: UNPAIR_ALL command
- **Frequency**: Rare (factory reset scenario)
- **Commit**: Line 488 (once after clearing all)
- **Verdict**: **No action needed** ✅

---

## Recommended Solutions

### Solution A: Delayed Commit with Debounce Timer (RECOMMENDED)

**Strategy**: Update in-memory value immediately, delay NVS commit until settled

**Implementation**:
1. When `audio_processor_set_volume()` called:
   - Update `s_volume_gain` immediately (existing behavior)
   - Cancel any pending NVS commit timer
   - Start new timer (500-1000ms)
2. On timer expiration:
   - Commit current `s_volume_gain` to NVS
   - Clear timer

**Benefits**:
- ✅ Reduces flash writes from "every change" to "once per adjustment session"
- ✅ User sees immediate volume response (in-memory update)
- ✅ Flash only written when user "settles" on final value
- ✅ Handles rapid sequences: 100 commands → 1 flash write

**Example**: User adjusts volume 0→50→75→100 in 2 seconds:
- **Without debounce**: 4 flash writes
- **With debounce (500ms)**: 1 flash write (after 500ms from last change)

**ESP-IDF Implementation**:
```c
static esp_timer_handle_t s_volume_commit_timer = NULL;

static void volume_commit_callback(void* arg) {
    (void)arg;
    nvs_storage_set_volume(s_volume_gain);
}

// In audio_processor_init():
esp_timer_create_args_t timer_args = {
    .callback = volume_commit_callback,
    .name = "volume_nvs_commit"
};
esp_timer_create(&timer_args, &s_volume_commit_timer);

// In audio_processor_set_volume():
s_volume_gain = volume;
esp_timer_stop(s_volume_commit_timer);  // Cancel pending
esp_timer_start_once(s_volume_commit_timer, 500000);  // 500ms
```

---

### Solution B: Write Throttling with Minimum Interval

**Strategy**: Enforce minimum time between NVS commits (e.g., 1 second)

**Benefits**:
- ✅ Simple to implement
- ✅ Guarantees maximum write rate

**Drawbacks**:
- ❌ First write always happens (not optimal for rapid sequences)
- ❌ May lose intermediate values if user adjusts during throttle window

**Verdict**: Less optimal than Solution A

---

### Solution C: "Dirty Flag" with Periodic Flush

**Strategy**: Mark volume as dirty, flush periodically (e.g., every 5 seconds)

**Benefits**:
- ✅ Very low write rate
- ✅ Simple implementation

**Drawbacks**:
- ❌ Risk of data loss on unexpected power loss/crash
- ❌ Up to 5-second delay before persistence

**Verdict**: Not recommended (data loss risk)

---

## Implementation Plan

### Phase 1: Add Debounced Volume Commit ✅ RECOMMENDED

**Files to modify**:
1. `components/audio_processor/audio_processor.c`:
   - Add `esp_timer_handle_t s_volume_commit_timer`
   - Add `volume_commit_callback()` function
   - Modify `audio_processor_init()` to create timer
   - Modify `audio_processor_set_volume()` to use debounced commit
   - Modify `audio_processor_deinit()` to cleanup timer

**Testing**:
- Unit test: Verify rapid volume changes result in single NVS write
- Integration test: Confirm volume persists across reboots
- Stress test: 100 rapid VOLUME commands → verify <5 flash writes

**Estimated effort**: 30-45 minutes

---

### Phase 2: Document NVS Write Strategy

**Files to update**:
1. `ARCH.md`: Add "NVS Write Strategy" section
2. `components/nvs_storage/nvs_storage.h`: Add header comments
3. `CODE_REVIEW8_TODO.md`: Mark task D complete

**Content**:
- Document debounce strategy for volume
- Document acceptable write frequencies for other settings
- Note flash wear considerations
- Reference this audit document

**Estimated effort**: 15-20 minutes

---

## Acceptance Criteria

- [x] All NVS write paths identified and documented
- [x] Write frequencies assessed and categorized by risk
- [ ] Volume debouncing implemented with esp_timer
- [ ] Unit tests validate debouncing behavior
- [ ] Host tests pass (244/244 or 245/245 if new test added)
- [ ] Manual hardware test: rapid volume changes → observe flash writes
- [ ] NVS write strategy documented in ARCH.md
- [ ] CODE_REVIEW8_TODO.md updated with completion status

---

## Summary Statistics

**Total NVS Write Operations Identified**: 8 distinct paths  
**Critical (Needs Fix)**: 1 (Volume)  
**Non-Critical (Infrequent)**: 7 (I2S, Autostart, PIN, Name, Paired devices)

**Flash Wear Reduction** (estimated with debouncing):
- Worst case without debounce: 100 writes/volume-adjustment
- Best case with debouncing: 1 write/volume-adjustment
- **Improvement: 99% reduction in flash writes**

**Projected Device Lifetime Improvement**:
- Current (no debounce): ~1-2 years with heavy volume use
- With debouncing: **10+ years** (limited by other factors)

---

## References

- ESP-IDF NVS Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
- ESP-IDF Timer Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html
- CODE_REVIEW8.md: External code review identifying this issue
- CODE_REVIEW8_TODO.md: Task tracking document
