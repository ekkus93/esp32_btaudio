# ESP32 Bluetooth Audio Full-Duplex — Audit Fix TODO

**Source audit:** verification pass against `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`, conducted 2026-07-31.
**Implementation branch:** `feature/esp-bt-audio-duplex`
**Scope:** 5 issues found during the audit — 1 real (if low-severity) correctness inconsistency, 1 documentation gap, 1 TODO-bookkeeping gap, 2 cosmetic/test-coverage gaps, and 1 CI-process gap.

## Ground rules (carried over from the parent TODO's non-negotiable rules)

- Work directly on `feature/esp-bt-audio-duplex`. Do not create a helper branch or PR without explicit instruction.
- Reject quiet fallback, silent data loss, fabricated status, fake success.
- Make rejected data and failed diagnostics visible through exact errors, stable records, or counters — this is the exact principle Part A is fixing a violation of.
- Do not flash hardware without explicit user approval. Nothing in this TODO requires flashing.
- One task = one commit, matching this repo's established convention. Verify with full host `ctest` and `idf.py build` after each part before moving to the next.

---

## Part A: Fix the primary/secondary error-precedence inconsistency in `bt_hfp_audio_control_i2s.c`

### Background

`start_i2s_for_session()` (lines 101-138) and `stop_i2s_for_session()` (lines 160-218) both call `sync_i2s_state()` after a primary I2S operation fails, to reconcile duplex state with reality. In both functions, if that secondary sync call *also* fails, its error (`sync_err`) silently **replaces** the primary operation's error (`err`) in `last_error`, in the value passed to `set_health()`, and in the function's return value — discarding the actual root cause. This runs against the project's own stated principle (applied correctly elsewhere, e.g. `bt_duplex_set_health`'s primary/secondary precedence) that a primary failure must be preserved while secondary failures are made visible through a separate channel, not by overwriting the primary.

This only manifests in a double-fault scenario (the primary I2S operation fails **and** the state-sync call also fails), so severity is low — the operation is still correctly marked `FAULTED`/quarantined either way, nothing reports fake success. But the diagnostic value of `last_error`/`HFP STATS` is degraded in exactly the scenario where it matters most (compound failure during teardown).

### A0. Design the fix

- [x] Decide the precedence rule: `err` (the primary operation's result) is authoritative whenever `err != ESP_OK`; `sync_err` only becomes the *returned*/`last_error` value when the primary operation itself succeeded (`err == ESP_OK`) but the sync failed. This matches how the codebase already treats primary-vs-secondary errors elsewhere (e.g. `control_unlock(esp_err_t prior)`'s `prior != ESP_OK ? prior : err` pattern in `bt_hfp_audio_control.c`).
- [x] Add a new counter field to make the secondary sync failure visible without overwriting the primary, per the project's "make failures visible via exact errors and counters" rule:
  - [x] `esp_bt_audio_source/components/bt_manager/include/bt_hfp_audio.h`: added `uint64_t i2s_state_sync_failures;` to `bt_hfp_audio_control_snapshot_t`.
  - [x] `esp_bt_audio_source/components/bt_manager/bt_hfp_manager_fd11.c`: threaded the new field through `COPY()`, `REG()`, and `SUB()` alongside `i2s_start_failures`/`i2s_stop_failures`.
  - [x] `esp_bt_audio_source/components/bt_manager/include/bt_hfp_manager.h`: added the matching field to `bt_hfp_manager_stats_t`'s `audio_control` struct.
  - [x] `esp_bt_audio_source/components/command_interface/cmd_handlers_hfp_stats.c`: **deviation from plan** — extending `STATS_AUDIO3` in place (as originally planned) pushed its worst-case formatted length (all counters at `UINT64_MAX`) from 315 to 356 bytes against a 320-byte `HFP_DATA_BUFFER_SIZE`, which broke the existing `test_hfp_stats_max_values_are_split_without_truncation` regression test (confirmed by measuring both worst-case lengths precisely with a script, not by guessing). Fixed by adding a new `STATS_AUDIO4` line carrying just `I2S_STATE_SYNC_FAIL`, matching this file's existing convention of splitting rather than growing a shared buffer (the `STATS_AUDIO1/2/3`, `STATS_RX1-4`, `STATS_I2S1-7` precedent). Added an explicit assertion for `STATS_AUDIO4`'s presence to the max-values test.

