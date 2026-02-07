# REMOVE_PLAY TODO List

**Date:** 2026-02-07  
**Status:** Not Started  
**Estimated Time:** 6-9 hours  
**Reference:** [REMOVE_PLAY.md](REMOVE_PLAY.md)

---

## Overview

This TODO list tracks the removal of WAV playback (PLAY command) and SPIFFS partition from the ESP32 Bluetooth audio source project. The work is organized into 8 phases as outlined in REMOVE_PLAY.md.

**Quick Status:**
- [ ] Phase 1: Preparation (1-2 hours)
- [ ] Phase 2: Remove PLAY Command Handler (30 min)
- [ ] Phase 3: Remove play_manager Component (1 hour)
- [ ] Phase 4: Update Tests (1-2 hours)
- [ ] Phase 5: Remove SPIFFS (30 min)
- [ ] Phase 6: Update Documentation (30 min)
- [ ] Phase 7: Final Testing (1-2 hours)
- [ ] Phase 8: Cleanup and Merge (30 min)

---

## Phase 1: Preparation (1-2 hours)

**Goal:** Understand full scope, backup code, create branch

### 1.1 Search for References
- [x] Search for `play_manager` references in C/H files
  ```bash
  cd esp_bt_audio_source
  grep -r "play_manager" --include="*.c" --include="*.h"
  ```
- [x] Search for `PLAY` command references
  ```bash
  grep -r "PLAY" --include="*.c" --include="*.h" | grep -i command
  ```
- [x] Search for `.wav` file references
  ```bash
  grep -r "\.wav" --include="*.c" --include="*.h"
  ```
- [x] Search for `spiffs` references
  ```bash
  grep -r "spiffs" --include="*.c" --include="*.h"
  ```
- [x] Search for `AUDIO_SOURCE_WAV` references
  ```bash
  grep -r "AUDIO_SOURCE_WAV" --include="*.c" --include="*.h"
  ```

### 1.2 Document Findings
- [x] Create list of all files containing `play_manager` references
- [x] Create list of all files containing `PLAY` command references
- [x] Create list of all files containing `AUDIO_SOURCE_WAV` references
- [x] Create list of all test files that need modification
- [x] Update REMOVE_PLAY.md with any additional files discovered

### 1.3 Git Setup
- [x] ~~Ensure current branch is clean (commit or stash any changes)~~ SKIPPED - working on master
- [x] ~~Create new branch: `git checkout -b remove-play-wav`~~ SKIPPED - working on master
- [x] ~~Verify branch created: `git branch --show-current`~~ SKIPPED - working on master

### 1.4 Establish Baseline Testing
- [x] Run host tests (baseline):
  ```bash
  cd test/host_test
  make test > baseline_host.txt
  ```
- [x] Save baseline host test results
- [x] Record number of passing tests in baseline (33/33 tests passed)
- [ ] (Optional) Run component tests if hardware available:
  ```bash
  cd test/component
  idf.py build test > baseline_component.txt
  ```
- [ ] (Optional) Run app tests if hardware available:
  ```bash
  cd test/test_app_audio
  idf.py build flash monitor > baseline_app.txt
  ```

### 1.5 Verification
- [x] All baseline test results saved (baseline_host.txt)
- [x] ~~Branch `remove-play-wav` created and checked out~~ SKIPPED - working on master
- [x] List of all files to modify documented (REMOVE_PLAY_FINDINGS.md)
- [x] Ready to proceed with Phase 2

---

## Phase 2: Remove PLAY Command Handler (30 min)

**Goal:** Remove user-facing PLAY command

### 2.1 Modify cmd_handlers_system.c
- [ ] Open `components/command_interface/cmd_handlers_system.c`
- [ ] Locate `cmd_handle_play()` function
- [ ] Remove entire `cmd_handle_play()` function (~50-100 lines)
- [ ] Locate command help text array
- [ ] Remove PLAY entry from help text: `{"PLAY", "<FILENAME>", "Play a WAV file from /spiffs (host-mode)"}`
- [ ] Locate command dispatch table
- [ ] Remove PLAY entry from dispatch table (if separate from help)
- [ ] Save file

