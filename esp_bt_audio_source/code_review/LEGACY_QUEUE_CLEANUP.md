# LEGACY_QUEUE_CLEANUP TODO

Purpose: fully remove the legacy multi‑producer `audio_queue` path now that the SPSC ring buffer is the active architecture. This list assumes the ring buffer path is stable and tests already cover it.

---

## 0) Prep and Scoping
- [x] Identify all queue references to remove.
  - [x] `rg -n "audio_queue|audio_chunk_" esp_bt_audio_source/components esp_bt_audio_source/main esp_bt_audio_source/test`
  - [x] Noted non-audio “queue” usages (ignored for this cleanup).
- [!] Confirm the ring buffer path is the only runtime audio data path used in production.
  - Findings (queue is still active in runtime paths):\n
    - `esp_bt_audio_source/components/audio_processor/i2s_manager.c` still enqueues via `audio_chunk_enqueue_bytes()`.\n
    - `esp_bt_audio_source/components/audio_processor/play_manager.c` still allocates/enqueues blocks.\n
    - `esp_bt_audio_source/components/audio_processor/beep_manager.c` still uses queue enqueue + diagnostics.\n
    - `esp_bt_audio_source/components/audio_processor/audio_processor_read.c` still dequeues `audio_chunk_t`.\n
    - `esp_bt_audio_source/components/bt_manager/bt_manager.c` still references `audio_chunk_t` in the read path.\n
  - Action: ring buffer is present but legacy queue is still in active use; cleanup requires source migration + API removal.\n
- [x] Decide whether to keep a temporary compatibility flag (e.g., `CONFIG_AUDIO_QUEUE_LEGACY`) for a transitional build.
  - Decision: **No compatibility flag**. Proceed with a single, full removal.

---

## 1) Public API Cleanup (audio_processor)
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

## 2) Remove the Queue Module
**Goal:** Delete the legacy queue implementation and its build hooks.
- [x] Delete `esp_bt_audio_source/components/audio_processor/audio_queue.c`
- [x] Delete `esp_bt_audio_source/components/audio_processor/include/audio_queue.h`
- [x] Update `esp_bt_audio_source/components/audio_processor/CMakeLists.txt`
  - [x] Remove `audio_queue.c` from sources (already absent)
- [ ] Remove any `audio_queue` references in `Kconfig` or build notes (if any).

---

## 3) Source Managers: Remove Queue Enqueue Paths
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

## 4) Audio Processor Diagnostics
**Goal:** Replace queue-based diagnostics with ring-buffer equivalents.
- [ ] Update `esp_bt_audio_source/components/audio_processor/audio_processor_diag.c`
  - [ ] Replace `queue_free` with ring‑buffer free/used stats
  - [ ] Remove tag‑queue dump that depends on queue descriptors
  - [ ] If needed, replace tag dump with span log snapshots

---

## 5) Tests and Stubs
**Goal:** Remove tests that rely on the queue or rework them to ring‑buffer fill.

### Host/unit tests
- [ ] `esp_bt_audio_source/test/test_play_manager/main/CMakeLists.txt`
  - [ ] Remove `audio_queue.c` from build
- [ ] `esp_bt_audio_source/test/test_play_manager/main/test_play_manager.c`
  - [ ] Replace queue-saturation tests with ring‑buffer or `*_source_fill()` tests
  - [ ] Remove direct `audio_chunk_*` usage
- [ ] `esp_bt_audio_source/test/host_test` (if any queue-specific tests exist)
  - [ ] Remove or rework to ring‑buffer tests

### Device test stubs
- [x] `esp_bt_audio_source/test/test_app/main/audio_processor_stub.c`
  - [x] Remove queue APIs from stubs or replace with ring‑buffer equivalents (stubs removed from build)
- [x] `esp_bt_audio_source/test/test_app/main/audio_processor_beep_stub.c`
  - [x] Remove `audio_processor_dump_tag_queue` stub if no longer used (stub removed from build)
- [ ] Any other stubs that reference `audio_chunk_t` or `audio_queue`

---

## 6) Documentation Updates
**Goal:** Align docs with ring‑buffer‑only reality.
- [ ] `esp_bt_audio_source/README.md`
  - [ ] Remove any mention of `audio_queue` as a runtime path
- [ ] `esp_bt_audio_source/main/README.md`
  - [ ] Remove or update any references to queue or chunk pool
- [ ] `esp_bt_audio_source/code_review/CODE_REVIEW6_TODO.md`
  - [ ] Mark Phase 6 cleanup tasks complete once queue is removed

---

## 7) Build/CI Verification
**Goal:** Ensure no residual references remain and tests still pass.
- [ ] `rg -n "audio_queue|audio_chunk_" esp_bt_audio_source` returns zero matches (excluding historical logs if desired)
- [ ] Build firmware (`idf.py build`) if this repo currently expects it
- [x] Run host tests (`test/host_test` CTest suite)
- [x] Run key device suites if hardware is available (test_app, test_app2, test_app_audio)

---

## 8) Optional Transitional Guard (if you want a soft landing)
**Goal:** Catch remaining dependencies quickly while allowing rollback.
- [ ] Add `CONFIG_AUDIO_QUEUE_LEGACY` (default off)
- [ ] Wrap all queue code behind the flag
- [ ] Add a build-time error when `CONFIG_AUDIO_QUEUE_LEGACY` is off but queue symbols are still referenced

---

## 9) Cleanup Checklist (Final “No Queue” Confirmation)
- [ ] No `audio_queue.c/.h` in tree
- [ ] No `audio_chunk_*` symbols in build logs
- [ ] No queue-only APIs in public headers
- [ ] No queue-based tests
- [ ] Docs updated to ring‑buffer only
