# Code Review 2602101453 - TODO List

**Review Date:** February 10, 2026  
**Reviewer:** ChatGPT 5.2  
**Status:** Not Started

---

## Priority Legend
- **P0:** Critical - Must fix (safety/correctness issues)
- **P1:** High Priority - Should fix soon (bugs, data races)
- **P2:** Maintainability - Future-proofing and code health
- **Feature:** New functionality requested during review

---

## P0 Tasks (Critical - Must Fix)

### P0.1: Replace vTaskDelete() with Cooperative Shutdown

**Risk:** Deadlock, resource leaks, state corruption  
**Location:** `components/audio_processor/audio_processor.c`

#### Subtasks:

- [ ] **P0.1.1:** Add shutdown infrastructure in `audio_processor_state.c`
  - [ ] Add `static volatile bool s_engine_stop_requested;`
  - [ ] Add `static EventGroupHandle_t s_engine_events;` (optional but robust)
  - [ ] Define event bits: `ENGINE_RUNNING_BIT`, `ENGINE_STOPPED_BIT`
  - [ ] Initialize event group in `audio_processor_init()` or first start

- [ ] **P0.1.2:** Modify `audio_processor_start()`
  - [ ] Clear `s_engine_stop_requested = false;` before creating task
  - [ ] Create task as currently done
  - [ ] (Optional) Wait for `ENGINE_RUNNING_BIT` with timeout for robustness
  - [ ] Return error if task doesn't signal running within timeout

