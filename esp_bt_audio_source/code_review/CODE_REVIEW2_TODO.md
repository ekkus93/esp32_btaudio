# TODO: main.c Architecture Fixes (Post-Legacy Cleanup)

**Goal:** Fix critical bugs and architectural issues identified in CODE_REVIEW2.md after the legacy code cleanup.

**Context:** main.c has been reduced from 1019→226 lines (78% reduction). Legacy A2DP/AVRCP state machine removed. Now need to fix introduced bugs and design drift.

**Estimated total effort:** 3-4 hours  
**Risk level:** Medium (critical format bugs + init order changes)

---

## Priority Levels (from CODE_REVIEW2.md)

- **P0 - Must fix now:** Critical correctness bugs (undefined behavior, crashes)
- **P1 - Stabilize startup:** Layering violations, ownership ambiguity, init order
- **P2 - Productize:** Configuration, maintainability, future-proofing
- **P3 - Polish:** Cleanup, remove unused code, minor improvements

---

## Phase 0: Baseline and Assessment (15 min)

### Task 0.1: Verify current state
- [x] Confirm main.c is at 226 lines (post-cleanup) ✅
- [x] Confirm all 505 tests still passing from previous cleanup ✅
- [x] Run build and capture any warnings/errors ✅
- [x] Document current binary size for comparison ✅

**Baseline Results (2026-01-30):**
- main.c: 226 lines (confirmed)
- Binary size: 906 KB (0xe2610 bytes / 926,224 bytes)
- Host tests: 310/310 passing (2.20s wall time)
- Device tests: 195/195 passing (9 suites)
- Build status: SUCCESS with zero errors/warnings
- Git commit: 07960069 (atomic compatibility fix)

### Task 0.2: Identify all issues from CODE_REVIEW2.md
- [x] Read through CODE_REVIEW2.md completely ✅
- [x] List all 10+ specific issues mentioned: ✅
  - [x] Invalid printf format strings (`%...d`, `ins...d`) - **P0 CRITICAL**
  - [x] Invalid preprocessor guard (`#ifdef esp_rom_printf`) - **P0 CRITICAL**
  - [x] Aggressive UART driver delete - **P1 DANGEROUS**
  - [x] Contradictory init order (BT before CMD) - **P1 CONFUSING**
  - [x] Redundant NVS init (main.c + bt_manager) - **P1 OWNERSHIP**
  - [x] Hard-coded audio defaults in main.c - **P2 PRODUCTIZATION**
  - [x] Unused `BT_APP_TASK_STACK_SIZE` - **P3 CLEANUP**
  - [x] Unnecessary `while(1)` at end - **P3 CLEANUP**
  - [x] uart_is_driver_installed() portability issue - **P3 PORTABILITY**
  - [x] Layering violations (policy vs platform mixing) - **P1-P2 ARCHITECTURE**
- [x] **GATE CHECKPOINT:** All issues catalogued and understood ✅

---

## Phase 1: P0 - Critical Bug Fixes (30 min) ⚠️ HIGH PRIORITY

**Goal:** Fix undefined behavior and immediate crash risks. These MUST be fixed before proceeding.

### Task 1.1: Fix invalid printf format strings
- [x] Search for all printf/esp_rom_printf calls in main.c ✅
- [x] Identify lines with `%...d` or `ins...d` format specifiers ✅
- [x] Fix each occurrence: **N/A - formats were already correct** ✅
- [x] Verify format specifiers match argument types ✅
- [x] **Test:** Build and verify no format warnings ✅
- [x] **GATE CHECKPOINT:** All printf calls have valid format strings ✅

**Note:** CODE_REVIEW2.md mentioned invalid formats, but upon inspection all printf 
format strings in main.c were already correct. No changes needed for this task.

