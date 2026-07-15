# Phase 7 Implementation Plan

## Overview

Phase 7 restructures the radio module to be single-owner and join-safe. The changes touch `radio.c`, `radio.h`, and the test file `test_radio_lifecycle.c`.

## Current State

- `radio_play_sync()` and `radio_stop_sync()` are callable directly (public API)
- Workers (`stream_task`, `decoder_task`) set exit bits but no start bits
- `radio_stop_sync()` has a FAULTED fast-free branch that frees the session early
- Waits use `vTaskDelay()` — not interruptible
- Prebuffer state uses `volatile` without synchronization
- `radio_get_status()` acquires nested locks (s_control_mtx → s_mtx → s_pcm_mtx)
- Compressed ring writes don't check backpressure

## Implementation Order (matching plan's commit groupings)

### Commit 7a: Radio lifecycle serialization (7.1, 7.2, 7.3, 7.4)

**Changes to radio.c:**

1. Add `RADIO_EVT_STREAM_STARTED` and `RADIO_EVT_DECODER_STARTED` event bits
2. Restructure `stream_task()` to set STARTED bit, use single `exit:` label
3. Restructure `decoder_task()` similarly  
4. `radio_play_sync()`: wait for both STARTED bits before marking RUNNING
5. Remove FAULTED fast-free branch from `radio_stop_sync()`
6. Add `session_all_exited()` helper, use it in `radio_stop_sync()`
7. Add `RADIO_STATE_FAULTED_JOIN_PENDING` enum value (or reuse FAULTED with different semantics)
8. On stop timeout: set FAULTED, retain session in memory (don't free)

**Changes to radio.h:**
- Add `RADIO_STATE_FAULTED_JOIN_PENDING` to state enum (or document FAULTED behavior)
- Document that `radio_play_sync()` / `radio_stop_sync()` are internal

**Changes to test_radio_lifecycle.c:**
- Update fault tests to verify session not freed early
- Test that FAULTED blocks restart
- Test that stop waits for both EXITED bits

### Commit 7b: Interruptible waits & HTTP status (7.5, 7.6)

**Changes to radio.c:**

1. In `stream_task()`: replace `vTaskDelay(backoff)` with notification wait
2. When stop requested, notify stream task via `xTaskNotifyGive()`
3. Set HTTP read timeout on client config
4. Validate HTTP status code after `esp_http_client_open()`
5. Change `resolve_url()` to return `esp_err_t` and handle failure
6. Add `RADIO_ERR_HTTP_STATUS` error code

**Changes to radio.h:**
- Add `RADIO_ERR_HTTP_STATUS` to error enum

### Commit 7c: Stream loop & decoder fixes (7.7, 7.8, 7.9)

**Changes to radio.c:**

1. In `stream_task()`: check ring free space before reading, exert backpressure
2. In `decoder_task()`: validate `raw.consumed <= raw.len`, remove forced progress
3. Use decoder error categories properly
4. Protect prebuffer state with `s_pcm_mtx` instead of `volatile`
5. Fix prebuffer bytes calculation

**Changes to test_radio_lifecycle.c:**
- Add test for decoder progress validation
- Verify prebuffer persistence under NVS failure

### Commit 7d: Status snapshot (7.10)

**Changes to radio.c:**
1. Add `s_status_mtx` + `radio_status_t s_status` snapshot
2. Workers/owner publish status under the snapshot mutex
3. `radio_get_status()` reads from snapshot without nested acquisition

**Changes to radio.h:**
- Minor: document snapshot semantics

### Commit 7e: Tests (7.11)

**Changes to test_radio_lifecycle.c:**
- Add tests for each allocation failure path
- Test stream/decoder task creation failure separately
- Test timeout during stop
- Verify no double-free, no use-after-free

## Files Modified

- `components/radio/radio.c` — main implementation
- `components/radio/include/radio.h` — new enums, API docs
- `test/host_test/test_radio_lifecycle.c` — new tests, updated tests

## Risk Assessment

- **High risk**: worker restructuring (7.3). The exit label change is mechanical but touches all early-return paths.
- **Medium risk**: snapshot pattern (7.10). Changes lock semantics but improves correctness.
- **Low risk**: prebuffer fix (7.9), HTTP validation (7.6).

## Verification Strategy

After each commit grouping:
1. Host tests pass (ctest)
2. Device build compiles (idf.py build)
3. No ASan/UBSan warnings in host tests
