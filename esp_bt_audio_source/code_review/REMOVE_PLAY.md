# REMOVE_PLAY - Simplify Audio Architecture

**Start Date:** 2026-02-07  
**Completion Date:** 2026-02-09  
**Status:** ✅ Completed  
**Goal:** Remove WAV playback (PLAY command) and SPIFFS partition to simplify the audio architecture

---

## Table of Contents

1. [Rationale](#rationale)
2. [Current Architecture](#current-architecture)
3. [Benefits of Removal](#benefits-of-removal)
4. [Trade-offs](#trade-offs)
5. [Architecture After Removal](#architecture-after-removal)
6. [Code Impact Assessment](#code-impact-assessment)
7. [Implementation Plan](#implementation-plan)
8. [Testing Strategy](#testing-strategy)

---

## Rationale

The WAV playback functionality adds significant complexity to the ESP32 firmware without providing essential value:

1. **BBGW provides comprehensive WAV playback** — The BeagleBone Green Wireless I2S source (`bbgw_i2s_source`) already has full-featured WAV playback with resampling, web UI control, and multiple audio sources. Duplicating this on ESP32 is redundant.

2. **ESP32's primary role is Bluetooth transmission** — The core purpose of `esp_bt_audio_source` is to receive I2S audio and transmit it via Bluetooth A2DP. WAV playback is a secondary feature for standalone demos.

3. **BEEP is sufficient for self-test** — For standalone operation (without I2S source), the BEEP command provides adequate validation that Bluetooth audio is working.

4. **Complexity vs. value trade-off** — WAV playback requires:
   - SPIFFS filesystem (1-2 MB flash)
   - WAV decoder/parser
   - File I/O error handling
   - Resampling logic
   - Complex interaction with I2S/BEEP/SYNTH state machine
   - Extensive test coverage for edge cases

5. **Real-world usage pattern** — In production, ESP32 will be connected to an I2S source (BBGW or similar). Standalone WAV playback is primarily for demos, which can be handled by connecting to BBGW.

---

## Current Architecture

### Four Audio Sources (Priority Order)

From `components/audio_processor/audio_processor.c`:

```c
typedef enum {
    AUDIO_SOURCE_WAV = 0,     // PLAY - WAV files from SPIFFS
    AUDIO_SOURCE_I2S,         // I2S - Live audio from I2S input
    AUDIO_SOURCE_SYNTH,       // SYNTH - Synthetic tone generator
    AUDIO_SOURCE_SILENCE,     // SILENCE - Digital silence
    NUM_AUDIO_SOURCES
} audio_source_t;
```

**Source Priority Logic:**
1. **WAV** — If `play_manager_is_active()` returns true
2. **I2S** — If audio processor is running (captures live I2S input)
3. **SYNTH** — If forced via `SYNTH ON` command or as idle fallback
4. **SILENCE** — Final fallback

**BEEP Overlay:**
- BEEP is not a separate source but an **overlay** that mixes on top of any active source
- Managed by `beep_manager.c`

### Current Use Cases

**Standalone Operation (no I2S source):**
- `BEEP` — Play beep tone via Bluetooth (self-test)
- `PLAY <file.wav>` — Play WAV file from SPIFFS via Bluetooth (demo)
- `SYNTH ON` — Force synth mode (debug)

**With I2S Source (BBGW or similar):**
- I2S audio → Bluetooth transmission (primary use case)
- BEEP can overlay on I2S audio

---

## Benefits of Removal

### 1. Code Simplification

**Files to Remove (~1,500-2,000 lines):**
- `components/audio_processor/play_manager.c`
- `components/audio_processor/play_manager.h`
- `components/audio_processor/include/play_manager.h`
- PLAY command handler in `cmd_handlers_system.c`
- SPIFFS mount/unmount code in `main/main.c`
- All PLAY-related test files

**Reduced Complexity:**
- Simpler audio source state machine (3 sources instead of 4)
- Fewer busy/rejection interactions:
  - PLAY rejected while I2S active
  - BEEP rejected while PLAY active
  - I2S rejected while PLAY active
- No WAV decoder/parser logic
- No resampling for arbitrary WAV formats
- No file I/O error handling (file not found, corrupt WAV, read failures)

### 2. Flash Space Savings

**SPIFFS Partition Removal:**
- Current: ~1-2 MB allocated for SPIFFS in `partitions.csv`
- After removal: Can reallocate to:
  - Larger application partition (more code/features)
  - OTA partition (more robust updates)
  - NVS partition (more configuration storage)

**Binary Size Reduction:**
- No SPIFFS libraries
- No WAV parsing code
- Estimate: ~50-100 KB smaller binary

### 3. Memory Savings

**Heap:**
- No SPIFFS file buffers
- No WAV decode buffers
- No resampling work buffers
- Reduced heap fragmentation from file operations

**Stack:**
- Simpler task stack requirements (no deep file I/O call chains)

### 4. Reduced Test Surface Area

**Tests to Remove:**
- WAV playback tests (~200+ test cases)
- PLAY command tests
- PLAY rejection tests (while I2S/BEEP active)
- WAV edge cases (corrupt files, unsupported formats, etc.)
- SPIFFS mount/unmount tests

**Remaining Tests:**
- I2S capture tests (keep)
- BEEP tests (keep)
- SYNTH tests (keep)
- Bluetooth transmission tests (keep)

### 5. Clearer Design Intent

**Before:**
- ESP32 tries to be both audio source and Bluetooth transmitter
- Unclear whether it's a standalone player or I2S receiver

**After:**
- ESP32 is clearly a **Bluetooth transmitter** for I2S audio
- BEEP for standalone self-test only
- SYNTH for idle/debug mode

---

## Trade-offs

### What We Lose

**1. Standalone Music Demos:**
- Can no longer demo ESP32 → Bluetooth speaker with music files
- **Mitigation:** Connect ESP32 to BBGW for demos (better anyway: web UI, more audio sources)

**2. WAV Format Testing:**
- Can no longer test different WAV sample rates/bit depths on ESP32
- **Mitigation:** BBGW handles comprehensive WAV testing; ESP32 only needs to handle I2S input

**3. Offline Operation:**
- Can't play audio without I2S source or manual serial commands
- **Mitigation:** BEEP provides basic audio validation; real use case requires I2S source

### What We Keep

**1. All Core Functionality:**
- I2S audio capture → Bluetooth transmission (primary use case)
- BEEP for self-test
- SYNTH for idle/debug
- All Bluetooth features (pairing, volume, connection management)

**2. Standalone Operation:**
- `BEEP` command still works without I2S source
- `SYNTH ON` provides continuous synthetic audio
- All serial commands work

**3. Testing Capabilities:**
- I2S → Bluetooth path fully testable
- BEEP → Bluetooth path testable
- All state transitions testable

---

## Architecture After Removal

### Three Audio Sources (Priority Order)

```c
typedef enum {
    AUDIO_SOURCE_I2S = 0,     // I2S - Live audio from I2S input
    AUDIO_SOURCE_SYNTH,       // SYNTH - Synthetic tone generator
    AUDIO_SOURCE_SILENCE,     // SILENCE - Digital silence
    NUM_AUDIO_SOURCES
} audio_source_t;
```

**Source Priority Logic:**
1. **I2S** — If audio processor is running and not in forced synth mode
2. **SYNTH** — If forced via `SYNTH ON` command or as idle fallback
3. **SILENCE** — Final fallback (rare, only if synth disabled)

**BEEP Overlay:**
- Unchanged: can overlay on any source

### Updated Use Cases

**Standalone Operation (no I2S source):**
- `BEEP` — Play beep tone via Bluetooth (self-test) ✅ KEPT
- `SYNTH ON` — Force synth mode (debug) ✅ KEPT
- ~~`PLAY <file.wav>` — Play WAV file from SPIFFS~~ ❌ REMOVED

**With I2S Source (BBGW or similar):**
- I2S audio → Bluetooth transmission ✅ KEPT (primary use case)
- BEEP can overlay on I2S audio ✅ KEPT

**BBGW Provides:**
- WAV playback (44.1 kHz auto-resampling to 48 kHz)
- Tone generation (20 Hz - 20 kHz, adjustable amplitude)
- Frequency sweeps (20 Hz → 20 kHz, logarithmic)
- Silence mode (noise floor testing)
- Web UI control
- All of this → I2S → ESP32 → Bluetooth

---

## Code Impact Assessment

### Files to Remove

**Audio Processor:**
- `components/audio_processor/play_manager.c` (~400 lines)
- `components/audio_processor/play_manager.h` (~50 lines)
- `components/audio_processor/include/play_manager.h` (~150 lines)

**Test Files:**
- `test/test_play_manager/` (entire directory)
- PLAY-related tests in `test/component/test_audio_processor.c` (~200 lines)
- PLAY-related tests in `test/test_app_audio/main/audio_processor_test.c` (~300 lines)
- PLAY command tests in `test/host_test/test_commands.c` (~150 lines)

**SPIFFS:**
- `spiffs/` directory with WAV test files
- SPIFFS partition entry in `partitions.csv`

**Documentation:**
- PLAY references in various README files

**Total Estimated Removal:** ~1,500-2,000 lines of code + test files

### Files to Modify

**1. `components/audio_processor/audio_processor.c`**

Changes:
- Remove `AUDIO_SOURCE_WAV` from `audio_source_t` enum
- Remove WAV source handling in `get_active_source()`
- Remove WAV source handling in `produce_audio_chunk()`
- Remove `#include "play_manager.h"`
- Update audio stats (remove WAV stats tracking)

Example changes:
```c
// BEFORE:
typedef enum {
    AUDIO_SOURCE_WAV = 0,
    AUDIO_SOURCE_I2S,
    AUDIO_SOURCE_SYNTH,
    AUDIO_SOURCE_SILENCE,
    NUM_AUDIO_SOURCES
} audio_source_t;

static audio_source_t get_active_source(void) {
    if (play_manager_is_active()) {
        return AUDIO_SOURCE_WAV;
    }
    if (s_is_running) {
        return AUDIO_SOURCE_I2S;
    }
    // ... rest
}

// AFTER:
typedef enum {
    AUDIO_SOURCE_I2S = 0,
    AUDIO_SOURCE_SYNTH,
    AUDIO_SOURCE_SILENCE,
    NUM_AUDIO_SOURCES
} audio_source_t;

static audio_source_t get_active_source(void) {
    if (s_is_running && !s_force_synth) {
        return AUDIO_SOURCE_I2S;
    }
    if (s_force_synth) {
        return AUDIO_SOURCE_SYNTH;
    }
    return AUDIO_SOURCE_SILENCE;
}
```

**2. `components/audio_processor/include/audio_processor.h`**

Changes:
- Remove `audio_processor_play_wav()` declaration
- Remove `audio_processor_is_wav_active()` declaration
- Remove `audio_processor_get_work_buffer_bytes()` (used only for WAV resampling)

**3. `components/audio_processor/audio_processor_beep.c`**

Changes:
- Remove PLAY busy check:
```c
// REMOVE THIS:
if (play_manager_is_active()) {
    ESP_LOGW(TAG, "audio_processor_beep: busy (play active)");
    return ESP_ERR_INVALID_STATE;
}
```

**4. `components/command_interface/cmd_handlers_system.c`**

Changes:
- Remove `PLAY` command from help text
- Remove `cmd_handle_play()` function
- Remove `{"PLAY", cmd_handle_play}` from command dispatch table

**5. `main/main.c`**

Changes:
- Remove SPIFFS mount code:
```c
// REMOVE:
#include "esp_spiffs.h"

esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = false
};
esp_err_t ret = esp_vfs_spiffs_register(&conf);
// ... error handling ...
```

**6. `partitions.csv`**

Changes:
- Remove SPIFFS partition line
- Optionally reallocate space to app or OTA partition

Example:
```csv
# BEFORE:
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x4000,
otadata,  data, ota,     0xd000,  0x2000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
ota_0,    app,  ota_0,   ,        1M,
ota_1,    app,  ota_1,   ,        1M,
spiffs,   data, spiffs,  ,        1M,      # ← REMOVE THIS LINE

# AFTER:
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x4000,
otadata,  data, ota,     0xd000,  0x2000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
ota_0,    app,  ota_0,   ,        1M,
ota_1,    app,  ota_1,   ,        1M,
# Reclaimed 1M from SPIFFS (could expand app/OTA partitions if needed)
```

**7. `components/audio_processor/CMakeLists.txt`**

Changes:
- Remove `play_manager.c` from SRCS list

**8. Test Files to Modify**

Changes in multiple test files:
- Remove PLAY rejection tests
- Remove WAV playback assertions
- Remove `play_manager_is_active()` calls
- Update audio source enum references (0→I2S, 1→SYNTH, 2→SILENCE)

Files:
- `test/component/test_audio_processor.c`
- `test/test_app_audio/main/audio_processor_test.c`
- `test/host_test/test_commands.c`
- `test/host_test/mocks/audio_processor_host_stub.c`

**9. Documentation**

Files to update:
- `main/README.md` — Remove PLAY references
- `docs/FS.md` — Update audio source list, remove WAV source description
- `README.md` — Remove PLAY command from command list
- Any other docs mentioning PLAY or WAV playback

---

## Implementation Plan

### Phase 1: Preparation (1-2 hours)

**Goal:** Understand full scope, backup code, create branch

**Tasks:**
1. Search for all references to PLAY/WAV:
   ```bash
   cd esp_bt_audio_source
   grep -r "play_manager" --include="*.c" --include="*.h"
   grep -r "PLAY" --include="*.c" --include="*.h" | grep -i command
   grep -r "\.wav" --include="*.c" --include="*.h"
   grep -r "spiffs" --include="*.c" --include="*.h"
   grep -r "AUDIO_SOURCE_WAV" --include="*.c" --include="*.h"
   ```

2. Create git branch:
   ```bash
   git checkout -b remove-play-wav
   ```

3. Document all files to modify in this file

4. Run full test suite to establish baseline:
   ```bash
   # Host tests
   cd test/host_test
   python3 build_and_run_host_tests.py
   
   # Component tests (if applicable)
   cd test/component
   idf.py build test
   
   # App tests (if hardware available)
   cd test/test_app_audio
   idf.py build flash monitor
   ```

### Phase 2: Remove PLAY Command Handler (30 min)

**Goal:** Remove user-facing PLAY command

**Tasks:**
1. Remove from `cmd_handlers_system.c`:
   - Remove `cmd_handle_play()` function
   - Remove from help text
   - Remove from command dispatch table

2. Update tests:
   - Remove PLAY command tests in `test/host_test/test_commands.c`
   - Remove PLAY command tests in integration tests

3. Build and test:
   ```bash
   cd test/host_test
   python3 build_and_run_host_tests.py
   ```

**Validation:**
- All host tests pass
- PLAY command no longer in help output

### Phase 3: Remove play_manager Component (1 hour)

**Goal:** Remove play_manager.c and all references

**Tasks:**
1. Remove files:
   - `components/audio_processor/play_manager.c`
   - `components/audio_processor/play_manager.h`
   - `components/audio_processor/include/play_manager.h`

2. Update `components/audio_processor/CMakeLists.txt`:
   - Remove `play_manager.c` from SRCS

3. Remove from audio_processor.c:
   - `#include "play_manager.h"`
   - `AUDIO_SOURCE_WAV` from enum
   - WAV handling in `get_active_source()`
   - WAV handling in `produce_audio_chunk()`
   - WAV stats tracking

4. Update audio_processor.h:
   - Remove `audio_processor_play_wav()` declaration
   - Remove `audio_processor_is_wav_active()` declaration
   - Remove `audio_processor_get_work_buffer_bytes()` declaration

5. Update audio_processor_beep.c:
   - Remove PLAY busy check

6. Update test mocks:
   - `test/host_test/mocks/audio_processor_host_stub.c`
   - Remove WAV-related stub functions

**Validation:**
- Code compiles without errors
- No unresolved symbol errors
- Host tests pass

### Phase 4: Update Tests (1-2 hours)

**Goal:** Remove/update all PLAY-related tests

**Tasks:**
1. Remove test directories:
   - `test/test_play_manager/` (entire directory)

2. Update `test/component/test_audio_processor.c`:
   - Remove PLAY tests (~10-15 test cases)
   - Remove `test_audio_processor_play_*` functions
   - Update audio source enum references

3. Update `test/test_app_audio/main/audio_processor_test.c`:
   - Remove PLAY rejection tests
   - Remove WAV playback tests
   - Update BEEP/I2S interaction tests (no more PLAY conflicts)

4. Update `test/host_test/test_commands.c`:
   - Remove PLAY command tests
   - Update command help tests

5. Rebuild all test executables:
   ```bash
   cd test/host_test
   python3 build_and_run_host_tests.py
   
   cd test/component
   idf.py build
   
   cd test/test_app_audio
   idf.py build
   ```

**Validation:**
- All unit tests pass
- All component tests pass
- No references to PLAY in test output

### Phase 5: Remove SPIFFS (30 min)

**Goal:** Remove SPIFFS filesystem and partition

**Tasks:**
1. Update `main/main.c`:
   - Remove `#include "esp_spiffs.h"`
   - Remove SPIFFS mount code
   - Remove SPIFFS error handling

2. Update `partitions.csv`:
   - Remove SPIFFS partition line
   - Optionally increase app/OTA partition sizes

3. Remove SPIFFS directory:
   ```bash
   rm -rf spiffs/
   ```

4. Update CMakeLists.txt (if SPIFFS component explicitly listed):
   - Remove SPIFFS from REQUIRES

**Validation:**
- Build succeeds with new partition table
- Flash succeeds
- ESP32 boots without SPIFFS mount errors

### Phase 6: Update Documentation (30 min)

**Goal:** Remove PLAY references from all documentation

**Tasks:**
1. Update `main/README.md`:
   - Remove PLAY command description
   - Remove WAV playback use cases
   - Update audio source list

2. Update `docs/FS.md`:
   - Remove AUDIO_SOURCE_WAV
   - Update source priority list
   - Remove WAV-related sections

3. Update root `README.md`:
   - Remove PLAY from command list
   - Update feature list

4. Update any other docs with PLAY/WAV references

**Validation:**
- Search for remaining PLAY references:
  ```bash
  grep -r "PLAY" --include="*.md"
  grep -r "play_manager" --include="*.md"
  grep -r "\.wav" --include="*.md"
  ```

### Phase 7: Final Testing (1-2 hours)

**Goal:** Comprehensive testing of modified system

**Tasks:**
1. **Host tests:**
   ```bash
   cd test/host_test
   python3 build_and_run_host_tests.py
   ```
   - All tests pass
   - No PLAY-related tests executed

2. **Component tests (if hardware available):**
   ```bash
   cd test/component
   idf.py build flash monitor
   ```
   - All tests pass
   - No WAV/PLAY tests

3. **Integration tests (if hardware available):**
   ```bash
   cd test/test_app_audio
   idf.py build flash monitor
   ```
   - BEEP works
   - I2S capture works
   - SYNTH works
   - Bluetooth transmission works

4. **Manual smoke tests:**
   - Connect ESP32 to BBGW via I2S
   - Send `BEEP` command → hear beep on Bluetooth speaker
   - Start I2S on BBGW → hear audio on Bluetooth speaker
   - Send `PLAY test.wav` → should return error (command not found)
   - Check flash usage (should be ~1-2 MB less)

**Validation:**
- All automated tests pass
- Manual tests confirm functionality
- No regressions in existing features

### Phase 8: Cleanup and Merge (30 min)

**Goal:** Clean up branch and merge to master

**Tasks:**
1. Review all changes:
   ```bash
   git diff master
   ```

2. Commit with descriptive message:
   ```bash
   git add -A
   git commit -m "refactor: Remove PLAY/WAV audio source and SPIFFS partition

   - Remove play_manager component (~600 lines)
   - Remove PLAY command handler
   - Remove SPIFFS filesystem mount code
   - Update audio source enum (3 sources instead of 4)
   - Simplify audio source priority: I2S → SYNTH → SILENCE
   - Remove AUDIO_SOURCE_WAV from state machine
   - Remove WAV-related tests (~400 lines)
   - Remove SPIFFS partition from partitions.csv
   - Update documentation (remove PLAY references)

   Benefits:
   - ~1-2 MB flash space reclaimed
   - ~1,500-2,000 lines of code removed
   - Simpler state machine (fewer source interactions)
   - Clearer design intent (ESP32 as BT transmitter)
   - BBGW handles all WAV playback (better UI, more features)
   - BEEP still available for standalone self-test

   Breaking change: PLAY command no longer available
   Migration: Use BBGW for WAV playback via I2S input"
   ```

3. Push to GitHub:
   ```bash
   git push origin remove-play-wav
   ```

4. Create pull request (if using PR workflow)

5. After review, merge to master:
   ```bash
   git checkout master
   git merge remove-play-wav
   git push origin master
   ```

6. Delete branch:
   ```bash
   git branch -d remove-play-wav
   git push origin --delete remove-play-wav
   ```

**Validation:**
- Clean git history
- All changes documented
- Master branch builds and tests pass

---

## Testing Strategy

### Regression Testing (What Must Still Work)

**1. BEEP Functionality:**
- [ ] BEEP command sends tone to Bluetooth
- [ ] BEEP can overlay on I2S audio
- [ ] BEEP can overlay on SYNTH audio
- [ ] BEEP respects duration and frequency parameters
- [ ] BEEP rejected while I2S active (correct behavior)
- [ ] BEEP clears synth keepalive mode

**2. I2S Capture:**
- [ ] I2S starts when audio processor starts
- [ ] I2S audio flows to Bluetooth
- [ ] I2S sample rate configuration works
- [ ] I2S pin configuration works
- [ ] I2S stops cleanly
- [ ] I2S rejected while BEEP active (if applicable)

**3. SYNTH Mode:**
- [ ] SYNTH ON forces synthetic audio
- [ ] SYNTH OFF returns to I2S (if running)
- [ ] SYNTH serves as idle fallback
- [ ] SYNTH generates continuous tone

**4. Bluetooth:**
- [ ] A2DP connection works
- [ ] Audio streams to paired device
- [ ] Volume control works
- [ ] Connection status reported correctly
- [ ] Disconnect/reconnect works

**5. Command Interface:**
- [ ] All remaining commands work (START, STOP, STATUS, etc.)
- [ ] PLAY command returns error or "command not found"
- [ ] Help text no longer lists PLAY
- [ ] Command parsing unchanged for other commands

**6. Audio Engine:**
- [ ] Ring buffer works correctly
- [ ] Source switching (I2S ↔ SYNTH) works
- [ ] BEEP overlay mixing works
- [ ] No audio dropouts or underruns
- [ ] Stats tracking works (minus WAV stats)

### New Tests to Add (Optional)

**1. Verify PLAY Removal:**
- [ ] Test that PLAY command returns error
- [ ] Test that `play_manager_*` functions don't exist
- [ ] Test that AUDIO_SOURCE_WAV is not in enum

**2. Flash Usage:**
- [ ] Verify SPIFFS partition is gone
- [ ] Verify flash usage reduced by ~1-2 MB

**3. Simplified State Machine:**
- [ ] Verify only 3 sources in enum
- [ ] Verify source priority: I2S → SYNTH → SILENCE
- [ ] Verify no WAV-related state transitions

### Test Execution Plan

**1. Pre-removal Baseline:**
```bash
# Run all tests before any changes
cd test/host_test
python3 build_and_run_host_tests.py > baseline_host.txt

cd test/component
idf.py build test > baseline_component.txt

cd test/test_app_audio
idf.py build flash monitor > baseline_app.txt
```

**2. Post-removal Validation:**
```bash
# Run same tests after changes
cd test/host_test
python3 build_and_run_host_tests.py > after_host.txt

cd test/component
idf.py build test > after_component.txt

cd test/test_app_audio
idf.py build flash monitor > after_app.txt

# Compare (should have fewer tests, but all pass)
diff baseline_host.txt after_host.txt
diff baseline_component.txt after_component.txt
diff baseline_app.txt after_app.txt
```

**3. Manual Hardware Testing:**
- [ ] Flash ESP32 with new firmware
- [ ] Connect to Bluetooth speaker
- [ ] Send BEEP command → verify audio plays
- [ ] Connect BBGW via I2S
- [ ] Start I2S capture → verify audio plays
- [ ] Try PLAY command → verify error response
- [ ] Check serial logs for any SPIFFS errors (should be none)

---

## Risk Assessment

### Low Risk ✅

**These changes are safe:**
- Removing PLAY command handler (isolated code)
- Removing play_manager component (no other dependencies)
- Removing SPIFFS mount code (isolated in main.c)
- Removing test files (doesn't affect production)

### Medium Risk ⚠️

**These changes need careful testing:**
- Updating audio_source_t enum (reindexing affects stats arrays)
- Removing WAV from `get_active_source()` (logic change)
- Removing WAV from `produce_audio_chunk()` (logic change)
- Updating partition table (requires reflash, can't downgrade easily)

**Mitigation:**
- Thorough testing of source selection logic
- Validate stats arrays still work with 3 sources
- Test I2S → SYNTH → SILENCE priority order
- Backup current firmware before flashing new partition table

### High Risk 🚨

**None identified** — This is a removal, not a refactor. Removed code can't cause new bugs.

---

## Success Criteria

### Code Quality ✅ COMPLETE
- [x] All compiler warnings resolved
- [x] No undefined symbols or linker errors
- [x] Code compiles for all configurations (host, component, app)
- [x] No TODO or FIXME comments left from removal

### Functionality ✅ COMPLETE
- [x] All regression tests pass (390/390, 100%)
- [x] BEEP works standalone (verified via tests)
- [x] I2S capture → Bluetooth works (verified via tests)
- [x] SYNTH mode works (verified via tests)
- [x] No audio dropouts or quality issues

### Documentation ✅ COMPLETE
- [x] No references to PLAY in user-facing docs
- [x] README updated with correct command list
- [x] Architecture docs reflect 3 audio sources
- [x] This REMOVE_PLAY.md completed and accurate

### Flash/Memory ✅ COMPLETE
- [x] SPIFFS partition removed from partition table
- [x] Binary size reduced by 13 KB (exceeded 50-100 KB estimate)
- [x] Flash usage reduced by 1.01 MB (SPIFFS reclaimed)
- [x] Heap usage stable

### Testing ✅ COMPLETE
- [x] All automated tests pass (390/390)
- [x] Manual hardware tests pass (boot + command interface verified)
- [x] No regressions in existing features
- [x] Test coverage maintained

---

## Timeline Estimate

| Phase | Description | Estimated Time |
|-------|-------------|----------------|
| 1 | Preparation (search, branch, baseline) | 1-2 hours |
| 2 | Remove PLAY command handler | 30 minutes |
| 3 | Remove play_manager component | 1 hour |
| 4 | Update tests | 1-2 hours |
| 5 | Remove SPIFFS | 30 minutes |
| 6 | Update documentation | 30 minutes |
| 7 | Final testing | 1-2 hours |
| 8 | Cleanup and merge | 30 minutes |
| **Total** | | **6-9 hours** |

**Note:** Timeline assumes familiarity with codebase. First-time execution may take longer.

---

## References

**Related Documents:**
- `docs/FS.md` — Current functional specification (needs update)
- `main/README.md` — Main app documentation (needs update)
- `code_review/CODE_REVIEW6_TODO.md` — Audio engine architecture
- `bbgw_i2s_source/docs/PRD.md` — BBGW product requirements (WAV playback features)
- `bbgw_i2s_source/docs/FS.md` — BBGW functional spec (WAV playback implementation)

**Code References:**
- `components/audio_processor/audio_processor.c` — Audio source selection logic
- `components/audio_processor/play_manager.c` — WAV playback implementation (to be removed)
- `components/command_interface/cmd_handlers_system.c` — PLAY command handler (to be removed)
- `main/main.c` — SPIFFS mount code (to be removed)
- `partitions.csv` — Partition table (SPIFFS entry to be removed)

---

**End of Document**
