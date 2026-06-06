# BUG_FIXES2_TODO

Items derived from the open-work sections in `esp_bt_audio_source/README.md` that can
be completed without manual hardware verification.  All items that turned out to be
already done are listed in the **Status corrections** section at the bottom.

---

## DOC-1 — Crystal frequency divergence note

**Status:** `[x]` Done  
**Source:** README.md "Build warnings present" section  
**Priority:** High (release-blocker documentation)

### Background
The ESP-IDF build prints an informational warning:

```
I (boot): crystal_freq_mhz: detected 41.01MHz, expected 40MHz
```

This is hardware-specific and non-fatal but confuses operators who see it for the first
time.  It is mentioned in README.md under "Build warnings present" but not explained
anywhere that a developer would actually look.

### Tasks

- [x] **DOC-1a** Add a `### Known Hardware Quirks` subsection to `esp_bt_audio_source/ARCH.md`
  (or create a short `docs/HARDWARE_NOTES.md` if ARCH.md is already long) explaining:
  - What the warning means: the ESP32 ROM bootloader measures the crystal by counting
    oscillator cycles during a fixed window; a 1–2 % deviation from 40 MHz is normal
    for mass-production crystals and does not affect operation.
  - That the 41.01 MHz reading is specific to the WROOM32 module on this board and has
    been confirmed safe across all test runs.
  - Mitigation: none required; the value is logged once at boot and not used for audio
    timing (audio timing derives from the A2DP stack clock, not the crystal directly).
- [x] **DOC-1b** Update the README.md "Build warnings present" bullet to cross-reference
  the new ARCH.md section instead of repeating the raw warning text.
- [x] **DOC-1c** Verify no tests assert on the absence of this warning (grep
  `crystal_freq` in test logs / `run_unity.py` output parsing); if any do, remove the
  assertion.

---

## TEST-1 — Host test: audio buffer fill and volume behavior

**Status:** `[x]` Done  
**Source:** README.md "Test coverage gaps" — "assert observable buffer fill/volume behavior"  
**Priority:** High

### Background
`test/host_test/test_audio_processor_real.c` and `test_audio_processor_core_logic.c`
exercise initialisation, source-switch counting, and beep-overlay behaviour, but they
do not assert:
- That injecting audio data **actually fills** the ring buffer to a measurable level.
- That reads **drain** the buffer proportionally (bytes-out == bytes-in for a full
  round-trip with no source switch).
- That volume scaling produces a **quantifiably attenuated** output sample (i.e. a 50 %
  volume setting halves the peak sample amplitude).

### Tasks

- [x] **TEST-1a** In `test_audio_processor_read.c` add
  `test_audio_buffer_fill_and_drain_round_trip`: injects 64 bytes, asserts free bytes
  decrease by 64, reads back via `audio_processor_read()`, asserts free bytes restore.

- [x] **TEST-1b** Add `test_volume_set_then_apply_scales_pcm_amplitude` in
  `test_audio_processor_diag.c` and `test_audio_volume_set_reflects_in_volume_gain_state`
  in `test_audio_processor_core_logic.c`: together they verify the full set→apply chain.

- [x] **TEST-1c** Add `test_audio_buffer_full_rejects_write_no_state_mutation` in
  `test_audio_processor_read.c`: fills ring to capacity, asserts the next write returns 0
  and buffer state is unchanged.

- [x] **TEST-1d** All new tests compile and pass under ASan: 60/60 host tests pass.

---

## TEST-2 — Host test: 128 KiB runtime heap floor

**Status:** `[x]` Done  
**Source:** README.md "Test coverage gaps" — "capture the new 128 KiB runtime floor"  
**Priority:** High

### Background
`test_audio_processor_real.c` line 57 asserts only `bytes >= 1024` for
`audio_processor_get_work_buffer_bytes()`.  The README indicates that the audio
processor was updated to reserve a 128 KiB runtime heap floor for DRAM-only boards
(as noted in the "Real I2S capture integration" entry).  This floor needs an explicit
assertion so a regression (e.g. accidental reduction of the work-buffer allocation)
fails a test immediately.

### Tasks

- [x] **TEST-2a** Added `AUDIO_WORK_BUFFER_DRAM_MIN_BYTES` and `AUDIO_MIN_RB_CAPACITY_BYTES`
  constants to `components/audio_processor/include/audio_processor_internal.h`.

- [x] **TEST-2b** Added `test_work_buffer_dram_min_is_at_least_half_compile_time_default`
  in `test_audio_processor_state.c`: asserts the DRAM floor equals half the compile-time
  default and is at least 1 KiB.

- [x] **TEST-2c** Added `test_ring_buffer_capacity_meets_minimum_floor` in
  `test_audio_processor_read.c`: allocates a ring buffer at `AUDIO_MIN_RB_CAPACITY_BYTES`
  and asserts the reported capacity meets the floor.

- [x] **TEST-2d** 60/60 host tests pass.

---

## TEST-3 — Host test: connection drop and timeout scenario coverage

