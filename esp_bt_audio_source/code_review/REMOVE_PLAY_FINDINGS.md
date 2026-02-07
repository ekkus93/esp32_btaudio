# PLAY/WAV Removal - Detailed Findings

**Date:** 2026-02-07  
**Phase:** 1.2 - Document Findings  
**Source:** Code search results from Phase 1.1

---

## 1. Files Containing `play_manager` References

### Production Code Files (10 files)

1. **components/command_interface/cmd_handlers_audio.c**
   - Multiple references to `play_manager_is_active()`
   - PLAY command handler implementation
   - Error responses for PLAY/BEEP conflicts
   
2. **components/command_interface/cmd_handlers_system.c**
   - References to `play_manager_status_t` and `play_manager_get_status()`
   - Used in DIAG/STATUS command

3. **components/command_interface/include/commands_priv.h**
   - `#include "play_manager.h"`

4. **components/audio_processor/audio_processor_beep.c**
   - PLAY busy check: `play_manager_is_active()`

5. **components/audio_processor/audio_processor.c**
   - `play_manager_is_active()` - source selection
   - `play_manager_init()` - initialization
   - `play_manager_deinit()` - cleanup
   - `play_manager_abort()` - abort playback
   - `play_manager_play_wav()` - start playback
   - `play_manager_buffers_t` - buffer management

6. **components/audio_processor/audio_processor_wav.c**
   - 4 references to `play_manager_is_active()`
   - `play_manager_abort()` call

7. **components/audio_processor/play_manager.c** ⚠️ **TO DELETE**
   - Entire implementation file (~400 lines)

8. **components/audio_processor/include/audio_processor_internal.h**
   - `#include "play_manager.h"`

9. **components/audio_processor/include/play_manager.h** ⚠️ **TO DELETE**
   - Header file with all play_manager declarations

10. **components/audio_processor/include/audio_util.h**
    - Comment reference to play_manager

### Test Files (3 files)

11. **test/test_app_audio/main/audio_processor_test.c**
    - Extensive use of `play_manager_*` functions
    - `play_manager_is_active()` - status checks
    - `play_manager_get_instrumentation()` - test validation
    - `play_manager_play_wav()` - test execution

12. **test/host_test/mocks/mock_audio_and_btstate.c**
    - Mock implementation of `play_manager_is_active()`
    - `play_manager_test_set_active()` - test helper

13. **test/host_test/test_commands.c**
    - `play_manager_test_set_active()` - test setup

**Total: 13 files with play_manager references**

---

## 2. Files Containing `PLAY` Command References

### Command Handler Files (4 files)

1. **components/command_interface/cmd_handlers_audio.c**
   - Main PLAY command handler function
   - Error responses:
     - `ERR|PLAY|MISSING_PARAM`
     - `ERR|PLAY|PATH_TOO_LONG`
     - `ERR|PLAY|SPIFFS_MOUNT_FAILED`
     - `ERR|PLAY|BUSY|BEEP_ACTIVE`
     - `ERR|PLAY|BUSY|WAV_ACTIVE`
     - `ERR|PLAY|A2DP_NOT_CONNECTED`
     - `OK|PLAY|ENQUEUED`
     - `OK|PLAY|MOCK_ENQUEUED`
     - `ERR|PLAY|MOCK_FAILED`

2. **components/command_interface/cmd_handlers_system.c**
   - Help text entry: `{"PLAY", "<FILENAME>", "Play a WAV file from /spiffs (host-mode)"}`

3. **components/command_interface/commands.c**
   - Command parsing: `strcasecmp(token, "PLAY")`
   - `CMD_TYPE_PLAY` assignment

4. **components/command_interface/include/command_interface.h**
   - `CMD_TYPE_PLAY` enum definition

### Test Files (4 files)

5. **test/test_app_audio/components/test_command_interface/test_command_interface.c**
   - Test parser PLAY implementation
   - A2DP connection check for PLAY

6. **test/test_app_audio/components/test_command_interface/include/command_interface.h**
   - `CMD_TYPE_PLAY` enum

7. **test/test_app_audio/main/audio_processor_test.c**
   - PLAY command test cases
   - Comments about PLAY functionality

8. **test/host_test/test_commands.c**
   - Extensive PLAY command tests
   - Test functions for PLAY scenarios

### Other Files (1 file)

9. **main/main.c**
   - Log message mentioning PLAY: `"Use PLAY/VOLUME commands to control audio"`

**Total: 9 files with PLAY command references**

---

## 3. Files Containing `AUDIO_SOURCE_WAV` References

### Production Code (2 files)

