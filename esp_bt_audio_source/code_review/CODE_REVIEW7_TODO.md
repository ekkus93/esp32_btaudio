# CODE_REVIEW7 TODO List

**Source:** ChatGPT 5.2 Code Review (CODE_REVIEW7.md)  
**Date Created:** 2026-02-09  
**Status:** Not Started

---

## Priority 1: CRITICAL BUG - Fix SYNTH Mode (User-Visible Functional Bug)

### Task 1.1: Fix `get_active_source()` Priority Logic
**File:** `components/audio_processor/audio_processor.c`

**Problem:**
- `SYNTH ON` command after `START` does nothing
- `get_active_source()` checks `s_is_running` before `s_force_synth`
- I2S always takes priority, making SYNTH mode unreachable

**Fix Required:**
- [ ] Locate `get_active_source()` function in audio_processor.c
- [ ] Change priority order to:
  ```c
  // Priority 1: Forced SYNTH mode
  if (s_force_synth) → return AUDIO_SOURCE_SYNTH
  
  // Priority 2: I2S if running
  else if (i2s_manager_is_running() || (s_is_running && i2s_manager_is_running())) 
      → return AUDIO_SOURCE_I2S
  
  // Priority 3: Silence
  else → return AUDIO_SOURCE_SILENCE
  ```

**Subtasks:**
- [ ] Read current `get_active_source()` implementation
- [ ] Identify the exact priority check logic
- [ ] Reorder checks to prioritize `s_force_synth` first
- [ ] Ensure I2S check is second priority
- [ ] Add code comments explaining priority rationale

**Testing:**
- [ ] Create test case: `START` → `SYNTH ON` → verify synth_source_fill() is called
- [ ] Create test case: `SYNTH ON` → `START` → verify synth_source_fill() is called
- [ ] Create test case: `SYNTH ON` → `START` → `SYNTH OFF` → verify I2S takes over
- [ ] Manual test: Flash firmware, connect via UART, issue commands, verify audio source switching

**Acceptance Criteria:**
- ✅ `SYNTH ON` after `START` changes active source to SYNTH
- ✅ `SYNTH OFF` returns to I2S (if I2S running) or SILENCE
- ✅ All existing tests still pass
- ✅ Manual UART test confirms audio source switching works

---

### Task 1.2: Review `audio_processor_start()` I2S Auto-Start Behavior
**File:** `components/audio_processor/audio_processor.c`

**Problem:**
- `audio_processor_start()` may always start I2S, preventing SYNTH-only mode

**Investigation Required:**
- [ ] Check if `audio_processor_start()` unconditionally calls `i2s_manager_start()`
- [ ] Determine if this prevents SYNTH-only operation
- [ ] Decide: Should I2S auto-start be conditional on `!s_force_synth`?

**Options:**
- **Option A:** Keep current behavior (I2S always starts, SYNTH override via priority) ✅ **CHOSEN**
- **Option B:** Make I2S start conditional: `if (!s_force_synth) i2s_manager_start()` ❌
- **Option C:** Add config flag: `CONFIG_AUDIO_AUTOSTART_I2S` ❌

**Decision: Option A (2026-02-09)**

**Rationale:**
- **Auto-reconnect use case**: If Bluetooth headset was paired previously, ESP32 may automatically reconnect on power-up. I2S audio should immediately start streaming without manual `START` command.
- **Simplicity**: No lazy initialization complexity or error handling needed.
- **Fast switching**: I2S ready immediately when SYNTH mode disabled.
- **Power cost negligible**: ~1-2mA for development/bench device (not battery-powered).
- **SYNTH is debugging override**: Primary mode is I2S capture, SYNTH overrides via priority.

**Implementation:**
- [x] Keep current `audio_processor_start()` unchanged (already correct)
- [x] Add comprehensive comment explaining rationale
- [ ] Update ARCH.md with auto-reconnect workflow

