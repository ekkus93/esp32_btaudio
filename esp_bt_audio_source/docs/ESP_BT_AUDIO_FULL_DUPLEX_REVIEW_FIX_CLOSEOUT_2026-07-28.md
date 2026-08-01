# ESP Bluetooth Audio Full-Duplex Review-Fix Closeout

Date: 2026-07-31
Branch: `feature/esp-bt-audio-duplex`
Validated head: `ab3ab8a35b25f269cbef903c2cbae8e128339f0d`
Status: **Review-fix software complete and CI-validated at the head above**

## 1. Scope closed out

This closeout satisfies the FD-25 "Required for the final documentation head" gate and the FD-29 "Closeout blockers" gate in `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`, covering two layers of work:

1. **FD-29's "Review-fix software complete" items** (callback overlap fail-closed, event-owned `conn_hdl` identity, atomic A2DP binding commit, lock-independent health-report visibility, status-unavailable/transport-failure exactness, callback stack-array bound enforcement, single-critical-section I2S writer accounting, atomic HFP callback registration) — completed prior to this closeout and unchanged by it.
2. **`ESP_BT_AUDIO_FULL_DUPLEX_AUDIT_FIX_TODO_2026-07-31.md`** — a 5-part follow-up produced by an independent verification pass against the parent TODO's own claims, fixing everything that verification found:
   - **Part A**: a real (low-severity) primary/secondary error-precedence bug in `bt_hfp_audio_control_i2s.c` — a secondary duplex-state-sync failure could silently overwrite the primary I2S start/stop failure in `last_error`, health reports, and the return value. Fixed, with a new `i2s_state_sync_failures` counter (surfaced via a new `STATS_AUDIO4` line) making the secondary failure visible without discarding the primary. New regression tests confirmed via `git stash` bisection to fail on the pre-fix code.
   - **Part B**: documented (README + full-duplex spec) that HFP microphone audio never autostarts — behavior was already correct, this closed a documentation gap only.
   - **Part C**: reconciled two FD-24 checklist items ("stale events cannot resurrect stopped sessions," "counters remain monotonic except explicit baseline reset") against actual test coverage — both fully satisfied, checked off with citations.
   - **Part D**: fixed a GPIO25/26 (ESP32 DAC pin) exclusion check that omitted `dout_gpio`, and added test coverage for both that check and a previously-untested writer lock-unavailable defensive path.
   - **Part E**: this gate. Running the full CI-equivalent local validation surfaced a real regression — three standalone `cc`-based sanitizer scripts and a fully separate CMake project (`test/host_test/a2dp_binding_lifecycle/`) had not been updated for the file splits performed earlier in this session (`bt_manager_profiles.c`, `bt_hfp_audio_control_{i2s,work,events}.c`, `cmd_handlers_hfp_{wire,stats}.c`, `bt_events_a2dp_{binding,data}.c`), because those splits only updated the main `test/host_test/CMakeLists.txt`-based `ctest` flow. Fixed; the `a2dp_binding_lifecycle` project was also separately missing `bt_ctx_lock.c` from a change that predates this session.

## 2. CI evidence at the validated head

| Workflow | Run | SHA | Conclusion |
|---|---|---|---|
| CI — host tests (optimized) | [30674261295](https://github.com/ekkus93/esp32_btaudio/actions/runs/30674261295) | `ab3ab8a3` | `success` |
| CI — device build (compile only) | [30674261291](https://github.com/ekkus93/esp32_btaudio/actions/runs/30674261291) | `ab3ab8a3` | `success` |

- Host: full CTest 100/100 passed; all 13 standalone ASan/UBSan sanitizer scripts invoked by `ci-host-tests.yml` passed with 0 failures and 0 sanitizer errors; Python lint gate ran with no changed Python files (legacy full-tree flake8 backlog remains advisory-only, per CI design).
- Device: image `0xfb220` bytes (1,028,529 bytes total padded), partition `0x1b0000` bytes, headroom `0xb4de0` bytes (740,832 bytes, 42% free, ≈723 KiB — exceeds the 256 KiB FD-26 compile-only gate). Consistent with the historical baseline recorded in FD-25 (no meaningful regression).
- **No hardware was flashed** — both workflows are compile-only/host-only by design; the device workflow step is explicitly titled "Build esp_bt_audio_source (no flash)."

## 3. Final changed files (this closeout's scope, `64100418..ab3ab8a3`)

Production code:
- `components/audio_processor/hfp_i2s_output.c`
- `components/bt_manager/bt_duplex_state_core.c`
- `components/bt_manager/bt_duplex_state_transitions.c`
- `components/bt_manager/bt_hfp_audio_control_i2s.c`
- `components/bt_manager/bt_hfp_manager_fd11.c`
- `components/bt_manager/include/bt_duplex_state.h`
- `components/bt_manager/include/bt_duplex_state_internal.h`
- `components/bt_manager/include/bt_hfp_audio.h`
- `components/bt_manager/include/bt_hfp_manager.h`
- `components/command_interface/cmd_handlers_hfp_stats.c`

Tests:
- `test/host_test/test_bt_hfp_audio_control.c`, `test_bt_hfp_audio_control_cases.c`
- `test/host_test/test_bt_hfp_commands_cases.c`
- `test/host_test/test_hfp_i2s_output.c`, `test_hfp_i2s_output_cases.c`

CI/build infrastructure:
- `test/host_test/a2dp_binding_lifecycle/CMakeLists.txt`
- `tools/run_bt_hfp_audio_control_test.sh`, `run_bt_hfp_commands_test.sh`, `run_bt_manager_hfp_profiles_test.sh`

Documentation:
- `README.md`
- `docs/ESP_BT_AUDIO_FULL_DUPLEX_AUDIT_FIX_TODO_2026-07-31.md` (new)
- `docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`
- `docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
- repo-root `memory.md`

## 4. Limitations and pending hardware

Nothing in this closeout changes the hardware-pending status of any phase. Still explicitly pending, unaffected by this work:

- FD-14/FD-15: CVSD hardware bring-up (GPIO32/33/27 electrical/timing verification).
- FD-17: simultaneous A2DP playback + HFP microphone hardware gate.
- FD-18/FD-19: HFP compatibility downlink (playback voice tap, outgoing PCM send path) — confirmed during the audit to have zero implementation, correctly so.
- FD-20/FD-21: operational HFP full duplex and mSBC — blocked on FD-18/FD-19 and hardware.
- FD-22–24: the broader failure/teardown/race hardening matrix beyond the two items closed out in Part C (threshold reconciliation table, full failure-injection matrix, full disconnect/race-ordering matrix remain open).
- FD-26/FD-27: all *runtime* (as opposed to compile-only) heap/stack/callback/soak measurements — require physical hardware.
- FD-28: README/architecture updates beyond the one-off autostart note added in Part B remain deferred pending hardware behavior.

No hardware was flashed at any point during the review-fix or audit-fix work.
