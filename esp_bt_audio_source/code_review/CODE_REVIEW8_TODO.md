# CODE_REVIEW8 TODO List

This document tracks action items from CODE_REVIEW8.md (ChatGPT 5.2 review, 2026-02-09).

## Priority Legend
- **P0**: Critical - fix soon (safety/correctness issues)
- **P1**: High - should address in next iteration (maintainability, low-hanging fruit)
- **P2**: Medium - nice to have (architectural improvements)
- **P3**: Low - consider later (theoretical concerns, premature optimization)

---

## P0: Critical Issues (Fix Soon)

### ✅ B. Ignored Return Codes in Diagnostic Paths **[COMPLETE]**
**File**: `components/command_interface/cmd_handlers_system.c`  
**Issue**: `bt_get_streaming_info(&stream_info)` return value is ignored (cast-to-void)  
**Risk**: Partially uninitialized data printed, misleads debugging

**Tasks**:
- [x] Locate call to `bt_get_streaming_info()` in cmd_handlers_system.c
- [x] Check return value explicitly
- [x] On failure, print "STREAM_INFO=UNAVAILABLE" or similar
- [x] Add unit test for failure case (test_status_command_streaming_info_unavailable)

**Completed**: 2026-02-09 13:05  
**Status**: All host tests passing (244/244)

**Implementation Summary**:
- Modified cmd_handle_status() to check bt_get_streaming_info() return value
- On success: prints full streaming stats
- On failure: prints basic status with "STREAM_INFO=UNAVAILABLE"
- Added test infrastructure: bt_manager_test_force_streaming_info_failure()
- New test verifies graceful failure handling

**Estimated effort**: 10-15 minutes (actual: ~25 minutes with test coverage)

---

### ❌ D. NVS Write Rate / Flash Wear Audit
**Files**: `components/nvs_storage/*`, volume/pairing commit points  
**Issue**: Ensure NVS commits aren't too aggressive (e.g., volume on every increment)  
**Risk**: Premature flash wear, reduced device lifetime

**Tasks**:
- [ ] Audit all `nvs_storage_*` write/commit calls
- [ ] Identify volume change commit points
- [ ] Verify debouncing/rate-limiting exists for rapid changes
- [ ] Check pairing data commit frequency
- [ ] If missing, add debounce logic (e.g., commit on "settled" value after 500ms)
- [ ] Document NVS write strategy in comments or ARCH.md

**Estimated effort**: 30-45 minutes investigation + potential fixes

---

## P1: High Priority (Next Iteration)

### ❌ Split bt_manager.c into Smaller Modules
**File**: `components/bt_manager/bt_manager.c` (large, many responsibilities)  
**Reason**: Cognitive complexity, difficult to safely change  
**Suggested structure**:
- `bt_pairing_store.c` - pairing persistence, pending state
- `bt_scan.c` - device discovery
- `bt_connection.c` - connect/disconnect logic
- `bt_events_a2dp.c` - A2DP event handlers
- `bt_events_avrc.c` - AVRCP event handlers
- `bt_manager_core.c` - initialization, coordination, exported API wrappers

**Tasks**:
- [ ] **Phase 1: Preparation**
  - [ ] Review bt_manager.c structure (functions, state, dependencies)
  - [ ] Create refactoring plan: which functions go where
  - [ ] Ensure all tests passing before refactor (baseline)
  - [ ] Create git branch: `refactor/bt-manager-split`

- [ ] **Phase 2: Extract Pairing Store**
  - [ ] Create `bt_pairing_store.c` + `bt_pairing_store.h`
  - [ ] Move pairing-related functions:
    - `bt_pairing_format_mac`, `bt_pairing_set_pending_addr`, `bt_pairing_clear_pending_flags`
    - `bt_pairing_parse_mac_string`, `bt_pairing_addr_is_zero`, `bt_pairing_prepare_pending_for_event`
    - `bt_pairing_send_event`, `bt_pairing_get_pending_request`, `bt_pairing_confirm`, `bt_pairing_submit_pin`
  - [ ] Move `s_pair_pending` state
  - [ ] Update CMakeLists.txt
  - [ ] Run tests, verify no regressions
  - [ ] Commit: "refactor(bt): Extract pairing logic to bt_pairing_store.c"

- [ ] **Phase 3: Extract Scan Logic**
  - [ ] Create `bt_scan.c` + `bt_scan.h`
  - [ ] Move scan-related functions:
    - `bt_start_scan`, `bt_stop_scan`, device discovery handling
  - [ ] Move discovered devices state if isolated
  - [ ] Update CMakeLists.txt
  - [ ] Run tests, verify no regressions
  - [ ] Commit: "refactor(bt): Extract scan logic to bt_scan.c"