### 2.2 Update Test Files
- [ ] Open `test/host_test/test_commands.c`
- [ ] Locate all PLAY command tests
- [ ] Remove test functions:
  - [ ] Remove `test_cmd_play_*` functions (if any)
  - [ ] Remove any PLAY-related command parsing tests
  - [ ] Remove PLAY from help text validation tests
- [ ] Save file

### 2.3 Build and Test
- [ ] Build host tests:
  ```bash
  cd test/host_test
  python3 build_and_run_host_tests.py
  ```
- [ ] Verify all tests pass (fewer tests expected)
- [ ] Verify PLAY command no longer in help output
- [ ] Check for any compilation errors or warnings

### 2.4 Verification
- [ ] PLAY command handler removed from cmd_handlers_system.c
- [ ] PLAY tests removed from test_commands.c
- [ ] Host tests pass
- [ ] No new compiler warnings introduced
- [ ] Ready to proceed with Phase 3

---

## Phase 3: Remove play_manager Component (1 hour)

**Goal:** Remove play_manager.c and all references

### 3.1 Remove play_manager Files
- [ ] Delete `components/audio_processor/play_manager.c`
- [ ] Delete `components/audio_processor/play_manager.h`
- [ ] Delete `components/audio_processor/include/play_manager.h`
- [ ] Verify files deleted: `git status`

### 3.2 Update CMakeLists.txt
- [ ] Open `components/audio_processor/CMakeLists.txt`
- [ ] Locate SRCS list
- [ ] Remove `play_manager.c` from SRCS
- [ ] Save file

### 3.3 Modify audio_processor.c
- [ ] Open `components/audio_processor/audio_processor.c`
- [ ] Remove `#include "play_manager.h"` from includes
- [ ] Locate `audio_source_t` enum definition
- [ ] Change enum to remove AUDIO_SOURCE_WAV:
  ```c
  // BEFORE:
  typedef enum {
      AUDIO_SOURCE_WAV = 0,
      AUDIO_SOURCE_I2S,
      AUDIO_SOURCE_SYNTH,
      AUDIO_SOURCE_SILENCE,
      NUM_AUDIO_SOURCES
  } audio_source_t;
  
  // AFTER:
  typedef enum {
      AUDIO_SOURCE_I2S = 0,
      AUDIO_SOURCE_SYNTH,
      AUDIO_SOURCE_SILENCE,
      NUM_AUDIO_SOURCES
  } audio_source_t;
  ```
- [ ] Locate `get_active_source()` function
- [ ] Remove WAV check: Delete `if (play_manager_is_active()) return AUDIO_SOURCE_WAV;`
- [ ] Update logic to prioritize I2S → SYNTH → SILENCE
- [ ] Locate `produce_audio_chunk()` function
- [ ] Remove WAV source handling from source array or switch statement
- [ ] Locate audio stats tracking code
- [ ] Update stats arrays to use 3 sources instead of 4
- [ ] Remove WAV stats tracking (e.g., `stats.bytes_by_source[0]` references)
- [ ] Search for any other `play_manager_` function calls and remove
- [ ] Save file

### 3.4 Modify audio_processor.h
- [ ] Open `components/audio_processor/include/audio_processor.h`
- [ ] Remove `audio_processor_play_wav()` declaration
- [ ] Remove `audio_processor_is_wav_active()` declaration
- [ ] Remove `audio_processor_get_work_buffer_bytes()` declaration (if WAV-only)
- [ ] Save file

### 3.5 Modify audio_processor_beep.c
- [ ] Open `components/audio_processor/audio_processor_beep.c`
- [ ] Locate PLAY busy check:
  ```c
  if (play_manager_is_active()) {
      ESP_LOGW(TAG, "audio_processor_beep: busy (play active)");
      return ESP_ERR_INVALID_STATE;
  }
  ```
- [ ] Remove the entire PLAY busy check
- [ ] Save file

### 3.6 Update Test Mocks
- [ ] Open `test/host_test/mocks/audio_processor_host_stub.c`
- [ ] Remove `audio_processor_play_wav()` stub function
- [ ] Remove `audio_processor_is_wav_active()` stub function
- [ ] Remove `audio_processor_get_work_buffer_bytes()` stub function (if present)
- [ ] Save file