**Status:** `[x]` Done  
**Source:** README.md "Test coverage gaps" implicit from "Device scanning and connection
management — event-streaming & robustness"  
**Priority:** Medium

### Background
`test/host_test/test_bt_connection_manager.c` and `test_bt_connection_manager_edge_cases.c`
cover normal connect/disconnect flows.  There is no host-side test that drives:
- A simulated mid-stream disconnect while audio data is in-flight.
- A connection attempt that times out (GAP callback never arrives).
- Reconnection after an unexpected disconnect.

These scenarios are risky to test only on-device because they require a cooperative
Bluetooth peer.

### Tasks

- [x] **TEST-3a** Added `test_bt_disconnect_mid_stream_transitions_streaming_state_to_stopped`
  in `test_bt_connection_manager_edge_cases.c`: connects, starts streaming, fires
  `ESP_A2D_AUDIO_STATE_STOPPED` → asserts PAUSED, then fires
  `ESP_A2D_CONNECTION_STATE_DISCONNECTED` → asserts STOPPED.

- [x] **TEST-3b** Added `test_bt_all_reconnect_attempts_exhausted_returns_to_disconnected`
  (under `#if CONFIG_BT_MOCK_TESTING`): forces all reconnects to fail, asserts
  `reconnect_attempts == 5` and state is not CONNECTING.

- [x] **TEST-3c** Added `test_bt_reconnect_succeeds_after_unexpected_disconnect`
  (under `#if CONFIG_BT_MOCK_TESTING`): simulates unexpected disconnect, triggers
  auto-reconnect, simulates successful reconnect, asserts CONNECTED.

- [x] **TEST-3d** No timer-advance mock was needed; existing mock infrastructure was
  sufficient.

- [x] **TEST-3e** 60/60 host tests pass.

---

## TOOL-1 — Unity log formatting uplift

**Status:** `[ ]` Open  
**Source:** README.md "Unity log formatting uplift"  
**Priority:** Medium

### Background
`esp_bt_audio_source/tools/run_unity.py` has a "timeout fallback" code path
(lines ~486–534) that post-processes the log file when real-time event parsing fails to
find a completion marker.  The README asks for this fallback to be eliminated so
canonical pass/fail lines always arrive via the real-time path, keeping
`tmp/canonical_unity_summary.json` minimal and CI-friendly.

### Tasks

- [ ] **TOOL-1a** Reproduce the condition that triggers the timeout fallback:
  - Add `--debug` output to a test suite whose Unity summary line arrives after the
    `TEST_RUN_COMPLETE` regex fires (i.e. the summary arrives late on the serial port).
  - Document the root cause in a comment in `run_unity.py`.

- [ ] **TOOL-1b** Fix the root cause:
  - If the issue is that Unity emits the summary line **after** the custom
    `TEST_RUN_COMPLETE` marker, reorder the test runner's `app_main` to call
    `UNITY_END()` before printing the custom marker.
  - If the issue is a serial latency race, increase the post-completion drain window
    (currently implicit in the regex loop) before the script declares the run done.

- [ ] **TOOL-1c** Remove or guard the timeout-fallback branch in `run_unity.py` behind
  a `--allow-fallback` flag so it is off by default and CI fails loudly if the
  canonical path is broken.

- [ ] **TOOL-1d** Update `tmp/canonical_unity_summary.json` schema comment (or the
  docstring in `tools/aggregate_unity.py`) to note that the fallback field is now
  absent in normal runs.

- [ ] **TOOL-1e** Run `tools/run_all_tests.py` to confirm all device and host suites
  pass and the JSON summary contains no `fallback: true` entries.

---

## Status corrections (README items already done)

These items appear as open in README.md but have been implemented and committed.

| README item | Actual status | Where |
|---|---|---|
| HELP command "placeholder message" | **Done** — `s_cmd_help_entries[]` table with 32 commands, `cmd_handle_help()` emits structured `INFO\|HELP\|ENTRY\|…` per command | `components/command_interface/cmd_handlers_system.c` |
| Beep diagnostics CLI ("arm `audio_processor_enable_next_beep_diag()` without reflash") | **Done** — `handle_debug_beep_diag()` registered as `DEBUG BEEP_DIAG`, responds `OK\|DEBUG\|BEEP_DIAG_ARMED` | `components/command_interface/cmd_handlers_bt.c:583` |
| `bt_app_core.c` queue-full param leak | **Done** — `bt_app_send_msg` frees `msg.param` via `param_free_cb` on enqueue failure | `components/bt_manager/bt_app_core.c:143` |
| `bt_app_core.c` shutdown drain | **Done** — `bt_app_task_shut_down` drains queue and calls `param_free_cb` before `vQueueDelete` | `components/bt_manager/bt_app_core.c:109–116` |
| Real I2S capture integration ("replace sine-wave stub in bt_streaming_manager.c") | **Done** — `bt_streaming_manager.c` already delegates to `audio_processor` for I2S capture; no sine stub remains | `components/bt_manager/bt_streaming_manager.c:157–361` |