**Testing:**
- [x] Verify I2S starts on `audio_processor_start()` (existing behavior)
- [ ] Test auto-reconnect: Power cycle → headset reconnects → verify I2S audio plays
- [ ] Test SYNTH override: `START` → `SYNTH ON` → verify synth plays (Task 1.1 fix enables this)

---

## Priority 2: MEDIUM - Wire Up Span Log (Debugging Infrastructure)

### Task 2.1: Add Span Log Calls to Audio Engine
**File:** `components/audio_processor/audio_processor.c`

**Problem:**
- `audio_span_log.[ch]` exists but is never called
- No debugging visibility into audio engine behavior

**Implementation:**
- [ ] Locate `audio_engine_task()` function
- [ ] Find the point after successful `audio_rb_write(...)`
- [ ] Add `span_log_push()` call with:
  - [ ] Sequence number (increment per write)
  - [ ] Timestamp (`esp_timer_get_time()`)
  - [ ] Bytes written
  - [ ] Ring buffer used bytes (after write)
  - [ ] Ring buffer free bytes (after write)
  - [ ] Active source (I2S/SYNTH/SILENCE)
  - [ ] Beep overlay active flag
  - [ ] Underrun counter snapshot (optional but useful)

**Code Example:**
```c
// After audio_rb_write() succeeds
span_log_entry_t entry = {
    .seq = s_span_seq++,
    .timestamp_us = esp_timer_get_time(),
    .bytes_written = chunk_size,
    .ring_used = audio_rb_get_used_bytes(),
    .ring_free = audio_rb_get_free_bytes(),
    .source = get_active_source(),
    .beep_active = beep_manager_is_active(),
    .underruns = s_stats.buffer_underruns  // snapshot
};
span_log_push(&entry);
```

**Subtasks:**
- [ ] Review `audio_span_log.h` for entry structure
- [ ] Add static sequence counter `s_span_seq` to audio_processor.c
- [ ] Implement span_log_push() call in audio_engine_task()
- [ ] Add error handling if span_log_push() fails

**Testing:**
- [ ] Verify span log fills correctly during audio playback
- [ ] Verify span log wraps correctly (circular buffer)
- [ ] Check for memory leaks or corruption

---

### Task 2.2: Add CLI Command to Dump Span Log
**File:** `components/command_interface/commands.c`

**Implementation:**
- [ ] Create new command: `SPANLOG <count>` or `SPAN_DUMP <count>`
- [ ] Call `span_log_get_last_n(count)` from span log API
- [ ] Format output for UART readability
- [ ] Include column headers: seq, timestamp, bytes, used, free, source, beep, underruns

**Output Format Example:**
```
OK|SPANLOG|
seq,timestamp_us,bytes,used,free,source,beep,underruns
1234,123456789,4096,8192,16384,I2S,0,0
1235,123460000,4096,12288,12288,I2S,1,0
...
```

**Subtasks:**
- [ ] Add SPANLOG command to command parser
- [ ] Implement `cmd_handle_spanlog()` handler
- [ ] Parse count parameter (default: 10, max: 100)
- [ ] Format span entries as CSV or human-readable text
- [ ] Add error handling for invalid count

**Testing:**
- [ ] Test `SPANLOG 10` returns last 10 entries
- [ ] Test `SPANLOG 100` returns last 100 entries (or max available)
- [ ] Test `SPANLOG` with no args uses default (10)
- [ ] Test output parsing/readability

**Acceptance Criteria:**
- ✅ Span log captures every audio_rb_write() event
- ✅ SPANLOG command returns last N entries
- ✅ Output is human-readable and useful for debugging
- ✅ No performance impact on audio engine (span log is lock-free)

---

## Priority 3: MEDIUM - Clean Up WAV Scaffolding

### Task 3.1: Decide on WAV Removal Strategy

**Options:**

**Option A: Complete Removal (Recommended)**
- Remove `audio_processor_play_wav()` function
- Remove `WAV_STATUS` command
- Remove any file-related command handlers (if SPIFFS-specific)
- Simplest, cleanest, smallest binary

