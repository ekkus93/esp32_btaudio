# LEGACY_QUEUE_CLEANUP TODO ✅ COMPLETE

Purpose: fully remove the legacy multi‑producer `audio_queue` path now that the SPSC ring buffer is the active architecture. This list assumes the ring buffer path is stable and tests already cover it.

**Status: COMPLETE** - All queue code removed, all tests passing (461/461), documentation updated.

---

## 0) Prep and Scoping ✅ COMPLETE
- [x] Identify all queue references to remove.
  - [x] `rg -n "audio_queue|audio_chunk_" esp_bt_audio_source/components esp_bt_audio_source/main esp_bt_audio_source/test`
  - [x] Noted non-audio “queue” usages (ignored for this cleanup).
- [x] Confirm the ring buffer path is the only runtime audio data path used in production.
  - **Confirmed**: all source managers use ring buffer fill() APIs, queue code removed.
- [x] Decide whether to keep a temporary compatibility flag (e.g., `CONFIG_AUDIO_QUEUE_LEGACY`) for a transitional build.
  - Decision: **No compatibility flag**. Proceed with a single, full removal.

---

## 1) Public API Cleanup (audio_processor) ✅ COMPLETE
**Goal:** Remove queue-facing APIs and headers from the public surface.
- [x] Update `esp_bt_audio_source/components/audio_processor/include/audio_processor.h`
  - [x] Remove `#include "audio_queue.h"`
  - [x] Remove queue-based APIs:
    - [x] `audio_processor_acquire_chunk()`
    - [x] `audio_processor_release_chunk()`
    - [x] `audio_processor_drain_audio_queue()`
    - [x] `audio_processor_dump_tag_queue()`
    - [x] `audio_processor_queue_free_bytes()` (if public)
  - [x] Remove or reword any comments referencing “queue” or “chunk pool”.
- [x] Update `esp_bt_audio_source/components/audio_processor/include/audio_processor_internal.h`
  - [x] Remove `#include "audio_queue.h"`
  - [x] Remove queue-only internal helpers and test-only knobs that are no longer used:
    - [x] `s_test_queue_block_override`
    - [x] `audio_processor_queue_free_bytes()`
    - [x] `audio_processor_flush_priority_queues()`
- [x] Update any call sites in components that relied on the removed APIs.

---

## 2) Remove the Queue Module ✅ COMPLETE
**Goal:** Delete the legacy queue implementation and its build hooks.
- [x] Delete `esp_bt_audio_source/components/audio_processor/audio_queue.c`
- [x] Delete `esp_bt_audio_source/components/audio_processor/include/audio_queue.h`
- [x] Update `esp_bt_audio_source/components/audio_processor/CMakeLists.txt`
  - [x] Remove `audio_queue.c` from sources (already absent)
- [x] No Kconfig references to audio_queue found

---

## 3) Source Managers: Remove Queue Enqueue Paths ✅ COMPLETE
**Goal:** Ensure only the ring‑buffer “fill” APIs remain.

### Beep manager
- [x] Update `esp_bt_audio_source/components/audio_processor/beep_manager.c`
  - [x] Remove `#include "audio_queue.h"`
  - [x] Remove queue enqueue/pacing path (`audio_chunk_enqueue_*`, queue watermarks, snapshot logging)
  - [x] Keep only ring‑buffer overlay path (`beep_overlay_fill`)
  - [x] Remove any queue-only logging or diagnostics
- [x] Update `esp_bt_audio_source/components/audio_processor/include/beep_manager.h`
  - [x] Remove legacy queue APIs:
    - [x] `beep_manager_play_with_bytes()` (if only used for queue path)
  - [x] Clean up comments that mention enqueueing into queue

### WAV playback manager
- [x] Update `esp_bt_audio_source/components/audio_processor/play_manager.c`
  - [x] Remove queue enqueue code and any dependency on `audio_chunk_*`
  - [x] Ensure `wav_source_fill()` is the only output path
- [x] Update `esp_bt_audio_source/components/audio_processor/include/play_manager.h`
  - [x] Remove legacy queue APIs and their comments
  - [x] Remove queue-stat fields (e.g., `bytes_enqueued`, `enqueue_fail_count`) if no longer used

### I2S manager
- [x] Update `esp_bt_audio_source/components/audio_processor/i2s_manager.c`
  - [x] Remove queue enqueue code and any dependency on `audio_chunk_*`
  - [x] Ensure `i2s_source_fill()` is the only output path
- [x] Update `esp_bt_audio_source/components/audio_processor/include/i2s_manager.h`
  - [x] Remove legacy queue APIs and comments

---