### 3.7 Build and Test
- [ ] Build host tests:
  ```bash
  cd test/host_test
  python3 build_and_run_host_tests.py
  ```
- [ ] Verify no unresolved symbol errors
- [ ] Verify no compilation errors
- [ ] Check for new warnings and resolve if any

### 3.8 Verification
- [ ] play_manager files deleted
- [ ] CMakeLists.txt updated (play_manager.c removed)
- [ ] audio_processor.c updated (WAV source removed, enum updated)
- [ ] audio_processor.h updated (WAV functions removed)
- [ ] audio_processor_beep.c updated (PLAY check removed)
- [ ] Test mocks updated
- [ ] Code compiles without errors
- [ ] Host tests pass
- [ ] Ready to proceed with Phase 4

---

## Phase 4: Update Tests (1-2 hours)

**Goal:** Remove/update all PLAY-related tests

### 4.1 Remove Test Directories
- [ ] Check if `test/test_play_manager/` directory exists
- [ ] If exists, delete entire directory: `rm -rf test/test_play_manager/`
- [ ] Verify deletion: `git status`

### 4.2 Update test/component/test_audio_processor.c
- [ ] Open `test/component/test_audio_processor.c`
- [ ] Search for all `test_audio_processor_play_` functions
- [ ] Remove each PLAY-related test function:
  - [ ] Remove `test_audio_processor_play_*` functions
  - [ ] Remove any `play_manager_` test calls
  - [ ] Remove WAV playback assertions
- [ ] Update audio source enum references:
  - [ ] Change `AUDIO_SOURCE_WAV` to appropriate alternative (or remove)
  - [ ] Update index 0 from WAV to I2S where applicable
  - [ ] Update index 1 from I2S to SYNTH where applicable
  - [ ] Update index 2 from SYNTH to SILENCE where applicable
- [ ] Remove PLAY rejection tests (PLAY vs I2S, PLAY vs BEEP)
- [ ] Save file

### 4.3 Update test/test_app_audio/main/audio_processor_test.c
- [ ] Open `test/test_app_audio/main/audio_processor_test.c`
- [ ] Search for PLAY-related test functions
- [ ] Remove test functions:
  - [ ] Remove `test_play_rejected_while_i2s_running()`
  - [ ] Remove `test_play_busy_when_beep_active()`
  - [ ] Remove any WAV playback integration tests
  - [ ] Remove PLAY rejection tests (~10-15 test cases)
- [ ] Update BEEP/I2S interaction tests:
  - [ ] Remove references to PLAY conflicts
  - [ ] Update source selection assertions
- [ ] Update audio source enum references (0→I2S, 1→SYNTH, 2→SILENCE)
- [ ] Save file

### 4.4 Update test/host_test/test_commands.c (additional cleanup)
- [ ] Open `test/host_test/test_commands.c` (if not already done in Phase 2)
- [ ] Search for any remaining PLAY references
- [ ] Remove any missed PLAY command tests
- [ ] Update command help tests to not expect PLAY
- [ ] Save file

### 4.5 Update Other Test Files
- [ ] Search for other test files with PLAY references:
  ```bash
  grep -r "play_manager" test/ --include="*.c" --include="*.h"
  grep -r "AUDIO_SOURCE_WAV" test/ --include="*.c" --include="*.h"
  ```
- [ ] For each file found, remove PLAY/WAV references
- [ ] Update audio source enum references in all test files

### 4.6 Rebuild Test Executables
- [ ] Build host tests:
  ```bash
  cd test/host_test
  python3 build_and_run_host_tests.py
  ```
- [ ] Verify all tests pass
- [ ] (Optional) Build component tests:
  ```bash
  cd test/component
  idf.py build
  ```
- [ ] (Optional) Build app tests:
  ```bash
  cd test/test_app_audio
  idf.py build
  ```

### 4.7 Verification
- [ ] test_play_manager/ directory removed (if existed)
- [ ] test/component/test_audio_processor.c updated (PLAY tests removed)
- [ ] test/test_app_audio/main/audio_processor_test.c updated (PLAY tests removed)
- [ ] test/host_test/test_commands.c fully updated
- [ ] All test files updated with new audio source enum indices
- [ ] All unit tests pass
- [ ] No references to PLAY in test output
- [ ] Ready to proceed with Phase 5