### A1. Fix `start_i2s_for_session()` (`bt_hfp_audio_control_i2s.c:101-138`)

- [x] Changed `last_error` assignment to always use `err` (the branch is only reached when `err != ESP_OK`), and increment `i2s_state_sync_failures` when `sync_err != ESP_OK` in the same locked block.
- [x] `set_health()`'s error argument now always passes `err`; the health-report `text` now says `"HFP I2S startup failed (state sync also failed)"` when `sync_err != ESP_OK`, so both facts are visible without cross-referencing the counter.
- [x] Return value changed to `return err;` unconditionally.
- [x] Confirmed the `QUARANTINED` branch (`sync_err == ESP_OK && local.state == HFP_I2S_OUTPUT_QUARANTINED`) is unaffected — it already used `err`, not `sync_err`.

### A2. Fix `stop_i2s_for_session()` (`bt_hfp_audio_control_i2s.c:160-218`)

- [x] Changed to `result = err != ESP_OK ? err : sync_err;` — primary `err` wins whenever non-OK; `sync_err` only surfaces when the primary stop succeeded but the sync failed.
- [x] Added a `bool sync_failed` parameter to `record_i2s_stop_failure()`, set from `err != ESP_OK && sync_err != ESP_OK` at the call site, incrementing `i2s_state_sync_failures` under the same lock as `i2s_stop_failures`.
- [x] `record_i2s_stop_failure()`'s health-report branch now reports `"HFP I2S stop failed (state sync also failed)"` when `sync_failed` is true, otherwise the original text; both branches use the corrected `result`.

### A3. Tests — reproduce the double-fault scenario before fixing, confirm after

- [x] Found `test_bt_hfp_audio_control_cases.c` (only file referencing `start_i2s_for_session`/`i2s_start_failures`/`i2s_stop_failures`).
- [x] Added `test_start_i2s_double_fault_preserves_primary_error` and `test_stop_i2s_double_fault_preserves_primary_error`. **Deviation from plan**: no existing test hook could force `bt_duplex_set_i2s_state` to fail at exactly the sync point (the state at that moment — `STARTING`/`STOPPING` — legally permits every target state the mock could realistically produce, so no "illegal transition" naturally occurs there, and there is no existing lock-injection hook analogous to `bt_manager_test_force_next_ctx_lock_result` for the duplex layer). Added a new, minimal, one-shot test-only hook `bt_duplex_test_force_i2s_state_result(target_state, result)` in `bt_duplex_state_transitions.c`/`bt_duplex_state_core.c` (mirroring the existing `bt_duplex_test_set_health_report_result` pattern), which forces the *next* `bt_duplex_set_i2s_state()` call whose target state matches `target_state` to fail with `result`, then disarms itself — precise enough to hit only the sync call, not the earlier `STARTING`/`STOPPING` transition in the same function.
- [x] Confirmed both new tests **fail against the pre-fix code**: temporarily reverted just `bt_hfp_audio_control_i2s.c` via `git stash` (keeping the new struct field/hook in place), rebuilt, and got `Expected -5 [ESP_ERR_TIMEOUT] Was -3 [ESP_ERR_INVALID_STATE]` on both new tests, with all 21 pre-existing tests in the same binary still passing — proving the tests genuinely catch the regression, not a tautology. Restored the fix (`git stash pop`) afterward.
- [x] Full existing `test_bt_hfp_audio_control` suite (23/23) passes with no regression in the single-fault cases.

### A4. Verify and commit

- [x] Full host `ctest --output-on-failure` — 100/100 passed, zero regressions (after fixing the `STATS_AUDIO3`/`STATS_AUDIO4` split above).
- [x] `idf.py build` from `esp_bt_audio_source/` — clean device build (image `0xfb200` bytes, headroom `0xb4e00` bytes / 42% free).
- [x] No FD-10 checkbox changes made in the parent TODO — this is a bugfix within already-"software implemented" scope, not a new capability.
- [x] Commit, push.