- [ ] **Phase 4: Extract Connection Logic**
  - [ ] Create `bt_connection.c` + `bt_connection.h`
  - [ ] Move connection-related functions:
    - `bt_connect`, `bt_connect_by_name`, `bt_disconnect`
    - Connection state management
  - [ ] Update CMakeLists.txt
  - [ ] Run tests, verify no regressions
  - [ ] Commit: "refactor(bt): Extract connection logic to bt_connection.c"

- [ ] **Phase 5: Extract Event Handlers**
  - [ ] Create `bt_events_a2dp.c` + `bt_events_a2dp.h`
  - [ ] Move A2DP event handlers:
    - `bt_manager_handle_a2dp_connection`, `bt_manager_handle_a2dp_audio`
    - `bt_app_a2d_callback`, `bt_app_a2d_data_callback`
  - [ ] Create `bt_events_avrc.c` + `bt_events_avrc.h`
  - [ ] Move AVRCP event handler: `bt_app_avrc_ct_callback`
  - [ ] Create `bt_events_gap.c` + `bt_events_gap.h`
  - [ ] Move GAP event handler: `bt_app_gap_callback`
  - [ ] Update CMakeLists.txt
  - [ ] Run tests, verify no regressions
  - [ ] Commit: "refactor(bt): Extract event handlers to bt_events_*.c"

- [ ] **Phase 6: Core Manager**
  - [ ] Rename remaining bt_manager.c or keep as coordination layer
  - [ ] Ensure clean public API surface in bt_manager.h
  - [ ] Document module responsibilities in header comments
  - [ ] Run full clang-tidy + test suite
  - [ ] Commit: "refactor(bt): Finalize bt_manager module split"

- [ ] **Phase 7: Cleanup**
  - [ ] Update ARCH.md with new bt_manager component structure
  - [ ] Review includes/dependencies between new modules
  - [ ] Ensure no circular dependencies
  - [ ] Run final validation (clang-tidy + 390 tests)
  - [ ] Merge to master

**Estimated effort**: 4-6 hours (incremental, test after each phase)

---

### ❌ A. Fix Confusing Preprocessor Split (bt_disconnect + Others)
**File**: `components/bt_manager/bt_manager.c`  
**Issue**: Weak attribute split across #ifdef blocks is brittle and hard to read  
**Current**:
```c
#if defined(UNIT_TEST)
__attribute__((weak))
bt_err_t bt_disconnect(void) {
#else
 bt_err_t bt_disconnect(void) {
#endif
```

**Tasks**:
- [ ] Define `MAYBE_WEAK` macro at top of bt_manager.c:
  ```c
  #ifdef UNIT_TEST
  #define MAYBE_WEAK __attribute__((weak))
  #else
  #define MAYBE_WEAK
  #endif
  ```
- [ ] Apply to all weak functions:
  - `bt_disconnect`
  - Any other functions using this pattern
- [ ] Verify unit tests still link correctly (weak symbols work)
- [ ] Run clang-tidy and fix any new warnings
- [ ] Commit: "refactor(bt): Use MAYBE_WEAK macro for cleaner test hooks"

**Estimated effort**: 15-20 minutes

---

### ❌ C. Audit Logging in Hot Paths
**Files**: All components, especially `bt_manager`, `audio_processor`  
**Issue**: `printf()` mixed with `ESP_LOG*`, blocking I/O can affect timing  
**Risk**: Audio glitches, timing violations in callbacks/ISRs

**Tasks**:
- [ ] Audit all `printf()` calls in production code paths (grep for `printf\(`)
- [ ] Identify hot paths: A2DP/AVRC callbacks, audio data callback, ISRs
- [ ] Replace blocking `printf()` with:
  - `ESP_LOG*` for diagnostics (can be compile-time gated)
  - Non-blocking console output if printf is essential
- [ ] Add compile-time gate for verbose logging:
  ```c
  #if CONFIG_BT_MANAGER_VERBOSE_LOGGING
  ESP_LOGI(TAG, "...");
  #endif
  ```
- [ ] Document logging policy in ARCH.md or component READMEs
- [ ] Run tests with verbose logging disabled to verify production builds are clean

**Estimated effort**: 1-2 hours (audit + selective fixes)

---

## P2: Medium Priority (Architectural Improvements)

### ❌ Define BT State Access Contract
**Files**: `components/bt_manager/*`, `components/command_interface/*`  
**Issue**: BT state updated from callbacks/task, read from command handlers without explicit locking  
**Risk**: Potential race conditions if refactored incorrectly

**Tasks**:
- [ ] Document current threading model:
  - BT task/queue (bt_app_core)
  - Command interface task
  - Any other tasks accessing BT state
- [ ] Choose contract:
  - **Option A**: Single-threaded ownership (all BT state on BT task, read via request/response)
  - **Option B**: Locking (mutex/critical section around shared state)
- [ ] If Option A:
  - [ ] Add "get status" request/response queue
  - [ ] Route all reads through BT task