---

## Phase 5: Remove SPIFFS (30 min)

**Goal:** Remove SPIFFS filesystem and partition

### 5.1 Update main/main.c
- [ ] Open `main/main.c`
- [ ] Locate `#include "esp_spiffs.h"` and remove it
- [ ] Search for SPIFFS mount code:
  ```c
  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      // ... rest of config
  };
  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  ```
- [ ] Remove entire SPIFFS mount block
- [ ] Remove SPIFFS error handling code
- [ ] Remove any SPIFFS unmount code (if in cleanup/shutdown)
- [ ] Save file

### 5.2 Update partitions.csv
- [ ] Open `partitions.csv`
- [ ] Locate SPIFFS partition line:
  ```csv
  spiffs,   data, spiffs,  ,        1M,
  ```
- [ ] Remove the entire SPIFFS line
- [ ] (Optional) Increase app or OTA partition sizes to use reclaimed space
- [ ] Add comment noting space reclaimed from SPIFFS
- [ ] Save file

### 5.3 Remove SPIFFS Directory
- [ ] Check if `spiffs/` directory exists: `ls -la spiffs/`
- [ ] Remove directory and all contents:
  ```bash
  rm -rf spiffs/
  ```
- [ ] Verify deletion: `git status`

### 5.4 Update CMakeLists.txt (if needed)
- [ ] Open top-level `CMakeLists.txt`
- [ ] Check if SPIFFS component explicitly listed in REQUIRES
- [ ] If found, remove SPIFFS from REQUIRES
- [ ] Save file (if modified)

### 5.5 Build with New Partition Table
- [ ] Clean build directory:
  ```bash
  cd esp_bt_audio_source
  rm -rf build
  ```
- [ ] Build with new partition table:
  ```bash
  . $HOME/esp/esp-idf/export.sh
  idf.py build
  ```
- [ ] Verify build succeeds
- [ ] Check partition table output in build log

### 5.6 Flash and Test (if hardware available)
- [ ] Flash new firmware with partition table:
  ```bash
  idf.py flash
  ```
- [ ] Monitor boot:
  ```bash
  idf.py monitor
  ```
- [ ] Verify ESP32 boots without SPIFFS mount errors
- [ ] Verify no SPIFFS-related error messages in serial output

### 5.7 Verification
- [ ] main/main.c updated (SPIFFS mount code removed)
- [ ] partitions.csv updated (SPIFFS partition removed)
- [ ] spiffs/ directory deleted
- [ ] Build succeeds with new partition table
- [ ] (If flashed) ESP32 boots without SPIFFS errors
- [ ] Ready to proceed with Phase 6

---

## Phase 6: Update Documentation (30 min)

**Goal:** Remove PLAY references from all documentation

### 6.1 Update main/README.md
- [ ] Open `main/README.md`
- [ ] Search for `PLAY` command mentions
- [ ] Remove PLAY command description from command list
- [ ] Remove WAV playback use cases
- [ ] Update audio source list (remove WAV, show 3 sources)
- [ ] Update any diagrams or flowcharts showing PLAY
- [ ] Save file

### 6.2 Update docs/FS.md
- [ ] Open `docs/FS.md` (if exists)
- [ ] Search for `AUDIO_SOURCE_WAV` mentions
- [ ] Remove AUDIO_SOURCE_WAV from architecture descriptions
- [ ] Update source priority list (I2S → SYNTH → SILENCE)
- [ ] Remove WAV-related sections
- [ ] Update any state machine diagrams
- [ ] Save file

### 6.3 Update Root README.md
- [ ] Open root `README.md`
- [ ] Search for PLAY command in command list
- [ ] Remove PLAY from available commands
- [ ] Update feature list (remove WAV playback)
- [ ] Update architecture description (3 sources not 4)
- [ ] Save file

### 6.4 Search for Other Documentation
- [ ] Search for PLAY references in all markdown files:
  ```bash
  grep -r "PLAY" --include="*.md"
  ```