1. **components/audio_processor/audio_processor.c**
   - Enum definition: `AUDIO_SOURCE_WAV = 0,`
   - Source selection: `return AUDIO_SOURCE_WAV;`
   - Case statement: `case AUDIO_SOURCE_WAV:`

2. **components/audio_processor/include/audio_span_log.h**
   - Comment reference in logging macro

**Total: 2 files with AUDIO_SOURCE_WAV references**

---

## 4. Files Containing `.wav` File References

### Production Code (2 files)

1. **components/audio_processor/play_manager.c** ⚠️ **TO DELETE**
   - `s_pm.wav_channels` - 4 references
   - WAV file handling logic

2. **components/audio_processor/include/audio_processor.h**
   - Comment about WAV path: `/spiffs/worker_long_norm.wav`

### Test Files (5 files)

3. **test/test_spiffs_fail/main/spiffs_test.c**
   - `/spiffs/worker_long_norm.wav` references

4. **test/test_app_audio/main/test_main.c**
   - `/spiffs/worker_long_norm.wav` file checks

5. **test/test_app_audio/main/audio_processor_test.c**
   - `worker_long_norm.wav` (multiple references)
   - `test_441_1s.wav`
   - `test_48_downsample_1s.wav`
   - `test_48_baseline_1s.wav`
   - `does_not_exist.wav`

6. **test/host_test/test_commands.c**
   - `worker_long_norm.wav`
   - `missing_asset.wav`
   - `short.wav`
   - `missing.wav`

7. **test/component/test_audio_processor.c**
   - `does_not_exist.wav`
   - `missing.wav`
   - `dummy.wav`

**Total: 7 files with .wav references**

---

## 5. Test Files Requiring Modification

### Component Tests (1 file)

1. **test/component/test_audio_processor.c**
   - Remove PLAY rejection tests
   - Update audio source enum references
   - Remove WAV playback tests

### App Tests (3 files)

2. **test/test_app_audio/main/audio_processor_test.c**
   - Remove extensive PLAY/WAV tests (~300 lines)
   - Remove `test_play_rejected_while_i2s_running()`
   - Remove `test_play_busy_when_beep_active()`
   - Remove `test_drain_stops_play_manager_and_clears_queue()`
   - Update BEEP/I2S interaction tests

3. **test/test_app_audio/main/test_main.c**
   - Remove SPIFFS file checks for worker_long_norm.wav

4. **test/test_app_audio/components/test_command_interface/test_command_interface.c**
   - Remove PLAY command parser logic
   - Update command dispatch

5. **test/test_app_audio/components/test_command_interface/include/command_interface.h**
   - Remove `CMD_TYPE_PLAY` enum

### Host Tests (2 files)

6. **test/host_test/test_commands.c**
   - Remove PLAY command tests (~150 lines)
   - Remove `test_cmd_play_*` functions
   - Update help text validation

7. **test/host_test/mocks/mock_audio_and_btstate.c**
   - Remove `play_manager_test_set_active()`
   - Remove `play_manager_is_active()` stub

### SPIFFS Tests (1 directory)