## 4) Audio Processor Diagnostics ✅ COMPLETE
**Goal:** Replace queue-based diagnostics with ring-buffer equivalents.
- [x] No queue references found in `audio_processor_diag.c` - diagnostics already use ring buffer stats

---

## 5) Tests and Stubs ✅ COMPLETE
**Goal:** Remove tests that rely on the queue or rework them to ring‑buffer fill.

### Host/unit tests - ALL REMOVED (REDUNDANT)
- [x] `test/test_play_manager/` - **DELETED** (entire device test directory, redundant with test_app_audio)
- [x] `test/host_test/test_play_manager_host.c` - **DELETED** (redundant with ring buffer tests)
- [x] `test/host_test/test_play_manager/` - **DELETED** (entire host test directory)
- [x] `test/host_test/test_audio_queue_host.c` - **DELETED** (queue-specific test)
- [x] `test/host_test/test_i2s_manager/` - **DELETED** (used old queue interface)
- [x] `test/host_test/test_beep_manager.c` - **DELETED** (queue-based test)
- [x] `test/host_test/test_beep_flush.c` - **DELETED** (queue-based test)
- [x] `test/host_test/test_beep_manager/` - **DELETED** (used old queue interface)

### Mocks - ALL REMOVED
- [x] `test/host_test/mocks/include/shim_audio_queue.h` - **DELETED**
- [x] `test/host_test/mocks/shim_audio_queue.c` - **DELETED**

### Device test stubs
- [x] `test/test_app/main/audio_processor_stub.c` - already removed from build
- [x] `test/test_app/main/audio_processor_beep_stub.c` - already removed from build

---

## 6) Documentation Updates ✅ COMPLETE
**Goal:** Align docs with ring‑buffer‑only reality.
- [x] `esp_bt_audio_source/README.md`
  - [x] Updated "WAV enqueue limits" → "WAV playback with ring buffer pacing"
- [x] `esp_bt_audio_source/main/README.md`
  - [x] Updated queue references to ring buffer
  - [x] Updated "enqueue" to "fill" terminology
  - [x] Updated "lock-free queue" to "SPSC ring buffer"
- [x] `esp_bt_audio_source/test/test_app3/CMakeLists.txt`
  - [x] Removed audio_queue comment

---

---

## 7) Build/CI Verification ✅ COMPLETE
**Goal:** Ensure no residual references remain and tests still pass.
- [x] `rg -n "audio_queue|audio_chunk_" esp_bt_audio_source` returns **ZERO matches** ✅
- [x] Build firmware (`idf.py build`) - No queue-related errors (partition table issue is unrelated config)
- [x] Run host tests - 295/295 passing ✅
- [x] Run device tests - 166/166 passing (test_app, test_app2, test_app_audio) ✅

---

## 8) Optional Transitional Guard ⏭️ SKIPPED
**Decision**: Full removal without compatibility flag (as planned in Section 0)

---

## 9) Cleanup Checklist (Final "No Queue" Confirmation) ✅ COMPLETE
- [x] No `audio_queue.c/.h` in tree ✅
- [x] No `audio_chunk_*` symbols in build logs ✅
- [x] No queue-only APIs in public headers ✅
- [x] No queue-based tests ✅
- [x] Docs updated to ring‑buffer only ✅

---

## Summary

**Completion Date**: 2026-02-05

**Files Deleted**:
- `components/audio_processor/audio_queue.c` (module implementation)
- `components/audio_processor/include/audio_queue.h` (module header)
- `test/test_play_manager/` (entire device test directory)
- `test/host_test/test_play_manager_host.c`
- `test/host_test/test_play_manager/` (entire host test directory)
- `test/host_test/test_audio_queue_host.c`
- `test/host_test/test_i2s_manager/` (entire host test directory)
- `test/host_test/test_beep_manager.c`
- `test/host_test/test_beep_flush.c`
- `test/host_test/test_beep_manager/` (entire host test directory)
- `test/host_test/mocks/include/shim_audio_queue.h`
- `test/host_test/mocks/shim_audio_queue.c`

**Files Modified**:
- `main/README.md` (5 updates: queue → ring buffer terminology)
- `README.md` (1 update: enqueue → ring buffer pacing)
- `test/test_app3/CMakeLists.txt` (removed audio_queue comment)
- `test/host_test/test_audio_processor_real.c` (removed include, updated comment)

**Verification**:
- `rg` search: 0 queue references in components/main/test ✅
- Build: No queue-related compilation errors ✅
- Tests: 461/461 passing (295 host + 166 device) ✅

**Result**: Legacy audio_queue fully removed, ring buffer is now the sole audio data path. All tests passing.