- [ ] For each file found, review and remove PLAY references
- [ ] Search for play_manager references:
  ```bash
  grep -r "play_manager" --include="*.md"
  ```
- [ ] Search for WAV references:
  ```bash
  grep -r "\.wav" --include="*.md" --include="*.rst" --include="*.txt"
  ```
- [ ] Update any architecture diagrams or flowcharts

### 6.5 Update Comments in Code
- [ ] Search for PLAY-related comments in remaining code:
  ```bash
  grep -r "PLAY\|play_manager\|WAV" --include="*.c" --include="*.h"
  ```
- [ ] Review each finding
- [ ] Remove or update outdated comments
- [ ] Update function documentation that mentions PLAY

### 6.6 Final Documentation Check
- [ ] Verify no PLAY references in docs:
  ```bash
  grep -ri "PLAY" --include="*.md" | grep -v "REMOVE_PLAY"
  ```
- [ ] Verify no play_manager references:
  ```bash
  grep -ri "play_manager" --include="*.md"
  ```
- [ ] Verify no misleading WAV references:
  ```bash
  grep -ri "\.wav" --include="*.md"
  ```

### 6.7 Verification
- [ ] main/README.md updated (PLAY removed)
- [ ] docs/FS.md updated (WAV source removed)
- [ ] Root README.md updated (PLAY removed)
- [ ] All markdown files checked and updated
- [ ] Code comments updated
- [ ] No misleading PLAY/WAV references remain
- [ ] Ready to proceed with Phase 7

---

## Phase 7: Final Testing (1-2 hours)

**Goal:** Comprehensive testing of modified system

### 7.1 Host Tests
- [ ] Run full host test suite:
  ```bash
  cd test/host_test
  python3 build_and_run_host_tests.py > after_host.txt
  ```
- [ ] Verify all tests pass
- [ ] Compare with baseline:
  ```bash
  diff baseline_host.txt after_host.txt
  ```
- [ ] Verify test count decreased (PLAY tests removed)
- [ ] Verify no PLAY-related tests executed
- [ ] Check for any unexpected failures

### 7.2 Component Tests (if hardware available)
- [ ] Build component tests:
  ```bash
  cd test/component
  idf.py build
  ```
- [ ] Flash and run component tests:
  ```bash
  idf.py flash monitor
  ```
- [ ] Verify all tests pass
- [ ] Verify no WAV/PLAY tests
- [ ] Save test results to file

### 7.3 Integration Tests (if hardware available)
- [ ] Build integration tests:
  ```bash
  cd test/test_app_audio
  idf.py build
  ```
- [ ] Flash and run integration tests:
  ```bash
  idf.py flash monitor
  ```
- [ ] Verify tests pass:
  - [ ] BEEP tests pass
  - [ ] I2S capture tests pass
  - [ ] SYNTH tests pass
  - [ ] Bluetooth transmission tests pass
- [ ] Save test results to file

### 7.4 Manual Smoke Tests (if hardware available)
- [ ] Flash ESP32 with new firmware
- [ ] Connect ESP32 to Bluetooth speaker
- [ ] Test BEEP command:
  - [ ] Send `BEEP` via serial
  - [ ] Verify beep plays on Bluetooth speaker
  - [ ] Verify beep duration correct (~10 seconds)
- [ ] Test PLAY command rejection:
  - [ ] Send `PLAY test.wav` via serial
  - [ ] Verify error response (command not found or invalid)
- [ ] Test I2S capture (if BBGW or I2S source available):
  - [ ] Connect ESP32 to I2S source
  - [ ] Start I2S capture
  - [ ] Verify audio plays on Bluetooth speaker
  - [ ] Verify audio quality acceptable
- [ ] Test SYNTH mode:
  - [ ] Send `SYNTH ON` via serial
  - [ ] Verify synthetic tone plays on Bluetooth speaker
  - [ ] Send `SYNTH OFF` via serial
  - [ ] Verify synth stops (returns to I2S or silence)
- [ ] Check serial logs:
  - [ ] Verify no SPIFFS mount errors
  - [ ] Verify no SPIFFS-related warnings
  - [ ] Verify no play_manager errors