**Option B: Gate Behind `#ifdef CONFIG_ENABLE_WAV`**
- Keep WAV code for potential future use
- Guard with `#ifdef CONFIG_ENABLE_WAV` / `#endif`
- Add Kconfig option to enable WAV support
- Allows easy re-enabling if needed

**Decision:**
- [ ] Choose Option A or B (recommend A unless WAV needed soon)
- [ ] Document choice in commit message

---

### Task 3.2: Remove WAV API (If Option A Chosen)
**Files:** 
- `components/audio_processor/audio_processor.c`
- `components/audio_processor/include/audio_processor.h`
- `components/command_interface/commands.c`

**Implementation:**
- [ ] Locate `audio_processor_play_wav(const char* path)` function
- [ ] Remove function implementation from audio_processor.c
- [ ] Remove function declaration from audio_processor.h
- [ ] Locate `cmd_handle_wav_status()` or similar in commands.c
- [ ] Remove `WAV_STATUS` command handler
- [ ] Remove `WAV_STATUS` from command parser
- [ ] Search for any other WAV-related functions/commands
- [ ] Remove all WAV-related code

**Subtasks:**
- [ ] `grep -r "wav" components/` to find all WAV references
- [ ] `grep -r "WAV" components/` (case-sensitive)
- [ ] Review each match, remove if SPIFFS/file-playback related
- [ ] Keep audio_processor_wav.c if it's just I2S sample generation

**Testing:**
- [ ] Build firmware, verify no undefined references
- [ ] Verify WAV_STATUS command returns "COMMAND_NOT_FOUND"
- [ ] Run all host tests, component tests, integration tests
- [ ] Check binary size reduction

**Acceptance Criteria:**
- ✅ No `audio_processor_play_wav()` function exists
- ✅ No `WAV_STATUS` command exists
- ✅ All tests pass
- ✅ Binary size reduced (estimate: 5-10 KB)

---

### Task 3.3: Gate WAV Behind Config (If Option B Chosen)
**Files:**
- `components/audio_processor/Kconfig`
- `components/audio_processor/audio_processor.c`
- `components/audio_processor/include/audio_processor.h`
- `components/command_interface/commands.c`

**Implementation:**
- [ ] Create `CONFIG_ENABLE_WAV` option in Kconfig
- [ ] Default to `n` (disabled)
- [ ] Wrap `audio_processor_play_wav()` in `#ifdef CONFIG_ENABLE_WAV`
- [ ] Wrap WAV_STATUS command in `#ifdef CONFIG_ENABLE_WAV`
- [ ] Add stub implementations when disabled (return ESP_ERR_NOT_SUPPORTED)

**Testing:**
- [ ] Build with `CONFIG_ENABLE_WAV=n`, verify WAV code not compiled
- [ ] Build with `CONFIG_ENABLE_WAV=y`, verify WAV code compiles
- [ ] Test both configurations with all test suites

---

## Priority 4: LOW - Harden Ring Buffer (Design Improvement)

### Task 4.1: Make SPSC Contract Explicit
**File:** `components/audio_processor/audio_ringbuffer.c`

**Problem:**
- Ring buffer works for single-producer single-consumer (SPSC)
- Would break with multiple producers
- Contract not explicit in naming/documentation

**Implementation:**
- [ ] Rename functions to make SPSC explicit:
  - `audio_rb_init()` → `audio_rb_spsc_init()`
  - `audio_rb_write()` → `audio_rb_spsc_write()`
  - `audio_rb_read()` → `audio_rb_spsc_read()`
  - (Or keep names but add `_SPSC` suffix to file/header)
- [ ] Add documentation comments:
  ```c
  /**
   * Single-Producer Single-Consumer (SPSC) ring buffer.
   * 
   * IMPORTANT: This implementation is NOT thread-safe for multi-producer
   * or multi-consumer scenarios. Use only with:
   * - ONE producer task (audio_engine_task)
   * - ONE consumer context (BT A2DP callback)
   */
  ```