- [ ] **P0.1.3:** Modify `audio_processor_stop()`
  - [ ] Set `s_engine_stop_requested = true;`
  - [ ] Call `xTaskNotifyGive(s_audio_engine_task_handle);` to wake task immediately
  - [ ] Wait for `ENGINE_STOPPED_BIT` with bounded timeout (e.g., 500ms)
  - [ ] If timeout, log error but continue (don't leak handle)
  - [ ] Set `s_audio_engine_task_handle = NULL;`
  - [ ] Remove current `vTaskDelete(s_audio_engine_task_handle);` call

- [ ] **P0.1.4:** Modify `audio_engine_task()` main loop
  - [ ] At top of loop: check `if (s_engine_stop_requested) break;`
  - [ ] Also check `ulTaskNotifyTake(pdTRUE, 0)` to wake fast on stop request
  - [ ] Consider combining: `if (s_engine_stop_requested || ulTaskNotifyTake(...)) break;`

- [ ] **P0.1.5:** Modify `audio_engine_task()` cleanup/exit
  - [ ] After breaking from loop, free `chunk_buf` allocation
  - [ ] Set `ENGINE_STOPPED_BIT` in event group
  - [ ] Call `vTaskDelete(NULL)` to self-delete (NOT external handle)
  - [ ] Ensure cleanup happens even on error paths

- [ ] **P0.1.6:** Testing
  - [ ] Write unit test for cooperative shutdown (if feasible in host environment)
  - [ ] Test start/stop cycles on ESP32 (no leaks, no hangs)
  - [ ] Test stop timeout path (modify code temporarily to force timeout)
  - [ ] Verify no deadlocks under stress (rapid start/stop cycles)

- [ ] **P0.1.7:** Documentation
  - [ ] Add WHY comment explaining cooperative shutdown in code
  - [ ] Document shutdown timeout value choice
  - [ ] Update any architecture docs if they mention task lifecycle

---

## P1 Tasks (High Priority - Should Fix Soon)

### P1.1: Guard bt_audio_data_callback() Against Invalid Length

**Risk:** Signed/unsigned conversion bugs, potential buffer overrun  
**Location:** `components/bt_streaming_manager/bt_streaming_manager.c` (or similar)

#### Subtasks:

- [ ] **P1.1.1:** Add length validation at function entry
  - [ ] Check `if (len <= 0) return 0;` at top of `bt_audio_data_callback()`
  - [ ] Log warning if `len == 0` (should never happen)
  - [ ] Log error if `len < 0` (definitely a bug upstream)

- [ ] **P1.1.2:** Clean up signed/unsigned arithmetic
  - [ ] Cast `len` to `size_t` once: `size_t req = (size_t)len;`
  - [ ] Use `req` throughout function instead of `len`
  - [ ] Update comparisons: `bytes_read < req`, `(req - bytes_read)`, etc.
  - [ ] Verify `memset()` calls use `size_t` arguments

- [ ] **P1.1.3:** Testing
  - [ ] Add unit test with `len = 0` (should return immediately)
  - [ ] Add unit test with `len = -1` (should return immediately, log error)
  - [ ] Verify normal operation with typical positive lengths unchanged

- [ ] **P1.1.4:** Review similar patterns
  - [ ] Search codebase for other callbacks with `int32_t len` parameters
  - [ ] Apply same hardening pattern where applicable

---

### P1.2: Add Synchronization for bt_streaming_info_t Stats

**Risk:** Torn reads, inconsistent diagnostic data  
**Location:** `components/bt_streaming_manager/bt_streaming_manager.c`

#### Subtasks:

- [ ] **P1.2.1:** Choose synchronization approach
  - [ ] Option A: Add `portMUX_TYPE` spinlock for stats access
  - [ ] Option B: Double-buffer with sequence counter (lock-free read)
  - [ ] Decision: ___________ (recommend Option A for simplicity)

- [ ] **P1.2.2:** Implement Option A (spinlock approach)
  - [ ] Add `static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;`
  - [ ] Wrap all `s_streaming_info.*` writes in `portENTER_CRITICAL(&s_stats_lock);` / `portEXIT_CRITICAL(&s_stats_lock);`
  - [ ] Wrap `bt_get_streaming_info()` memcpy in same lock
  - [ ] Keep critical sections minimal (just the struct access)

- [ ] **P1.2.3:** OR Implement Option B (double-buffer approach)
  - [ ] Add `static bt_streaming_info_t s_streaming_info_shadow;`
  - [ ] Add `static volatile uint32_t s_stats_sequence;`
  - [ ] Writer: increment seq, write shadow, increment seq again
  - [ ] Reader: retry loop checking seq before/after copy
  - [ ] More complex but lock-free for reader

- [ ] **P1.2.4:** Review other stats structures
  - [ ] Search for similar diagnostic/stats structs updated from multiple contexts
  - [ ] Apply same pattern for consistency

- [ ] **P1.2.5:** Testing
  - [ ] Stress test: rapid STATUS queries during heavy streaming
  - [ ] Verify no torn reads (fields consistent with each other)
  - [ ] Measure performance impact (should be negligible)

---

## P2 Tasks (Maintainability / Future-Proofing)

### P2.1: Clean Up components/components/ Directory Pollution

**Risk:** Confusion, slow tools, accidental edits to vendored code  
**Location:** Repository root structure

#### Subtasks:

- [ ] **P2.1.1:** Investigate what `components/components/` is
  - [ ] Determine if it's accidentally vendored ESP-IDF
  - [ ] Check if it's referenced in CMakeLists.txt
  - [ ] Check if build actually uses it or relies on `IDF_PATH`

- [ ] **P2.1.2:** Option A - Remove if accidental
  - [ ] Verify build works with only `IDF_PATH` ESP-IDF
  - [ ] Add to `.gitignore` to prevent re-committing
  - [ ] Delete `components/components/` directory
  - [ ] Test full clean build

- [ ] **P2.1.3:** Option B - Isolate if intentional
  - [ ] Move to clearly-named directory (e.g., `third_party/esp-idf/`)
  - [ ] Update build to reference new location
  - [ ] Add README explaining why it's vendored
  - [ ] Ensure clear boundary between "our code" and "vendored code"

- [ ] **P2.1.4:** Documentation
  - [ ] Document dependency management policy
  - [ ] Update build instructions if changed
  - [ ] Add note to README about ESP-IDF version requirements

---

### P2.2: Split/Clean ARCH.md to Remove Obsolete Sections

**Risk:** Contributor confusion, documentation drift  
**Location:** `esp_bt_audio_source/ARCH.md`

#### Subtasks:

- [ ] **P2.2.1:** Audit current ARCH.md
  - [ ] List all obsolete sections (WAV, SPIFFS, play_manager)
  - [ ] Identify what's still current and accurate
  - [ ] Check for other stale content

- [ ] **P2.2.2:** Option A - Split into current + historical
  - [ ] Create `ARCH.md` (current only)
  - [ ] Create `ARCH_HISTORICAL.md` (obsolete sections)
  - [ ] Add clear header in historical doc: "OBSOLETE - for reference only"
  - [ ] Cross-link between documents

- [ ] **P2.2.3:** Option B - Delete obsolete sections
  - [ ] Remove WAV playback sections
  - [ ] Remove SPIFFS references
  - [ ] Remove play_manager references
  - [ ] Git history preserves deleted content if needed later

- [ ] **P2.2.4:** Update current architecture documentation
  - [ ] Ensure 3-source model (I2S, synth, silence) is clearly documented
  - [ ] Reference `main/README.md` and `docs/FS.md` as source of truth
  - [ ] Add "last updated" dates to each major section

- [ ] **P2.2.5:** Decision: Choose Option _____ (recommend B - delete obsolete)

---

## Feature Tasks

### F1: Implement BEEP Priority Mode

**Goal:** BEEP should preempt and suspend active source (I2S or SYNTH), then restore it  
**Requirements:**
- Mutual exclusion: I2S and SYNTH never both active
- BEEP stops active source, drops buffered audio, plays beep, restores source
- No audio buffered/saved during BEEP

---

#### F1.1: Remove SYNTH Toggling from Command Handler

**Location:** `components/command_interface/cmd_handlers_audio.c`

- [ ] **F1.1.1:** Remove synth mode manipulation from `cmd_handle_beep()`
  - [ ] Delete: `bool _prev_synth = audio_processor_is_synth_mode_enabled();`
  - [ ] Delete: `audio_processor_set_synth_mode(true);`
  - [ ] Delete: `if (!_prev_synth) audio_processor_set_synth_mode(false);`
  - [ ] Keep only: validation checks + `audio_processor_beep_tone()` call + response mapping

- [ ] **F1.1.2:** Simplify error handling
  - [ ] Keep connection state checks
  - [ ] Keep beep-active busy check
  - [ ] Map `ESP_ERR_INVALID_STATE` from beep_tone to more specific error if possible

- [ ] **F1.1.3:** Testing
  - [ ] Verify BEEP command still validates correctly
  - [ ] Verify error responses unchanged
  - [ ] Check that synth state is NOT modified by command handler

---

#### F1.2: Implement Source Preemption in audio_processor_beep_tone()

**Location:** `components/audio_processor/audio_processor_beep.c`

- [ ] **F1.2.1:** Add restore state variables
  - [ ] In `audio_processor_state.c`: Add `static bool s_beep_restore_synth;`
  - [ ] In `audio_processor_state.c`: Add `static bool s_beep_restore_i2s;`
  - [ ] In `audio_processor_internal.h`: Declare both externally

- [ ] **F1.2.2:** Replace I2S rejection with preemption
  - [ ] Remove: `if (i2s_manager_is_running()) return ESP_ERR_INVALID_STATE;`
  - [ ] Add: Snapshot active source state
    ```c
    bool was_synth = s_force_synth;
    bool was_i2s = i2s_manager_is_running();
    ```

- [ ] **F1.2.3:** Enforce mutual exclusion invariant
  - [ ] If both `was_synth` and `was_i2s` are true: log error
  - [ ] Decide deterministic priority (recommend: SYNTH wins, treat as synth-only)
  - [ ] Adjust `was_i2s` based on decision: `if (was_synth && was_i2s) was_i2s = false;`

- [ ] **F1.2.4:** Stop active source
  - [ ] If `was_i2s`: call `i2s_manager_stop();`
  - [ ] If `was_synth`: set `s_force_synth = false;` (optionally reset synth generator state)

- [ ] **F1.2.5:** Set restore flags
  - [ ] `s_beep_restore_synth = was_synth;`
  - [ ] `s_beep_restore_i2s = was_i2s;`

- [ ] **F1.2.6:** Remove state-breaking lines
  - [ ] Delete: `s_force_synth = false;`
  - [ ] Delete: `s_keepalive_armed = false;`
  - [ ] (Manage keepalive independently if needed)

- [ ] **F1.2.7:** Proceed with beep
  - [ ] Continue to `beep_manager_play()` as currently done
  - [ ] Return ESP_OK on success

---

#### F1.3: Implement Source Restoration When Beep Ends

**Location:** `components/audio_processor/audio_processor_beep.c`

- [ ] **F1.3.1:** Modify `audio_processor_beep_done_cb()`
  - [ ] Remove: `if (bt_manager_is_a2dp_connected()) { s_keepalive_armed = true; s_force_synth = true; }`
  - [ ] Replace with restore logic:
    ```c
    if (s_beep_restore_synth) {
        s_force_synth = true;
    } else if (s_beep_restore_i2s && s_is_running) {
        i2s_manager_start();
    }
    ```

- [ ] **F1.3.2:** Clear restore flags
  - [ ] `s_beep_restore_synth = false;`
  - [ ] `s_beep_restore_i2s = false;`

- [ ] **F1.3.3:** Testing
  - [ ] Test: SYNTH ON → BEEP → verify SYNTH resumes
  - [ ] Test: I2S ON → BEEP → verify I2S resumes
  - [ ] Test: Both OFF → BEEP → verify silence after
  - [ ] Test: Ensure no unexpected source activation

---

#### F1.4: Drain Ring Buffer on Beep Start

**Location:** `components/audio_processor/audio_processor_read.c`

- [ ] **F1.4.1:** Add drain flag
  - [ ] In `audio_processor_state.c`: Add `static volatile bool s_drop_ring_audio;`
  - [ ] In `audio_processor_internal.h`: Declare extern

- [ ] **F1.4.2:** Set drain flag in `audio_processor_beep_tone()`
  - [ ] After stopping source, before starting beep: `s_drop_ring_audio = true;`

- [ ] **F1.4.3:** Implement drain logic in `audio_processor_read()`
  - [ ] At function entry, check `if (s_drop_ring_audio)`
  - [ ] Allocate small stack buffer (e.g., 512 bytes)
  - [ ] Loop: `while (audio_rb_available() > 0)` read and discard (bounded iterations)
  - [ ] Clear flag: `s_drop_ring_audio = false;`
  - [ ] Return silence for this callback (fill buffer with zeros)

- [ ] **F1.4.4:** Testing
  - [ ] Verify ring drains immediately on beep start
  - [ ] Verify beep feels "immediate" (no old audio first)
  - [ ] Verify no SPSC invariant violations
  - [ ] Check bounded iteration prevents infinite loop

---

#### F1.5: Make Audio Engine Return Silence During Beep

**Location:** `components/audio_processor/audio_processor.c`

- [ ] **F1.5.1:** Modify `get_active_source()`
  - [ ] Add highest-priority check at top of function:
    ```c
    if (beep_overlay_is_active() || s_beep_remaining_bytes > 0) {
        return AUDIO_SOURCE_SILENCE;
    }
    ```
  - [ ] This executes before I2S/SYNTH checks

- [ ] **F1.5.2:** Verify beep overlay still applies
  - [ ] Confirm `beep_overlay_fill()` is called in `produce_audio_chunk()`
  - [ ] Beep will mix over silence, effectively playing pure beep tone

- [ ] **F1.5.3:** Testing
  - [ ] Verify beep is NOT mixed with I2S or SYNTH
  - [ ] Verify clean beep tone output
  - [ ] Check SPANLOG shows source = SILENCE during beep

---

#### F1.6: Enforce I2S/SYNTH Mutual Exclusion

**Location:** `components/audio_processor/audio_processor.c`

- [ ] **F1.6.1:** Modify `audio_processor_set_synth_mode(bool enable)`
  - [ ] If enabling synth (`enable == true`):
    - [ ] Check `if (i2s_manager_is_running())` → call `i2s_manager_stop();`
    - [ ] Set `s_force_synth = true;`
  - [ ] If disabling synth (`enable == false`):
    - [ ] Set `s_force_synth = false;`
    - [ ] Check `if (s_is_running)` → call `i2s_manager_start();`

- [ ] **F1.6.2:** Add safety check in `audio_processor_start()`
  - [ ] If `s_force_synth` is already true, don't start I2S
  - [ ] Otherwise start I2S as currently done
  - [ ] Log source selection decision

- [ ] **F1.6.3:** Testing
  - [ ] Test: `SYNTH ON` stops I2S
  - [ ] Test: `SYNTH OFF` restarts I2S (if processor running)
  - [ ] Test: Start with SYNTH already on → I2S doesn't start
  - [ ] Verify no scenario where both I2S and SYNTH are running simultaneously

- [ ] **F1.6.4:** Documentation
  - [ ] Add WHY comment explaining mutual exclusion
  - [ ] Update architecture docs to reflect source exclusivity
  - [ ] Document user-visible behavior change (if any)

---

#### F1.7: Integration Testing for BEEP Priority

- [ ] **F1.7.1:** Test matrix
  - [ ] Initial state: I2S only
    - [ ] Issue BEEP → I2S stops, beep plays, I2S resumes
  - [ ] Initial state: SYNTH only
    - [ ] Issue BEEP → SYNTH stops, beep plays, SYNTH resumes
  - [ ] Initial state: Silence (both off)
    - [ ] Issue BEEP → beep plays over silence, remains silent after
  - [ ] Initial state: Both on (should be impossible after F1.6)
    - [ ] Verify invariant enforced

- [ ] **F1.7.2:** Edge cases
  - [ ] Rapid BEEP commands (one during another)
  - [ ] BEEP during source transition (SYNTH ON command while I2S running)
  - [ ] BEEP interrupted by disconnect
  - [ ] BEEP during startup/shutdown

- [ ] **F1.7.3:** Audio quality verification
  - [ ] Listen: no old audio before beep
  - [ ] Listen: clean beep tone (not mixed with source)
  - [ ] Listen: source resumes cleanly after beep
  - [ ] Check for clicks/pops at transitions

---

### F2: Support Standard I2S Configuration

**Goal:** Configure ESP32 for "most common" I2S profile  
**Profile:** Philips I2S, 48kHz stereo, 16-bit samples in 32-bit slots, ESP32 as RX slave

#### Subtasks:

- [ ] **F2.1:** Document target configuration
  - [ ] Format: Philips I2S standard (1-bit delay)
  - [ ] Sample rate: 48,000 Hz
  - [ ] Channels: Stereo (2)
  - [ ] Bit depth: 16-bit samples
  - [ ] Slot width: 32-bit slots
  - [ ] BCLK: 64×Fs = 3.072 MHz
  - [ ] WS polarity: WS low = Left, WS high = Right
  - [ ] Role: ESP32 slave RX, external master (Beaglebone)

- [ ] **F2.2:** Verify current `i2s_manager.c` configuration
  - [ ] Check if slot format is already Philips I2S (expect: yes, using default macro)
  - [ ] Check sample rate default (should be 48kHz based on history)
  - [ ] Check bit depth handling (16-bit)
  - [ ] Check slot width configuration

- [ ] **F2.3:** Update configuration if needed
  - [ ] Set sample rate to 48kHz in defaults
  - [ ] Ensure 16-bit sample / 32-bit slot configuration
  - [ ] Verify SLAVE mode for RX channel
  - [ ] Verify Philips I2S framing selected

- [ ] **F2.4:** Add runtime configuration capability (optional)
  - [ ] Extend `I2S_CONFIG` command to support format selection?
  - [ ] Or keep format fixed for V1, defer to future work

- [ ] **F2.5:** Beaglebone source testing
  - [ ] Configure BBGW I2S output to match profile
  - [ ] Verify LRCLK = 48 kHz on scope
  - [ ] Verify BCLK = 3.072 MHz on scope
  - [ ] Verify 1-bit delay (Philips standard)
  - [ ] Test audio capture and playback through BT

- [ ] **F2.6:** Documentation
  - [ ] Document supported I2S configuration in README
  - [ ] Add notes for future multi-profile support (44.1k, 24-bit, etc.)
  - [ ] Document how to configure external I2S master to match

---

## Additional Observations (Lower Priority)

### A1: Engine Tick + I2S Timeout Interplay

**Location:** `components/audio_processor/i2s_source.c`

- [ ] Review `i2s_source_fill()` timeout vs engine tick timing
- [ ] Document timeout/tick coupling or make relationship explicit
- [ ] Consider deriving one from the other for robustness
- [ ] Test under BT stack load / task jitter conditions

---

### A2: Reduce #ifndef UNIT_TEST Cognitive Load

**Location:** Various test-excluded code paths

- [ ] Audit `#ifndef UNIT_TEST` usage across codebase
- [ ] Consider compiling task code in host tests with simulated scheduler
- [ ] Ensure critical paths (task lifecycle, cooperative shutdown) are testable
- [ ] Document which code is platform-only vs testable

---

## Progress Tracking

- **P0 Tasks:** 0 / 7 subtasks complete (0%)
- **P1 Tasks:** 0 / 9 subtasks complete (0%)
- **P2 Tasks:** 0 / 9 subtasks complete (0%)
- **Feature Tasks:** 0 / 27 subtasks complete (0%)

---

## Notes

- Tasks may be reordered based on dependencies
- Recommended execution order: P0 → F1 (BEEP) → P1 → F2 (I2S) → P2
- Each major task should include: implementation → testing → documentation
- Update memory.md as tasks are completed with timestamps and results

---

**Last Updated:** 2026-02-10 (initial creation)