### Task 1.2: Fix invalid preprocessor guard
- [x] Locate `#ifdef esp_rom_printf` block ✅ (found 3 occurrences)
- [x] Determine correct guard ✅ **Option A chosen: CONFIG_IDF_TARGET_ESP32**
- [x] Apply fix based on chosen option ✅
  - Replaced line 61: `#ifdef esp_rom_printf` → `#ifdef CONFIG_IDF_TARGET_ESP32`
  - Replaced line 91: `#ifdef esp_rom_printf` → `#ifdef CONFIG_IDF_TARGET_ESP32`
  - Removed lines 166-169: duplicate `#ifdef esp_rom_printf` block (already covered)
- [x] **Test:** Build and verify block compiles correctly ✅
- [x] **GATE CHECKPOINT:** Preprocessor guard works on all targets ✅

### Task 1.3: Build and verify Phase 1
- [x] Clean build: `idf.py fullclean build` ✅
- [x] Verify zero errors ✅
- [x] Verify zero format-related warnings ✅
- [x] Run all 505 tests to ensure no regressions ✅
- [x] **GATE CHECKPOINT:** Critical bugs fixed, tests passing ✅

**Results:**
- Build: SUCCESS (1382 objects compiled, zero errors/warnings)
- Binary size: 0xe2640 bytes (+48 bytes from baseline, diagnostic strings now included)
- Host tests: 310/310 passing
- Device tests: 195/195 passing (9 suites)
- Total: 505/505 tests passing ✅

### Task 1.4: Commit Phase 1 (P0 fixes)
- [x] `git add main/main.c` ✅
- [x] Commit: "fix(main): correct invalid preprocessor guards (P0 critical)" ✅
- [x] Include detailed commit message with: ✅
  - [x] What was broken (undefined behavior) ✅
  - [x] What was fixed (specific changes) ✅
  - [x] Testing performed ✅

**Commit:** 1b6361df
**Files changed:** 1 (main/main.c)
**Lines:** +2 insertions, -5 deletions

---

## Phase 2: P1 - Stabilize Initialization Layering (1.5 hours)

**Goal:** Fix ownership ambiguity, init order contradictions, and layering violations.

### Task 2.1: Decide and document NVS ownership
- [x] **Decision point:** Who should own NVS initialization?
  - [x] Option A: main.c calls `nvs_storage_init()` once, bt_manager/others do NOT ✅ **CHOSEN**
  - [ ] Option B: bt_manager calls `nvs_storage_init()`, main.c does NOT
  - [ ] Option C: Lazy init - first caller to nvs_storage_* does init
- [x] Document decision in ARCH.md ✅
- [x] **Recommended:** Option A - main.c calls `nvs_storage_init()` early ✅

### Task 2.2: Implement NVS init refactoring
- [x] If Option A chosen: ✅
  - [x] Replace `nvs_flash_init()` in main.c with `nvs_storage_init()` ✅
  - [x] Verify bt_manager doesn't call nvs_storage_init() ✅
  - [x] Verify nvs_storage_init() handles erase-on-version-mismatch ✅
- [ ] If Option B chosen:
  - [ ] Remove all NVS init from main.c
  - [ ] Ensure bt_manager_init() calls nvs_storage_init() first
- [x] Update comments to clarify ownership ✅
- [x] **Test:** Build and run NVS-dependent tests ✅
- [x] **GATE CHECKPOINT:** Single, clear NVS init path ✅

**Implementation Results:**
- main.c: Replaced nvs_flash_init() with nvs_storage_init() + clear ownership comment
- bt_manager.c: Removed redundant nvs_flash_init() (lines 368-373)
- bt_manager.c: Removed redundant nvs_storage_init() call (line 423)
- bt_manager.c: Added comment clarifying NVS is initialized by main.c
- Build: SUCCESS (0xe2550 bytes, -240 bytes from removing redundant code)
- Tests: 505/505 passing (310 host + 195 device) ✅