---

## Part B: Document that HFP microphone audio does not autostart at boot (FD-28 gap)

### Background

The audit confirmed the *behavior* already satisfies this: `bt_manager_hfp_audio_start()` has exactly one call site in the entire codebase — the explicit `HFP AUDIO START` command handler (`cmd_handlers_hfp_fd11_v2.c`). Nothing in `main/main.c`, `bt_manager_init()`, or any A2DP-connection/autostart path calls it. The existing `AUDIO_AUTOSTART` command/feature (`README.md` line 63) is a **different, unrelated** mechanism — it controls A2DP-only playback autostart-on-connect (`s_autostart_enabled` in `bt_manager.c`), not HFP microphone audio. This is purely a documentation gap; no code change is needed, but the two "autostart" concepts being easy to conflate is itself worth calling out explicitly.

### B0. Confirm current behavior one more time before documenting it (avoid stating something later found to be untrue)

- [x] Re-ran `grep -rn "bt_manager_hfp_audio_start\|bt_hfp_audio_start(" esp_bt_audio_source/components esp_bt_audio_source/main` — confirmed the only production call site is still `cmd_handlers_hfp_fd11_v2.c:237` (the `HFP AUDIO START` command handler). Part A's changes (error precedence in the I2S layer) did not touch this call chain.

### B1. Add the documentation

- [x] `esp_bt_audio_source/README.md`: added an "HFP full-duplex microphone audio (experimental)" note right after the `UARTAUDIO` command table row, stating the mic never autostarts (not at boot, not on SLC connect, not on A2DP connect/autostart) and distinguishing it from `AUDIO_AUTOSTART` (A2DP-only).
- [x] `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md` §8 ("Duplex operating modes") already had a closely related normative statement ("the SCO/eSCO audio link MUST NOT open automatically merely because HFP SLC exists") — appended a concrete sentence naming boot/SLC-connect/A2DP-autostart explicitly and naming the `HFP AUDIO START` command as the only trigger, with no persisted "resume last session" behavior.

### B2. Close the loop in the parent TODO

- [x] Checked off the FD-28 bullet "State explicitly that microphone audio does not start automatically at boot" in `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md` (line ~406), citing both doc locations. Left every other FD-28 bullet unchecked (still genuinely blocked on hardware results).

### B3. Verify and commit

- [x] No code changes in this part — proofread the added prose against B0's re-confirmed behavior.
- [x] Commit (docs-only), push.

---

## Part C: Reconcile the two FD-24 items the audit found "likely already satisfied but unchecked"

### Background

The audit found concrete, passing test evidence for two of the seven FD-22–24 bullets and flagged them as possibly checkable — but cautioned against blindly checking boxes without confirming the existing coverage is actually exhaustive enough to "prove" the claim (the parent TODO uses "prove" deliberately, a stronger bar than "some tests exist"). This Part does that completeness review properly rather than rubber-stamping the audit's tentative finding.

### C0. "Prove stale events cannot resurrect stopped sessions"