- [ ] Add runtime assert to verify single producer/consumer (optional)

**Subtasks:**
- [ ] Review all audio_rb_* function names
- [ ] Decide on naming convention (add _spsc suffix or rename file)
- [ ] Add comprehensive header documentation
- [ ] Update all call sites if function names change

**Testing:**
- [ ] Verify all tests compile with renamed functions
- [ ] No functional change expected

---

### Task 4.2: Consider Atomic Fences for Memory Ordering
**File:** `components/audio_processor/audio_ringbuffer.c`

**Problem:**
- Current implementation uses spinlock for head/tail/used_bytes
- memcpy happens outside critical section
- Memory visibility issues possible on different architectures

**Investigation:**
- [ ] Research ESP32 memory ordering guarantees
- [ ] Determine if atomic fences are necessary
- [ ] Check if ESP-IDF provides atomic primitives

**Options:**
- **Option A:** Keep current implementation (works on ESP32)
- **Option B:** Add atomic fences using `atomic_thread_fence(memory_order_release/acquire)`
- **Option C:** Use ESP-IDF atomic APIs if available

**Decision:**
- [ ] Choose option (recommend Option A unless targeting other architectures)
- [ ] Document memory ordering assumptions in code comments

**Low Priority:** Only implement if targeting non-ESP32 platforms or observing race conditions.

---

## Priority 5: LOW - Clean Up Stats Tracking

### Task 5.1: Define "Overrun" Semantics Clearly
**File:** `components/audio_processor/audio_processor.c`

**Problem:**
- `buffer_overruns` counter exists but isn't incremented
- Unclear what "overrun" means in ring buffer context

**Options:**
- **Option A:** Track "engine backpressure events" (free < chunk size)
- **Option B:** Track "partial writes" (write < produced)
- **Option C:** Track "dropped bytes" (if ever choosing to drop instead of block)
- **Option D:** Remove overrun counter entirely (simplest)

**Decision:**
- [ ] Choose semantic definition
- [ ] Rename counter to match semantics:
  - `buffer_overruns` → `engine_backpressure_events`
  - or `partial_writes`
  - or `dropped_bytes`

**Implementation:**
- [ ] Locate where overrun should be incremented
- [ ] Add increment logic based on chosen semantic
- [ ] Update STATUS command output to reflect new name
- [ ] Update stats documentation

**Subtasks:**
- [ ] Review `audio_engine_task()` for backpressure points
- [ ] Add counter increment at appropriate location
- [ ] Verify counter is reported in STATUS/diagnostics

**Testing:**
- [ ] Trigger backpressure condition, verify counter increments
- [ ] STATUS command shows accurate overrun/backpressure count

---

### Task 5.2: Add Watermark Sanity Checks
**File:** `components/audio_processor/audio_processor.c`

**Problem:**
- No validation that watermarks are configured correctly
- Could mis-configure LOW > HIGH or watermarks > capacity

**Implementation:**
- [ ] Locate ring buffer initialization in `audio_processor_init()` or `audio_processor_start()`
- [ ] Add assertions/checks:
  ```c
  assert(AUDIO_RB_LOW_WATERMARK < AUDIO_RB_HIGH_WATERMARK);
  assert(AUDIO_RB_HIGH_WATERMARK < AUDIO_RB_CAPACITY);
  assert(AUDIO_RB_LOW_WATERMARK > 0);
  ```
- [ ] Add ESP_LOGE log if validation fails
- [ ] Consider making watermarks configurable via Kconfig

**Subtasks:**
- [ ] Find watermark constant definitions
- [ ] Add compile-time or runtime validation
- [ ] Add error handling if validation fails

**Testing:**
- [ ] Verify firmware boots with correct watermarks
- [ ] Test with intentionally invalid watermarks (should fail assert/log error)