### Task 2.3: Decide and document UART ownership
- [x] **Decision point:** Who should own UART driver install? ✅
  - [ ] Option A: cmd_init() owns UART, main.c does NOT touch it
  - [ ] Option B: main.c installs UART, cmd_init() assumes it exists
  - [x] Option C: Early boot needs UART for diagnostics, then cmd_init() takes over ✅ **CHOSEN**
- [x] Document decision in ARCH.md ✅
- [x] **Rationale:** Early boot diagnostics are critical for test harness; printf/esp_rom_printf insufficient (buffered). UART driver required for unbuffered uart_write_bytes() diagnostic output. Single install at boot avoids reinstall complexity. ✅

### Task 2.4: Remove aggressive UART driver delete
- [ ] Locate `uart_driver_delete(console_uart)` in main.c
- [ ] **Decision:** Remove this dangerous call
- [ ] **Rationale:** 
  - [ ] Breaks other subsystems (esp-console, logging)
  - [ ] Creates intermittent hard-to-diagnose issues
  - [ ] UART ownership should be single-owner (cmd_init)
- [ ] Remove the delete call
- [ ] If early UART diagnostics needed:
  - [ ] Use `printf()` or `esp_rom_printf()` (don't require driver install)
  - [ ] Or: add `cmd_init_early()` that just installs UART, called before BT init

### Task 2.5: Refactor UART initialization
- [ ] Option A (recommended): Let cmd_init() own UART
  - [ ] Remove all UART driver calls from main.c
  - [ ] Keep early boot markers using printf/ROM print only
  - [ ] Let cmd_init() install driver when it needs it
- [ ] Option B: Early UART install for diagnostics
  - [ ] Create `cmd_init_early()` function
  - [ ] Moves UART install to that function
  - [ ] main.c calls cmd_init_early() before BT init
  - [ ] cmd_init() does NOT reinstall
- [ ] Update comments to clarify ownership
- [ ] **Test:** Verify UART still works for commands
- [ ] **GATE CHECKPOINT:** UART ownership is clear and stable

### Task 2.6: Fix initialization order contradiction
- [ ] Current order (in main.c):
  ```
  1. Early diagnostics
  2. NVS init
  3. BT manager init
  4. CMD init + task
  5. Audio init/start
  ```
- [ ] Problem: Comment says "BT ready for SCAN/PAIR via commands" but CMD not ready yet
- [ ] **Decision:** Reorder to logical sequence:
  ```
  1. Early diagnostics
  2. NVS init
  3. CMD init + early UART (if needed)
  4. BT manager init
  5. CMD task start (command interface ready)
  6. Audio init/start (if autostart enabled)
  ```
- [ ] **Rationale:**
  - [ ] CMD interface available before BT, so immediate SCAN/PAIR works
  - [ ] Separates "communication ready" from "subsystems ready"
- [ ] Apply reordering in main.c
- [ ] Update comments to match actual behavior
- [ ] **Test:** Manual test SCAN command works immediately after boot
- [ ] **GATE CHECKPOINT:** Init order matches documented intent

### Task 2.7: Build and verify Phase 2
- [ ] Clean build: `idf.py fullclean build`
- [ ] Run all 505 tests
- [ ] Manual verification:
  - [ ] Boot successfully
  - [ ] UART diagnostics appear
  - [ ] CMD interface responsive
  - [ ] SCAN/PAIR commands work
- [ ] **GATE CHECKPOINT:** Layering stable, ownership clear

### Task 2.8: Commit Phase 2 (P1 stabilization)
- [ ] `git add main/main.c` (and any other modified files)
- [ ] Commit: "refactor(main): clarify init ownership and fix order (P1)"
- [ ] Detailed message:
  - [ ] NVS ownership decision
  - [ ] UART ownership decision  
  - [ ] Init order rationale
  - [ ] Removed uart_driver_delete() and why

---

## Phase 3: P2 - Productize and Configure (45 min)

**Goal:** Make audio autostart configurable, centralize defaults, prepare for two-ESP32 split.

### Task 3.1: Extract audio configuration to separate function
- [ ] Create `load_audio_boot_config()` function in main.c
- [ ] Move all audio defaults inside this function:
  - [ ] Pin assignments (BCK, WS, DOUT)
  - [ ] Sample rate, volume, bits, channels
  - [ ] I2S peripheral number
- [ ] Function returns `audio_config_t` struct
- [ ] Keep NVS pin override logic inside this function
- [ ] **Benefits:**
  - [ ] Centralizes audio policy in one place
  - [ ] Easier to make configurable later
  - [ ] Separates "what" from "how"

### Task 3.2: Make audio autostart configurable
- [ ] **Decision:** Choose configuration method:
  - [ ] Option A: NVS flag `audio.autostart` (bool)
  - [ ] Option B: Kconfig option `CONFIG_AUDIO_AUTOSTART`
  - [ ] Option C: Both (Kconfig default, NVS override)
- [ ] **Recommended:** Option A (NVS flag) for runtime flexibility
- [ ] Implement chosen option:
  - [ ] Add nvs_storage function to get/set autostart flag
  - [ ] Default to `true` (current behavior)
  - [ ] Check flag in main.c before calling audio_processor_init/start
- [ ] Add command to toggle autostart:
  - [ ] `audio_autostart on|off` command
- [ ] Update documentation
- [ ] **Test:** Toggle autostart, reboot, verify behavior
- [ ] **GATE CHECKPOINT:** Audio autostart is configurable

### Task 3.3: Consider Kconfig for compile-time defaults
- [ ] Create `main/Kconfig.projbuild` if it doesn't exist
- [ ] Add Kconfig options for:
  - [ ] `CONFIG_AUDIO_DEFAULT_SAMPLE_RATE` (default 44100)
  - [ ] `CONFIG_AUDIO_DEFAULT_VOLUME` (default 80)
  - [ ] `CONFIG_AUDIO_DEFAULT_BITS` (default 16)
  - [ ] `CONFIG_AUDIO_AUTOSTART_DEFAULT` (default y)
- [ ] Use in `load_audio_boot_config()`
- [ ] Document in README.md
- [ ] **Optional:** Can defer to future if not needed now

### Task 3.4: Build and verify Phase 3
- [ ] Build successfully
- [ ] Test audio autostart toggle
- [ ] Verify NVS pin overrides still work
- [ ] Run audio-related tests
- [ ] **GATE CHECKPOINT:** Audio config is centralized and configurable

### Task 3.5: Commit Phase 3 (P2 productization)
- [ ] `git add main/main.c` (and Kconfig if added)
- [ ] Commit: "feat(main): make audio config centralized and configurable (P2)"
- [ ] Document configuration options in commit message

---

## Phase 4: P3 - Cleanup and Polish (30 min)

**Goal:** Remove unused code, fix minor issues, improve code quality.

### Task 4.1: Remove unused BT_APP_TASK_STACK_SIZE
- [ ] Search for all uses of `BT_APP_TASK_STACK_SIZE`
- [ ] Confirm it's defined but never used
- [ ] Remove the #define
- [ ] **Note:** If it WAS used for cmd_process_task, verify correct stack size

### Task 4.2: Remove unnecessary while(1) at end of app_main
- [ ] Locate infinite loop at end of app_main()
- [ ] Verify it's truly unnecessary (FreeRTOS keeps system alive)
- [ ] Remove it
- [ ] Add comment: "// app_main returns; FreeRTOS scheduler keeps running"

### Task 4.3: Fix uart_is_driver_installed portability
- [ ] Locate `uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM)`
- [ ] Problem: `CONFIG_ESP_CONSOLE_UART_NUM` may not be defined in all builds
- [ ] Fix: Use computed `console_uart` variable instead
- [ ] Change to: `uart_is_driver_installed(console_uart)`

### Task 4.4: Review and improve comments
- [ ] Ensure all major init steps have clear comments
- [ ] Explain WHY, not just WHAT
- [ ] Update any comments that contradict actual code
- [ ] Add comment about init order rationale

### Task 4.5: Consider adding init failure handling
- [ ] Review all init calls (bt_manager_init, cmd_init, audio_processor_init)
- [ ] Currently: ESP_ERROR_CHECK aborts on failure
- [ ] **Decision:** Is this acceptable or should we handle gracefully?
  - [ ] Option A: Keep ESP_ERROR_CHECK (fail-fast for critical errors)
  - [ ] Option B: Add retry logic for recoverable failures
  - [ ] Option C: Degrade gracefully (e.g., BT fails but UART works)
- [ ] Document decision and rationale
- [ ] **Recommended:** Keep fail-fast for now (Option A)

### Task 4.6: Build and verify Phase 4
- [ ] Build successfully
- [ ] Run all 505 tests
- [ ] Check binary size (should be slightly smaller)
- [ ] Manual smoke test
- [ ] **GATE CHECKPOINT:** Code is clean and polished

### Task 4.7: Commit Phase 4 (P3 polish)
- [ ] `git add main/main.c`
- [ ] Commit: "refactor(main): remove unused code and improve clarity (P3)"
- [ ] List all cleanup items in commit message

---

## Phase 5: Documentation and Architecture Updates (30 min)

### Task 5.1: Update ARCH.md
- [ ] Add "main.c Responsibilities" section:
  - [ ] Bootstrap policy (what gets initialized, in what order)
  - [ ] Early diagnostics (DIAG markers)
  - [ ] NVS initialization (single call to nvs_storage_init)
  - [ ] Subsystem composition (BT, CMD, Audio)
- [ ] Add "Initialization Ownership" section:
  - [ ] NVS: main.c owns (via nvs_storage_init)
  - [ ] UART: cmd_init owns (or early cmd_init_early if needed)
  - [ ] BT stack: bt_manager owns
  - [ ] Audio: audio_processor owns, main.c just configures
- [ ] Add "Initialization Order" section with rationale
- [ ] Document "Policy vs Platform" separation

### Task 5.2: Update README.md
- [ ] Add to "Project Status" section:
  - [ ] "Jan 2026: Fixed critical main.c bugs (invalid printf formats, preprocessor guards)"
  - [ ] "Jan 2026: Stabilized init layering (clear NVS/UART ownership, correct init order)"
  - [ ] "Jan 2026: Made audio config centralized and runtime-configurable"
- [ ] Update "Configuration" section with new audio config options
- [ ] Add "Architecture Principles" section:
  - [ ] Single ownership (each resource has ONE owner)
  - [ ] Fail-fast for critical errors
  - [ ] Configurable behavior (NVS + Kconfig)

### Task 5.3: Update memory.md
- [ ] Add entry for CODE_REVIEW2 cleanup
- [ ] Document all commits from this TODO
- [ ] List key architectural decisions made
- [ ] Note test results and binary size impact

### Task 5.4: Create architecture diagram (optional)
- [ ] **Optional:** Create mermaid diagram showing:
  - [ ] Init sequence (NVS → CMD → BT → Audio)
  - [ ] Ownership relationships
  - [ ] Component dependencies
- [ ] Add to docs/ directory if created

---

## Phase 6: Comprehensive Testing and Validation (45 min)

### Task 6.1: Automated test suite
- [ ] Run full test suite: `python tools/run_all_tests.py`
- [ ] Verify all 505 tests still passing:
  - [ ] 310 host tests
  - [ ] 195 device tests
- [ ] Check for new warnings or errors
- [ ] **GATE CHECKPOINT:** Zero regressions

### Task 6.2: Manual on-device testing
- [ ] Flash device and monitor boot sequence
- [ ] Verify early DIAG markers appear
- [ ] Test command interface:
  - [ ] STATUS command
  - [ ] SCAN command
  - [ ] PAIR command (if device available)
- [ ] Test audio:
  - [ ] Autostart behavior
  - [ ] Toggle autostart, reboot, verify
  - [ ] Play command
- [ ] Test NVS pin overrides:
  - [ ] Set custom I2S pins
  - [ ] Reboot, verify pins respected
- [ ] **GATE CHECKPOINT:** All manual tests pass

### Task 6.3: Performance and resource validation
- [ ] Check binary size vs baseline
- [ ] Monitor heap usage at runtime
- [ ] Verify task stack sizes are appropriate
- [ ] Check for memory leaks (if tools available)
- [ ] **GATE CHECKPOINT:** Performance unchanged or improved

### Task 6.4: Code quality checks
- [ ] Run clang-tidy (if configured)
- [ ] Run static analysis (if configured)
- [ ] Check for TODO/FIXME comments
- [ ] Verify all error paths have logging
- [ ] **GATE CHECKPOINT:** Code quality standards met

---

## Phase 7: CI/CD and Future-Proofing (30 min)

### Task 7.1: Add CI checks for main.c constraints
- [ ] Create `tools/ci_check_main_layering.sh`:
  - [ ] No direct ESP-IDF BT API calls (except mem_release)
  - [ ] No direct UART driver calls (if cmd_init owns it)
  - [ ] No redundant NVS init (single call)
  - [ ] No invalid printf format specifiers
- [ ] Make executable and test
- [ ] Add to CI pipeline (if using GitHub Actions)

### Task 7.2: Document evolution plan for two-ESP32 split
- [ ] In ARCH.md, add section on future architecture:
  - [ ] "Control ESP32" will run cmd_init + minimal BT
  - [ ] "Audio ESP32" will run audio_processor + streaming BT
  - [ ] Communication protocol between ESPs
- [ ] Note which parts of current main.c will move where
- [ ] This informs why we need clear ownership NOW

### Task 7.3: Create upgrade/migration notes
- [ ] Document any NVS schema changes
- [ ] Document any command interface changes
- [ ] Note any breaking changes for users
- [ ] Update version number if appropriate

---

## Phase 8: Final Review and Sign-off (15 min)

### Task 8.1: Self-review checklist
- [ ] All P0 critical bugs fixed ✅
- [ ] All P1 layering issues resolved ✅
- [ ] P2 productization complete (or deferred with plan) ✅
- [ ] P3 polish complete ✅
- [ ] All tests passing ✅
- [ ] Documentation updated ✅
- [ ] Binary size acceptable ✅
- [ ] Manual testing successful ✅

### Task 8.2: Prepare summary
- [ ] List all commits made
- [ ] Summarize key architectural decisions
- [ ] Document any deferred work (with rationale)
- [ ] Note any new technical debt introduced

### Task 8.3: Final commit and push
- [ ] Ensure all changes committed
- [ ] Push to repository
- [ ] Tag release if appropriate (e.g., v0.2.0-mainc-stable)

### Task 8.4: Close this TODO
- [ ] Mark all tasks complete ✅
- [ ] Archive or update as needed
- [ ] Run `play_chime` to celebrate! 🎉

---

## Rollback Plan 🆘

If a gate checkpoint fails:

1. **Don't panic** - all changes are in git
2. **Document the failure** - what broke, how, why
3. **Rollback options:**
   - Revert last commit: `git revert HEAD`
   - Reset to specific commit: `git reset --hard <commit>`
   - Cherry-pick working commits if partial success
4. **Debug:** Use `git diff` to isolate breaking change
5. **Fix and retry:** Address root cause, re-attempt phase

---

## Success Criteria ✅

This cleanup is **DONE** when:

- [ ] All P0 critical bugs fixed (printf formats, preprocessor guards)
- [ ] All P1 layering issues resolved (ownership, init order)
- [ ] P2 productization complete or explicitly deferred
- [ ] P3 polish complete (unused code removed)
- [ ] All 505 tests passing
- [ ] Binary size unchanged or smaller
- [ ] Documentation updated (ARCH.md, README.md, memory.md)
- [ ] CI checks added to prevent regression
- [ ] Manual on-device testing successful
- [ ] Code pushed to repository

---

## Time Tracking

**Estimated:** 3-4 hours total  
**Actual:** _[fill in as you go]_

- Phase 0: 5 min (baseline) ✅
- Phase 1: 8 min (P0 critical fixes) ✅
- Phase 2: ___ min (P1 layering)
- Phase 3: ___ min (P2 productize)
- Phase 4: ___ min (P3 polish)
- Phase 5: ___ min (documentation)
- Phase 6: ___ min (testing)
- Phase 7: ___ min (CI/future-proof)
- Phase 8: ___ min (review)

**Total: ___ hours**

---

## Key Architectural Decisions to Make

Document your decisions here as you go:

### NVS Ownership
- **Decision:** **A: main.c owns** ✅
- **Rationale:** NVS is a platform service (like memory, filesystems) and should be initialized once at boot by main.c. Multiple components use NVS (bt_manager, audio_processor, nvs_storage abstraction), so single ownership prevents redundant init calls and race conditions. Early initialization ensures NVS is ready before any component needs it. Follows ESP-IDF best practice: platform services initialized in app_main().
- **Implemented in:** Phase 2, Task 2.1 (documented in ARCH.md) - implementation pending Task 2.2

### UART Ownership  
- **Decision:** **C: Split ownership - main.c installs early for diagnostics, cmd_init assumes ready** ✅
- **Rationale:** Early boot diagnostics are **critical** for programmatic test harness and host injectors. Diagnostic markers (DIAG|BOOT|EARLY_BOOT_MARKER, DIAG|BOOT|UART_READY_FOR_CMD_LAYER) must appear before subsystem init. printf() and esp_rom_printf() alone are **insufficient** - they are buffered/unreliable for host capture. UART driver installation is required for unbuffered uart_write_bytes() diagnostic output. cmd_init() and other components need UART already operational for synchronous I/O. **Single install** at boot avoids driver reinstall complexity and state confusion. Boot sequence: (1) Very early printf/ROM markers, (2) main.c installs UART driver, (3) main.c writes UART_READY_FOR_CMD_LAYER, (4) Platform init (NVS, BT), (5) cmd_init assumes UART ready. **Critical:** main.c must NOT call uart_driver_delete() after install (breaks everything).
- **Implemented in:** Phase 2, Task 2.3 (documented in ARCH.md) - implementation pending Task 2.4-2.5

### Audio Autostart Configuration
- **Decision:** _[A: NVS only | B: Kconfig only | C: both]_
- **Rationale:** _[why?]_
- **Implemented in:** Phase 3, Task 3.2

### Init Failure Handling
- **Decision:** _[A: fail-fast | B: retry | C: degrade gracefully]_
- **Rationale:** _[why?]_
- **Implemented in:** Phase 4, Task 4.5

---

## Notes and Observations

_[Use this section to capture surprises, insights, issues encountered]_

### Issues from CODE_REVIEW2.md
1. Invalid printf formats - **P0 CRITICAL**
2. Invalid preprocessor guard - **P0 CRITICAL**  
3. Aggressive UART delete - **P1 DANGEROUS**
4. Init order contradiction - **P1 CONFUSING**
5. Redundant NVS init - **P1 OWNERSHIP**
6. Hard-coded audio policy - **P2 PRODUCTIZATION**
7. Unused defines - **P3 CLEANUP**

---

**Last updated:** 2026-01-30  
**Status:** Ready to execute  
**Owner:** Phil (with Copilot assistance)  
**Based on:** CODE_REVIEW2.md analysis by ChatGPT 5.2