8. **test/test_spiffs_fail/** ⚠️ **CHECK IF PLAY-ONLY**
   - May need to be deleted if purely PLAY-related

**Total: 7 test files + 1 directory requiring modification**

---

## 6. Additional Files Not in REMOVE_PLAY.md

### Newly Discovered Production Files

1. **components/audio_processor/audio_processor_wav.c**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains 4 `play_manager_is_active()` calls
   - Contains `play_manager_abort()` call
   - **Decision needed:** Delete or modify?

2. **components/command_interface/include/commands_priv.h**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains `#include "play_manager.h"`
   - **Action:** Remove include

3. **components/audio_processor/include/audio_processor_internal.h**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains `#include "play_manager.h"`
   - **Action:** Remove include

4. **components/audio_processor/include/audio_util.h**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains comment reference to play_manager
   - **Action:** Update comment

### Newly Discovered Test Files

5. **test/test_app_audio/components/test_command_interface/test_command_interface.c**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains PLAY command parser logic
   - **Action:** Remove PLAY handling

6. **test/test_app_audio/components/test_command_interface/include/command_interface.h**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains `CMD_TYPE_PLAY` enum
   - **Action:** Remove enum entry

7. **test/test_app_audio/main/test_main.c**
   - Not explicitly listed in REMOVE_PLAY.md
   - Contains SPIFFS/WAV file checks
   - **Action:** Remove SPIFFS checks

8. **test/test_spiffs_fail/main/spiffs_test.c**
   - Not explicitly listed in REMOVE_PLAY.md
   - May be SPIFFS-only test
   - **Action:** Investigate and potentially delete directory

---

## 7. Summary Statistics

### Files to Delete
- `components/audio_processor/play_manager.c` (~400 lines)
- `components/audio_processor/play_manager.h` (~50 lines)
- `components/audio_processor/include/play_manager.h` (~150 lines)
- `test/test_play_manager/` (if exists - need to check)
- `test/test_spiffs_fail/` (if PLAY-only - need to check)
- `spiffs/` directory (WAV test files)
- **Potential:** `components/audio_processor/audio_processor_wav.c` (if WAV-only)

### Files to Modify (Production)
- `components/command_interface/cmd_handlers_audio.c` (remove PLAY handler)
- `components/command_interface/cmd_handlers_system.c` (remove help entry)
- `components/command_interface/commands.c` (remove PLAY parsing)
- `components/command_interface/include/command_interface.h` (remove CMD_TYPE_PLAY)
- `components/command_interface/include/commands_priv.h` (remove include)
- `components/audio_processor/audio_processor.c` (remove WAV source, enum, stats)
- `components/audio_processor/audio_processor_beep.c` (remove PLAY check)
- `components/audio_processor/include/audio_processor.h` (remove WAV functions)
- `components/audio_processor/include/audio_processor_internal.h` (remove include)
- `components/audio_processor/include/audio_span_log.h` (update comment)
- `components/audio_processor/include/audio_util.h` (update comment)
- `components/audio_processor/CMakeLists.txt` (remove play_manager.c)
- `main/main.c` (remove SPIFFS mount, update log message)
- `partitions.csv` (remove SPIFFS partition)

**Total Production Files to Modify:** 14 files

### Files to Modify (Tests)
- `test/component/test_audio_processor.c`
- `test/test_app_audio/main/audio_processor_test.c`
- `test/test_app_audio/main/test_main.c`
- `test/test_app_audio/components/test_command_interface/test_command_interface.c`
- `test/test_app_audio/components/test_command_interface/include/command_interface.h`
- `test/host_test/test_commands.c`
- `test/host_test/mocks/mock_audio_and_btstate.c`

**Total Test Files to Modify:** 7 files

### Overall Impact
- **Files to delete:** 3 confirmed + 2-3 potential (directories/optional)
- **Files to modify:** 21 files
- **Estimated lines removed:** ~1,500-2,000 lines
- **SPIFFS partition reclaimed:** ~1-2 MB flash

---

## 8. Files Requiring Investigation ✅ COMPLETED

### 1. audio_processor_wav.c ✅ INVESTIGATED
**Location:** `components/audio_processor/audio_processor_wav.c`  
**Status:** Not in original REMOVE_PLAY.md file list  
**References:**
- 4 calls to `play_manager_is_active()`
- 1 call to `play_manager_abort()`
- Variable: `s_wav_playback_active` - tracks WAV playback state

**Investigation Result:**
- File contains WAV playback state management functions
- Functions: `wav_playback_is_active()`, `wav_playback_begin()`, `wav_playback_consume()`, `wav_playback_abort()`
- Tightly coupled to play_manager
- **DECISION: DELETE THIS FILE** - It's purely for SPIFFS WAV playback support

### 2. test_spiffs_fail/ Directory ✅ INVESTIGATED
**Location:** `test/test_spiffs_fail/`  
**Status:** Not in original REMOVE_PLAY.md file list  
**Contains:** 
- `spiffs_test.c` - Manual SPIFFS mount/read smoke test
- Tests opening `/spiffs/worker_long_norm.wav`
- Purpose: "Manual SPIFFS mount/read smoke for the spiffs_fail test app"

**Investigation Result:**
- This is a SPIFFS functionality test, not a PLAY command test
- Tests basic SPIFFS mount/unmount and file reading
- However, it references WAV files from PLAY feature
- **DECISION: DELETE THIS DIRECTORY** - Without SPIFFS partition, this test is no longer relevant

### 3. test/test_play_manager/ Directory ✅ INVESTIGATED
**Location:** `test/test_play_manager/`  
**Status:** Assumed to exist in REMOVE_PLAY.md but not verified

**Investigation Result:**
- **Directory does NOT exist** - No action needed

---

## 9. Recommendations for REMOVE_PLAY.md Updates ✅ FINALIZED

### Files to ADD to "Files to Delete" Section

1. **components/audio_processor/audio_processor_wav.c** (~144 lines)
   - WAV playback state management
   - Tightly coupled to play_manager
   - Functions: wav_playback_is_active(), wav_playback_begin(), wav_playback_consume(), wav_playback_abort()

2. **test/test_spiffs_fail/** (entire directory)
   - SPIFFS mount/unmount smoke test
   - No longer relevant without SPIFFS partition
   - Contains references to worker_long_norm.wav

### Files to ADD to "Files to Modify" Section

**Production:**
- `components/command_interface/include/commands_priv.h` - Remove play_manager.h include
- `components/audio_processor/include/audio_processor_internal.h` - Remove play_manager.h include
- `components/audio_processor/include/audio_util.h` - Update comment referencing play_manager

**Tests:**
- `test/test_app_audio/main/test_main.c` - Remove SPIFFS file existence checks
- `test/test_app_audio/components/test_command_interface/test_command_interface.c` - Remove PLAY command handling
- `test/test_app_audio/components/test_command_interface/include/command_interface.h` - Remove CMD_TYPE_PLAY enum

### Files to REMOVE from "Files to Delete" Section

- **test/test_play_manager/** - This directory does NOT exist, remove from list

---

## 10. Final Confirmed File Lists

### ✅ CONFIRMED - Files to Delete (8 items)

1. `components/audio_processor/play_manager.c` (~400 lines)
2. `components/audio_processor/play_manager.h` (~50 lines)
3. `components/audio_processor/include/play_manager.h` (~150 lines)
4. `components/audio_processor/audio_processor_wav.c` (~144 lines) ⭐ NEW
5. `test/test_spiffs_fail/` (entire directory) ⭐ NEW
6. `spiffs/` directory (WAV test files)

**Total to delete:** 6 files/directories, ~750+ lines of production code

### ✅ CONFIRMED - Production Files to Modify (14 files)

1. `components/command_interface/cmd_handlers_audio.c` - Remove PLAY command handler
2. `components/command_interface/cmd_handlers_system.c` - Remove PLAY help text
3. `components/command_interface/commands.c` - Remove PLAY command parsing
4. `components/command_interface/include/command_interface.h` - Remove CMD_TYPE_PLAY enum
5. `components/command_interface/include/commands_priv.h` - Remove play_manager.h include ⭐ NEW
6. `components/audio_processor/audio_processor.c` - Remove WAV source, enum, stats, play_manager calls
7. `components/audio_processor/audio_processor_beep.c` - Remove PLAY busy check
8. `components/audio_processor/include/audio_processor.h` - Remove WAV function declarations
9. `components/audio_processor/include/audio_processor_internal.h` - Remove play_manager.h include ⭐ NEW
10. `components/audio_processor/include/audio_span_log.h` - Update AUDIO_SOURCE_WAV comment
11. `components/audio_processor/include/audio_util.h` - Update play_manager comment ⭐ NEW
12. `components/audio_processor/CMakeLists.txt` - Remove play_manager.c and audio_processor_wav.c from SRCS
13. `main/main.c` - Remove SPIFFS mount code, update PLAY log message
14. `partitions.csv` - Remove SPIFFS partition line

### ✅ CONFIRMED - Test Files to Modify (7 files)

1. `test/component/test_audio_processor.c` - Remove PLAY tests, update enum references
2. `test/test_app_audio/main/audio_processor_test.c` - Remove extensive PLAY tests (~300 lines)
3. `test/test_app_audio/main/test_main.c` - Remove SPIFFS file checks ⭐ NEW
4. `test/test_app_audio/components/test_command_interface/test_command_interface.c` - Remove PLAY handling ⭐ NEW
5. `test/test_app_audio/components/test_command_interface/include/command_interface.h` - Remove CMD_TYPE_PLAY ⭐ NEW
6. `test/host_test/test_commands.c` - Remove PLAY command tests (~150 lines)
7. `test/host_test/mocks/mock_audio_and_btstate.c` - Remove play_manager mocks

### 📊 Updated Impact Summary

- **Files to delete:** 6 confirmed (was 3-5)
- **Production files to modify:** 14 files (was ~15-20)
- **Test files to modify:** 7 files (was ~7-10)
- **Total files affected:** 27 files
- **Estimated lines removed:** ~1,700-2,200 lines (increased from 1,500-2,000)
- **SPIFFS partition reclaimed:** ~1-2 MB flash
- **Additional deletions:**
  - audio_processor_wav.c (~144 lines)
  - test_spiffs_fail/ directory

---

## 11. Next Steps for Phase 1.3 (Git Setup) ✅ READY

All investigations complete. Ready to proceed with:

1. ~~Investigate audio_processor_wav.c~~ ✅ DONE - Confirmed for deletion
2. ~~Check for test_play_manager/ directory~~ ✅ DONE - Does not exist
3. ~~Check test_spiffs_fail/ directory~~ ✅ DONE - Confirmed for deletion
4. **Update REMOVE_PLAY.md** with new findings (optional before branch creation)
5. **Create git branch** `remove-play-wav`

---

**End of Findings Document**