---

## Testing & Validation

### Overall Testing Strategy
After completing each priority group:

**Host Tests:**
- [ ] Run `make test` in test/host_test/
- [ ] Verify 33/33 tests pass
- [ ] Check for new failures

**Component Tests:**
- [ ] Build test/test_app
- [ ] Flash to ESP32
- [ ] Verify 46/46 tests pass

**Integration Tests:**
- [ ] Build test/test_app_audio
- [ ] Flash to ESP32
- [ ] Verify 29/29 tests pass

**Manual Testing:**
- [ ] Flash main firmware
- [ ] Test SYNTH mode switching via UART
- [ ] Test SPANLOG command output
- [ ] Verify no WAV commands exist (or are gated)
- [ ] Monitor for unexpected behavior

**Regression Testing:**
- [ ] All 390 tests from REMOVE_PLAY project still pass
- [ ] No new compiler warnings
- [ ] No new memory leaks (heap, stack)

---

## Documentation Updates

### Task: Update Architecture Documentation
**Files:**
- `ARCH.md`
- `README.md`
- `code_review/CODE_REVIEW7.md` (mark as addressed)

**Updates Required:**
- [ ] Document SYNTH mode priority fix
- [ ] Document span log usage
- [ ] Update WAV status (removed or gated)
- [ ] Update ring buffer SPSC contract
- [ ] Add troubleshooting section using SPANLOG command

---

## Progress Tracking

### Priority 1 (Critical):
- [x] Task 1.1: Fix get_active_source() priority (COMPLETE - commit 2dce8d77)
- [x] Task 1.2: Review audio_processor_start() I2S behavior (COMPLETE - Option A chosen)

### Priority 2 (Medium):
- [ ] Task 2.1: Wire span log into audio engine
- [ ] Task 2.2: Add SPANLOG CLI command
- [ ] Task 3.1: Decide WAV removal strategy
- [ ] Task 3.2: Remove WAV API (Option A)
- [ ] Task 3.3: Gate WAV behind config (Option B)

### Priority 3 (Low):
- [ ] Task 4.1: Make SPSC contract explicit
- [ ] Task 4.2: Consider atomic fences
- [ ] Task 5.1: Define overrun semantics
- [ ] Task 5.2: Add watermark sanity checks

### Testing:
- [ ] All host tests pass
- [ ] All component tests pass
- [ ] All integration tests pass
- [ ] Manual UART testing complete

### Documentation:
- [ ] ARCH.md updated
- [ ] README.md updated
- [ ] CODE_REVIEW7.md marked as addressed

---

## Success Criteria

**Phase Complete When:**
- ✅ SYNTH mode works correctly (Priority 1 complete)
- ✅ Span log is functional and useful (Priority 2 complete)
- ✅ WAV scaffolding removed or gated (Priority 2 complete)
- ✅ All 390 tests pass
- ✅ No new compiler warnings
- ✅ Manual testing confirms all fixes work
- ✅ Documentation updated

**Stretch Goals:**
- ✅ Ring buffer SPSC contract explicit (Priority 3)
- ✅ Stats tracking clarified (Priority 3)
- ✅ Watermark validation added (Priority 3)

---

## Notes

**From ChatGPT Review:**
> "Fixing SYNTH selection + adding span logging will make this dramatically easier to diagnose."

**Key Insight:**
Most likely truncation symptoms now (post-PLAY removal):
- Producer not filling ring fast enough → underruns
- I2S reads timing out → ring starves
- Source selection stuck on I2S when user thinks it's SYNTH (currently happening)

**Recommended Order:**
1. Fix SYNTH bug first (user-visible)
2. Wire span log (debugging visibility)
3. Clean WAV scaffolding (code hygiene)
4. Polish ring buffer/stats (architectural improvements)

---

**Last Updated:** 2026-02-09  
**Estimated Effort:** 8-12 hours (Priority 1-2), 4-6 hours (Priority 3)