### 7.5 Flash Usage Check
- [ ] Check partition sizes in build output
- [ ] Verify SPIFFS partition not present
- [ ] Compare binary size with baseline:
  - [ ] Check app binary size
  - [ ] Verify ~50-100 KB reduction
- [ ] Check flash usage:
  ```bash
  idf.py size
  ```
- [ ] Verify ~1-2 MB flash space reclaimed

### 7.6 Regression Testing Checklist
Complete all items from REMOVE_PLAY.md Testing Strategy:

**BEEP Functionality:**
- [ ] BEEP command sends tone to Bluetooth
- [ ] BEEP can overlay on I2S audio
- [ ] BEEP can overlay on SYNTH audio
- [ ] BEEP respects duration and frequency parameters
- [ ] BEEP rejected while I2S active (if applicable)
- [ ] BEEP clears synth keepalive mode

**I2S Capture:**
- [ ] I2S starts when audio processor starts
- [ ] I2S audio flows to Bluetooth
- [ ] I2S sample rate configuration works
- [ ] I2S pin configuration works
- [ ] I2S stops cleanly

**SYNTH Mode:**
- [ ] SYNTH ON forces synthetic audio
- [ ] SYNTH OFF returns to I2S (if running)
- [ ] SYNTH serves as idle fallback
- [ ] SYNTH generates continuous tone

**Bluetooth:**
- [ ] A2DP connection works
- [ ] Audio streams to paired device
- [ ] Volume control works
- [ ] Connection status reported correctly
- [ ] Disconnect/reconnect works

**Command Interface:**
- [ ] All remaining commands work (START, STOP, STATUS, etc.)
- [ ] PLAY command returns error or "command not found"
- [ ] Help text no longer lists PLAY
- [ ] Command parsing unchanged for other commands

**Audio Engine:**
- [ ] Ring buffer works correctly
- [ ] Source switching (I2S ↔ SYNTH) works
- [ ] BEEP overlay mixing works
- [ ] No audio dropouts or underruns
- [ ] Stats tracking works (minus WAV stats)

### 7.7 Verification
- [ ] All automated tests pass
- [ ] Manual smoke tests complete (if hardware available)
- [ ] No regressions in existing features
- [ ] Flash usage reduced as expected
- [ ] No SPIFFS-related errors
- [ ] PLAY command properly rejected
- [ ] Ready to proceed with Phase 8

---

## Phase 8: Cleanup and Merge (30 min)

**Goal:** Clean up branch and merge to master

### 8.1 Review Changes
- [ ] Review all file changes:
  ```bash
  git status
  ```
- [ ] Review detailed diff:
  ```bash
  git diff master
  ```
- [ ] Verify only intended files modified
- [ ] Check for any unintended changes
- [ ] Verify deleted files shown as deleted in git status

### 8.2 Final Code Quality Check
- [ ] Run clang-format (if configured):
  ```bash
  find . -name "*.c" -o -name "*.h" | xargs clang-format -i
  ```
- [ ] Check for compiler warnings:
  ```bash
  idf.py build | grep -i warning
  ```
- [ ] Resolve any warnings introduced by changes
- [ ] Search for TODO/FIXME comments from removal:
  ```bash
  grep -r "TODO\|FIXME" --include="*.c" --include="*.h"
  ```
- [ ] Resolve or document any TODOs

### 8.3 Commit Changes
- [ ] Stage all changes:
  ```bash
  git add -A
  ```
- [ ] Verify staged changes:
  ```bash
  git status
  ```