- [ ] If Option B:
  - [ ] Add mutex protecting `bt_ctx` and related state
  - [ ] Wrap all reads/writes with lock
- [ ] Document chosen approach in ARCH.md
- [ ] Add assertions/checks to enforce contract

**Estimated effort**: 2-3 hours (design + implementation)

---

### ❌ Refactor Platform/Test #ifdefs into Shims
**Files**: Multiple components with `#ifdef ESP_PLATFORM`, `#ifdef UNIT_TEST`  
**Issue**: Platform-specific code mixed with logic, hard to maintain both branches  
**Recommendation**: Push platform splits behind small shim layers

**Tasks**:
- [ ] Identify heavily #ifdef'd files (grep for `#ifdef ESP_PLATFORM` | wc -l)
- [ ] For each high-count file:
  - [ ] Create platform shim header (e.g., `bt_platform_shim.h`)
  - [ ] Define abstract interface (init, deinit, operations)
  - [ ] Implement ESP32 version (`bt_platform_esp32.c`)
  - [ ] Implement host/test version (`bt_platform_host.c`)
  - [ ] Refactor core logic to call shim, remove #ifdefs from logic
- [ ] Update CMakeLists.txt to compile correct shim per target
- [ ] Run tests on both platforms
- [ ] Document shim pattern in ARCH.md

**Estimated effort**: 4-6 hours (gradual, per-component)

---

### ❌ Replace Weak "Success" Stubs with "Explicit Error"
**Files**: `components/nvs_storage/*`, `components/bt_manager/*` (test hooks)  
**Issue**: Weak stubs returning success can let tests pass while production fails  
**Recommendation**: Return distinct error unless test explicitly overrides

**Tasks**:
- [ ] Audit all weak stub functions (grep for `__attribute__((weak))`)
- [ ] For critical APIs (NVS writes, BT operations):
  - [ ] Change default weak stub to return `ESP_ERR_NOT_SUPPORTED` or similar
  - [ ] Update tests to explicitly provide mocks/stubs
- [ ] Verify tests fail appropriately when mock missing
- [ ] Document test mock strategy in README_TESTS.md

**Estimated effort**: 1-2 hours

---

## P3: Low Priority (Consider Later / Theoretical)

### ⚠️ Pre-allocate Everything to Avoid Runtime Heap
**Files**: `audio_span_log`, SPANLOG command  
**Issue**: Some dynamic allocation in runtime paths  
**Comment**: Premature optimization, not causing issues

**Tasks**:
- [ ] IF long-running stability issues emerge:
  - [ ] Profile heap usage over extended runtime
  - [ ] Identify fragmentation or allocation failures
  - [ ] Pre-allocate span log at init (fixed size pool)
  - [ ] Pre-allocate SPANLOG command buffer (or use stack)
- [ ] Otherwise: **DEFER** until evidence of problem

**Estimated effort**: 2-3 hours IF needed

---

### ⚠️ Comprehensive Platform Shim Layer Refactoring
**Issue**: ESP32-specific project, cross-platform not a requirement  
**Comment**: Nice to have, but not urgent

**Tasks**:
- [ ] IF targeting multiple platforms (ESP32-C3, S3, other vendors):
  - [ ] Design comprehensive HAL (Hardware Abstraction Layer)
  - [ ] Abstract all ESP-IDF dependencies
  - [ ] Create platform configs
- [ ] Otherwise: **DEFER** - current structure is fine for ESP32-only

**Estimated effort**: 8-16 hours IF needed

---

## Completed / Non-Issues

### ✅ Zero-delay WDT Bug (Already Fixed)
**Issue**: `pdMS_TO_TICKS(2) = 0` at 100Hz causing watchdog  
**Status**: Fixed in commit 1fbb6759 with delay clamp

### ✅ Audio Autostart Configuration (Already Implemented)
**Issue**: Manual START required after every boot  
**Status**: Fixed in commit 1fbb6759 with CONFIG_AUDIO_AUTOSTART_DEFAULT=y

---

## Summary Metrics

- **P0 (Critical)**: 2 items → 1 remaining (~30-45 min remaining)
  - ✅ B. Ignored return codes (COMPLETE)
  - ❌ D. NVS write rate audit (TODO)
- **P1 (High)**: 4 items (~8-10 hours total, can be incremental)
- **P2 (Medium)**: 3 items (~7-11 hours total, architectural)
- **P3 (Low)**: 2 items (defer unless evidence of need)

**Next Actions**:
1. ~~Fix ignored return code (B)~~ ✅ COMPLETE  
2. Audit NVS write rate (D) - prevents flash wear
3. Plan bt_manager.c split (largest refactor, do incrementally)

---

## Notes

- All refactoring must pass full test suite (390 tests) before merge
- Run clang-tidy after each significant change
- Commit incrementally with conventional commit messages
- Update ARCH.md when module structure changes
- Tag releases before/after major refactors for rollback safety