- [x] Compiled the full list of existing test evidence and mapped every category: stale A2DP connection event (`test_a2dp_stale_same_peer_handle_cannot_mutate_new_connection`, `test_a2dp_stale_terminal_from_prior_session_cannot_change_new_session`), stale A2DP audio event (`test_a2dp_late_stopped_after_disconnect_is_ignored`), stale HFP profile event (`test_hfp_late_same_peer_event_after_new_generation_is_counted` — found during this reconciliation, not in the audit's original list), stale HFP audio-control event (`test_late_old_connected_event_after_timeout_is_ignored`), stale I2S-ring generation (`test_ring_generation_and_reset_contract`, `test_stale_generation_is_rejected_by_generation_bound_i2s`), stale duplex-state generation (`test_stale_generation_is_ignored_and_counted`).
- [x] All 6 categories have a direct, passing test — no gap found.
- [x] Checked off the bullet in `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md` (line ~350) with the full citation list.

### C1. "Prove counters remain monotonic except explicit baseline reset"

- [x] Compiled the existing evidence (`reset_sequence`, the `bt_hfp_manager.h` lifetime-maxima comment, the regression-detection and `RESETSTATS`-gating tests).
- [x] Enumerated every field in all 5 counter groups (`duplex`: 11/11 fields in `SUB()`; `slc`: 8/8; `audio_control`: all true counters in `SUB()`, matches `COPY()` exactly; `incoming`: 21 counters in `SUB()` vs. 22 in `COPY()`, the one gap being `callback_last_us` — a point-in-time gauge, correctly excluded; `i2s`: 24 counters in `SUB()` vs. 26 in `COPY()`, the two gaps being `ring_current_used` (point-in-time gauge) and `ring_peak_used_lifetime` (documented lifetime-max, excluded from `SUB()` by design but still present in `REG()`'s never-regressed check, exactly matching the `callback_max_us_lifetime` pattern). No unclassified gap — every exclusion from `SUB()` is a deliberate, correctly-categorized exception.
- [x] Confirmed the new `i2s_state_sync_failures` counter from Part A is correctly swept into `COPY`/`REG`/`SUB` (verified present in all three during this pass).
- [x] Checked off the bullet (line ~351) with the full citation list and the gauge/lifetime-max classification.

### C2. Verify and commit

- [x] No new tests were needed (full coverage already existed) — full host `ctest --output-on-failure` re-run to confirm zero regressions from the TODO-only edits.
- [x] Commit the TODO edits; push.

---

## Part D: Fix the two FD-08 cosmetic/coverage gaps in the I2S0 writer

### D0. GPIO 25/26 exclusion check omits `dout_gpio`

- [x] Confirmed GPIO25/26 are the ESP32's DAC1/DAC2 pins via `grep` — `main/main.c` and `i2s_manager.c` both document the exact playback-side bug this guards against (BCLK/WS had to move off GPIO25/26 to GPIO18/19 because the WROOM32, as I2S master, must output the clock and GPIO25/26 could not do so reliably).
- [x] Extended the check to include `config->dout_gpio == 25 || config->dout_gpio == 26`, with a comment explaining why.
- [x] Added 6 host test assertions (`test_default_config_and_pin_validation`): bclk=25, bclk=26, ws=25, ws=26, dout=25, dout=26 all rejected. **Note**: there was no existing test for the bclk/ws 25/26 exclusion at all (the TODO's phrasing assumed one existed) — added all 6 cases together rather than just the missing `dout_gpio` pair, closing the more fundamental pre-existing gap too.

### D1. Untested "lock cannot be acquired" defensive path in the writer

- [x] Confirmed the guard is implemented correctly (`hfp_i2s_output_data.c`) but was untested.
- [x] Added `test_writer_skips_consumption_when_lock_is_unavailable`: pushes real PCM into the ring, saves and nulls `g_hfp_i2s_output.lock`, calls `hfp_i2s_output_test_writer_once()`, restores the lock before `tearDown()` runs (confirmed via the mutex-delete log line appearing exactly once per test run — no leak, no double-free).
- [x] Asserted: returns `ESP_ERR_INVALID_STATE`; `write_calls`, `silence_intervals`, `silence_samples`, `short_writes`, `write_lost_bytes`, `ring.total_read_bytes`, and `ring.current_used` are all unchanged; the mock's mock `fake.write_calls == 0` (nothing reached the fake I2S channel). The `s_output.lock == NULL` path was directly reachable and clean to test — no fallback needed.

### D2. Verify and commit

- [x] Full host `ctest --output-on-failure` — 100/100 passed, zero regressions.
- [x] `idf.py build` — clean device build (image `0xfb220` bytes, headroom `0xb4de0` bytes / 42% free).
- [x] Commit, push.

---

## Part E: Obtain fresh CI validation for the FD-25/FD-29 closeout gates

### Background

FD-25's "Required for the final documentation head" list and FD-29's "Closeout blockers" are process/bookkeeping gates, not code gaps — the audit already confirmed today's local runs pass (host: 1199+ tests passing pre-Part-A/D additions; device: clean `idf.py build` under the project's documented ESP-IDF v5.5.1 environment). What's missing is a **fresh, recorded GitHub Actions CI run** at the head that includes Parts A-D's changes, with exact run IDs/SHA/conclusions captured — which is what the parent TODO's status rules require before a review-fix closeout doc can be created.

### E0. Push and let CI run

- [x] Pushed Parts A-D; CI ran automatically on `feature/esp-bt-audio-duplex`.
- [x] **Deviation from plan, important finding**: the first CI run after pushing (`docs: check off FD-24...`, and every run since the FILE_SPLIT_TODO closeout commit earlier in this session) **failed** host tests — not a flake. `gh run view <id> --log-failed` showed `undefined reference to bt_manager_test_init_profiles`. Root cause: 3 standalone `cc`-based sanitizer scripts (`run_bt_manager_hfp_profiles_test.sh`, `run_bt_hfp_audio_control_test.sh`, `run_bt_hfp_commands_test.sh`) and a 4th, fully separate CMake project (`test/host_test/a2dp_binding_lifecycle/CMakeLists.txt`, invoked by `run_bt_a2dp_binding_lifecycle_test.sh`) hardcode their own production source-file lists, independent of the main `test/host_test/CMakeLists.txt` that this session's earlier file splits (bt_manager_profiles.c, bt_hfp_audio_control_*.c, cmd_handlers_hfp_*.c, bt_events_a2dp_{binding,data}.c) updated. Local `cmake --build`/`ctest` never exercises these scripts, so this session's extensive local verification never caught it. Fixed all 4 files (committed separately as `ab3ab8a3`, its own task per the one-task-one-commit rule); the `a2dp_binding_lifecycle` project was also separately missing `bt_ctx_lock.c` from a change that predates this session entirely.
- [x] Verified the fix by running all 13 scripts `ci-host-tests.yml` invokes, locally, under the same ASan/UBSan flags CI uses, before pushing: 0 failures, 0 sanitizer errors across all of them.
- [x] Did not use the ChatGPT CI-status-bridge issue (it lives only on `master`, not this branch, per the spec doc's own mapping) — used `gh run list`/`gh run view` directly instead, which was sufficient.

### E1. Record exact evidence

- [x] Host workflow: run [30674261295](https://github.com/ekkus93/esp32_btaudio/actions/runs/30674261295), SHA `ab3ab8a35b25f269cbef903c2cbae8e128339f0d`, conclusion `success`, CTest 100/100, all 13 sanitizer scripts passed.
- [x] Device workflow: run [30674261291](https://github.com/ekkus93/esp32_btaudio/actions/runs/30674261291), same SHA, conclusion `success`, image `0xfb220` bytes, partition `0x1b0000` bytes, headroom `0xb4de0` bytes (42% free) — consistent with the historical baseline, no meaningful regression.
- [x] Confirmed no flash: device workflow step titled "Build esp_bt_audio_source (no flash)."

### E2. Close out the parent TODO's blocked items

- [x] Checked off the FD-25 "Required for final documentation head" bullets and FD-26's partition-headroom bullet, with the run IDs/SHA/counts inline.
- [x] Checked off all 5 FD-29 "Closeout blockers" bullets.
- [x] Created `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md` after confirming both workflows passed at the same head (`ab3ab8a3`), recording the full Parts A-E changed-file list, no-flash confirmation, and remaining hardware-pending limitations (FD-14/15/17/18-24/26-28 unaffected).

### E3. Final housekeeping

- [x] Updated `memory.md`.
- [x] Commit, push.

---

**TODO is now fully closed out — all 5 Parts (A–E) complete.** Fresh CI validated at SHA `ab3ab8a3` (host: [30674261295](https://github.com/ekkus93/esp32_btaudio/actions/runs/30674261295) success; device: [30674261291](https://github.com/ekkus93/esp32_btaudio/actions/runs/30674261291) success). See `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md` for the final closeout record.