- [ ] Commit with descriptive message:
  ```bash
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

### 8.4 Push to GitHub
- [ ] Push branch to GitHub:
  ```bash
  git push origin remove-play-wav
  ```
- [ ] Verify push successful
- [ ] Check GitHub for branch

### 8.5 Create Pull Request (Optional)
- [ ] Navigate to GitHub repository
- [ ] Create pull request from `remove-play-wav` to `master`
- [ ] Fill in PR description with:
  - [ ] Summary of changes
  - [ ] Benefits of removal
  - [ ] Breaking changes
  - [ ] Testing performed
  - [ ] Link to REMOVE_PLAY.md
- [ ] Request review (if applicable)
- [ ] Wait for review approval

### 8.6 Merge to Master
- [ ] (If using PR) Merge pull request on GitHub
- [ ] (If direct merge) Switch to master and merge:
  ```bash
  git checkout master
  git merge remove-play-wav
  ```
- [ ] Verify merge successful
- [ ] Push master to GitHub:
  ```bash
  git push origin master
  ```

### 8.7 Post-Merge Validation
- [ ] Build from master:
  ```bash
  git checkout master
  idf.py fullclean
  idf.py build
  ```
- [ ] Run tests from master:
  ```bash
  cd test/host_test
  python3 build_and_run_host_tests.py
  ```
- [ ] Verify all tests pass on master

### 8.8 Cleanup Branch
- [ ] Delete local branch:
  ```bash
  git branch -d remove-play-wav
  ```
- [ ] Delete remote branch:
  ```bash
  git push origin --delete remove-play-wav
  ```
- [ ] Verify branch deleted:
  ```bash
  git branch -a
  ```

### 8.9 Update Project Status
- [ ] Update REMOVE_PLAY.md status to "Completed"
- [ ] Update REMOVE_PLAY_TODO.md status to "Completed"
- [ ] Document completion date
- [ ] Document actual time spent
- [ ] Commit status updates:
  ```bash
  git add code_review/REMOVE_PLAY.md code_review/REMOVE_PLAY_TODO.md
  git commit -m "docs: Mark PLAY removal as completed"
  git push origin master
  ```

### 8.10 Update memory.md
- [ ] Update memory.md with completion info:
  ```bash
  date +"%Y-%m-%d %H:%M:%S" >> ../../memory.md
  echo "Completed PLAY/WAV removal from esp_bt_audio_source" >> ../../memory.md
  echo "- Removed ~1,500-2,000 lines of code" >> ../../memory.md
  echo "- Reclaimed ~1-2 MB flash space" >> ../../memory.md
  echo "- Simplified audio architecture: 3 sources (I2S, SYNTH, SILENCE)" >> ../../memory.md
  ```

### 8.11 Verification
- [ ] All changes committed to master
- [ ] Branch deleted (local and remote)
- [ ] Master builds and tests pass
- [ ] Documentation updated with completion status
- [ ] memory.md updated
- [ ] Project ready for next task

---

## Success Criteria

### Code Quality
- [ ] All compiler warnings resolved
- [ ] No undefined symbols or linker errors
- [ ] Code compiles for all configurations (host, component, app)
- [ ] No TODO or FIXME comments left from removal

### Functionality
- [ ] All regression tests pass (see Testing Strategy)
- [ ] BEEP works standalone
- [ ] I2S capture → Bluetooth works
- [ ] SYNTH mode works
- [ ] No audio dropouts or quality issues

### Documentation
- [ ] No references to PLAY in user-facing docs
- [ ] README updated with correct command list
- [ ] Architecture docs reflect 3 audio sources
- [ ] REMOVE_PLAY.md completed and accurate

### Flash/Memory
- [ ] SPIFFS partition removed from partition table
- [ ] Binary size reduced by ~50-100 KB
- [ ] Flash usage reduced by ~1-2 MB (SPIFFS reclaimed)
- [ ] Heap usage stable or improved

### Testing
- [ ] All automated tests pass
- [ ] Manual hardware tests pass (if performed)
- [ ] No regressions in existing features
- [ ] Test coverage remains >80% (excluding removed tests)

---

## Progress Tracking

**Start Date:** _________________  
**End Date:** _________________  
**Actual Time Spent:** _________________  

**Phase Completion:**
- Phase 1 completed: _________________
- Phase 2 completed: _________________
- Phase 3 completed: _________________
- Phase 4 completed: _________________
- Phase 5 completed: _________________
- Phase 6 completed: _________________
- Phase 7 completed: _________________
- Phase 8 completed: _________________

**Notes:**
- _____________________________________________
- _____________________________________________
- _____________________________________________

---

## Reference

See [REMOVE_PLAY.md](REMOVE_PLAY.md) for detailed rationale, architecture analysis, and implementation plan.

---

**End of TODO List**
