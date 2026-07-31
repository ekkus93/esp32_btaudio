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

- [ ] `grep -rn "bt_manager_hfp_audio_start\|bt_hfp_audio_start" esp_bt_audio_source/components esp_bt_audio_source/main` and confirm the only production call site remains the command handler (re-verify this hasn't changed since the audit, especially if Part A's changes touch nearby code).

### B1. Add the documentation

- [ ] `esp_bt_audio_source/README.md`: add a short note near the `AUDIO_AUTOSTART` command table entry (line ~63) or in a new minimal "HFP full-duplex audio (experimental)" subsection, stating explicitly: HFP microphone audio (`HFP AUDIO START`) is never started automatically — not at boot, not on SLC connect, not on A2DP connect/autostart — it requires an explicit `HFP AUDIO START` command every session. Make clear this is distinct from `AUDIO_AUTOSTART`, which only affects A2DP playback.
- [ ] Cross-check whether `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md` has a place this same fact belongs (it's the companion spec to the parent TODO) — add a matching one-line statement there if it has a relevant "behavior contract" or "capability boundary" section, for consistency with how FD-16's capability boundary is documented in the parent TODO.

### B2. Close the loop in the parent TODO

- [ ] In `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`, check off the FD-28 bullet "State explicitly that microphone audio does not start automatically at boot" (line ~406), since this Part directly satisfies it — do not check off the other FD-28 bullets, which remain genuinely blocked on hardware results.

### B3. Verify and commit

- [ ] No code changes in this part — no test/build verification needed beyond a proofread of the added prose for accuracy against B0's re-confirmed behavior.
- [ ] Commit (docs-only), push.

---

## Part C: Reconcile the two FD-24 items the audit found "likely already satisfied but unchecked"

### Background

The audit found concrete, passing test evidence for two of the seven FD-22–24 bullets and flagged them as possibly checkable — but cautioned against blindly checking boxes without confirming the existing coverage is actually exhaustive enough to "prove" the claim (the parent TODO uses "prove" deliberately, a stronger bar than "some tests exist"). This Part does that completeness review properly rather than rubber-stamping the audit's tentative finding.

### C0. "Prove stale events cannot resurrect stopped sessions"

- [ ] Compile the full list of existing test evidence found by the audit: `test_stale_generation_is_ignored_and_counted` (`test/host_test/test_bt_duplex_state_cases.c`), `test_a2dp_stale_terminal_from_prior_session_cannot_change_new_session` and `test_a2dp_late_stopped_after_disconnect_is_ignored` (`test/host_test/test_bt_manager_connection_pairing_events.c`), `test_a2dp_stale_same_peer_handle_cannot_mutate_new_connection` (`test_bt_a2dp_binding_cases.c`), `test_stale_generation_is_rejected_by_generation_bound_i2s` (`test_bt_hfp_audio_cases.c`).
- [ ] Identify what "stale event" categories exist across the whole system and confirm each has a corresponding test in the list above (don't just count tests — map them to categories): stale A2DP connection event, stale A2DP audio event, stale HFP profile event, stale HFP audio-control event, stale I2S-ring generation, stale duplex-state generation. Note any category with *no* direct test as a genuine remaining gap rather than silently treating the whole bullet as done.
- [ ] If every category maps to a real, passing test: check off the bullet in `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md` (FD-22–24 section, line ~350) and add a footnote/citation listing the exact test names, matching how FD-16's "Focused coverage present" section already cites tests inline.
- [ ] If a category is missing: write the missing test(s) first (in the appropriate existing test file for that subsystem), get them passing, *then* check off the bullet with the completed citation list.

### C1. "Prove counters remain monotonic except explicit baseline reset"

- [ ] Compile the existing evidence: the `reset_sequence` field threaded through `bt_hfp_manager_fd11.c` (lines ~29, 351, 489, 497, 503, 537-538), the header comment in `bt_hfp_manager.h` (lines ~124-127) stating callback lifetime maxima/counters are untouched by `RESETSTATS`, and the regression-detection tests `test_manager_stats_detects_regressed_lifetime_source` / `test_manager_stats_detects_regressed_lifetime_maximum` plus the `RESETSTATS`-gating tests `test_manager_resetstats_rejects_live_slc_audio_callback_and_i2s` / `test_manager_resetstats_rejects_authoritative_streaming_state` (all in `test_bt_hfp_manager_cases.c`).
- [ ] Enumerate every counter family exposed through `HFP STATS` (duplex, SLC, audio_control, incoming/RX, I2S — the `STATS_*` lines in `cmd_handlers_hfp_stats.c`) and confirm each one is covered by the `SUB()`-macro baseline-reset mechanism in `bt_hfp_manager_fd11.c`, i.e. that `RESETSTATS` correctly baselines it rather than zeroing it destructively (a "monotonic except explicit baseline reset" claim is violated if any counter is actually zeroed by `RESETSTATS` rather than baselined, since a baselined counter can still be read as "went backwards" if the underlying source counter itself ever regresses — which is exactly what the regression-detection tests above check for).
- [ ] Note explicitly whether the new `i2s_state_sync_failures` counter from Part A gets swept into this reconciliation correctly (it should, since A0 wires it through the same `COPY`/`REG`/`SUB` macros as its siblings) — this is a good place to catch a wiring mistake in Part A if one exists.
- [ ] If the enumeration confirms full coverage: check off the bullet (line ~351) with a citation list, same as C0.
- [ ] If gaps exist: write the missing regression/baseline tests for the uncovered counter families first, then check off the bullet.

### C2. Verify and commit

- [ ] Full host `ctest --output-on-failure` after any new tests are added — zero regressions.
- [ ] Commit the TODO edits (and any new test files) together; push.

---

## Part D: Fix the two FD-08 cosmetic/coverage gaps in the I2S0 writer

### D0. GPIO 25/26 exclusion check omits `dout_gpio`

- [ ] `esp_bt_audio_source/components/audio_processor/hfp_i2s_output.c`, `hfp_i2s_output_validate_config()` (lines 221-224): the check `if (config->bclk_gpio == 25 || config->bclk_gpio == 26 || config->ws_gpio == 25 || config->ws_gpio == 26)` excludes GPIO 25/26 (the ESP32's DAC1/DAC2 pins, confirmed reserved separately from the generic `is_strapping_gpio()` check at line 48-51, which only covers {0,2,5,12,15}) for `bclk_gpio` and `ws_gpio` but not `dout_gpio` — meaning a caller could currently configure `dout_gpio = 25` or `26` and pass validation, risking a collision with whatever else in this project reserves the DAC pins (check `grep -rn "GPIO_NUM_25\|GPIO_NUM_26\|gpio.*25\|gpio.*26" esp_bt_audio_source/components/audio_processor/ esp_bt_audio_source/main/` first to confirm exactly what currently uses them, so the fix's rationale/comment is accurate).
- [ ] Extend the check to include `config->dout_gpio == 25 || config->dout_gpio == 26`.
- [ ] Add a host test asserting `hfp_i2s_output_validate_config()` rejects a config with `dout_gpio` set to 25 and to 26 (mirroring whatever existing test already covers the `bclk_gpio`/`ws_gpio` 25/26 rejection — find it via `grep -n "25\|26" test/host_test/test_hfp_i2s_output*.c` and add the missing `dout_gpio` case alongside it).

### D1. Untested "lock cannot be acquired" defensive path in the writer

- [ ] `esp_bt_audio_source/components/audio_processor/hfp_i2s_output_data.c:80-84`: `hfp_i2s_output_writer_iteration()`'s lock-acquisition-failure branch is implemented correctly (returns immediately, before touching the ring or writing silence) but is currently untested — meaning a future refactor could silently break this guarantee with no test catching it.
- [ ] Write a host test that forces `hfp_i2s_output_lock()` to fail via the `s_output.lock == NULL` branch (`hfp_i2s_output.c:101`): after a normal `hfp_i2s_output_init()`/`_start()` sequence, directly null out `g_hfp_i2s_output.lock` through the internal header's `extern`-exposed struct (`s_output`/`g_hfp_i2s_output`, declared in `include/hfp_i2s_output_internal.h:65-66`), then call the existing test hook `hfp_i2s_output_test_writer_once()` (`hfp_i2s_output.c:309-312`).
- [ ] Assert: the call returns `ESP_ERR_INVALID_STATE`; the ring's available-to-read byte count is unchanged (nothing was consumed); no write/silence/short-write/loss counters incremented (nothing was fabricated or fed to the I2S channel). Restore the lock afterward (or otherwise ensure `tearDown()` can still clean up the fixture without crashing on a NULL mutex).
- [ ] If this reveals the "lock == NULL" path is actually unreachable in a way that makes the new test awkward to write cleanly (e.g. other invariants assume the lock is always non-NULL once initialized), note that finding and adjust the test to instead exercise whatever real path *can* trigger `hfp_i2s_output_lock()`'s failure return — do not weaken the assertion or skip the test to force it to pass.

### D2. Verify and commit

- [ ] Full host `ctest --output-on-failure` — zero regressions, both new tests passing.
- [ ] `idf.py build` — confirm clean device build (D0 touches production validation logic under `hfp_i2s_output.c`, which is compiled into the device firmware).
- [ ] Commit, push.

---

## Part E: Obtain fresh CI validation for the FD-25/FD-29 closeout gates

### Background

FD-25's "Required for the final documentation head" list and FD-29's "Closeout blockers" are process/bookkeeping gates, not code gaps — the audit already confirmed today's local runs pass (host: 1199+ tests passing pre-Part-A/D additions; device: clean `idf.py build` under the project's documented ESP-IDF v5.5.1 environment). What's missing is a **fresh, recorded GitHub Actions CI run** at the head that includes Parts A-D's changes, with exact run IDs/SHA/conclusions captured — which is what the parent TODO's status rules require before a review-fix closeout doc can be created.

### E0. Push and let CI run

- [ ] After Parts A-D are committed and pushed, this repo's push-triggered workflows (`CI — host tests (optimized)` and `CI — device build (compile only)`) run automatically on `feature/esp-bt-audio-duplex` — no manual trigger needed.
- [ ] Per `esp_bt_audio_source/docs/CHATGPT_READABLE_GITHUB_ACTIONS_CI_STATUS_BRIDGE_SPEC.md` (currently only on `master`, not this branch — read it via `git show master:esp_bt_audio_source/docs/CHATGPT_READABLE_GITHUB_ACTIONS_CI_STATUS_BRIDGE_SPEC.md` if needed), `feature/esp-bt-audio-duplex` is mapped to status issue `#4`. Check that issue (or the workflow run pages directly via `gh run list`/`gh api`) for the fresh run's result rather than guessing or re-running local tests as a substitute.

### E1. Record exact evidence

- [ ] Host workflow: exact run ID, final SHA (should be the commit from Part D or Part C, whichever lands last), conclusion, and test counts.
- [ ] Device workflow: exact run ID, final SHA, image size, partition size, and headroom (compare against the historical 1,025,808-byte image / 743,664-byte headroom baseline noted in the parent TODO's FD-25 section — flag any large regression, though the changes in this TODO are small and shouldn't move it meaningfully).
- [ ] Confirm no flash occurred (both workflows are compile-only/host-only by design — confirm this wasn't changed).

### E2. Close out the parent TODO's blocked items

- [ ] In `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`, check off the FD-25 bullets that a passing fresh CI run directly satisfies (lines ~367-379) and the FD-29 closeout-blocker bullets it satisfies (lines ~430-432), recording the exact run IDs/SHA/counts from E1 inline (matching the doc's existing evidence-citation style, e.g. the FD-25 "Historical evidence only" section's format).
- [ ] If both workflows pass at the same head: create `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md` (the exact filename the parent TODO already names at line ~433) recording final changed files (Parts A-D's file list), no-flash status, remaining limitations, and pending hardware work — do not create this file before both workflows are confirmed passing at the same head, per the parent TODO's explicit rule at line ~20.
- [ ] If either workflow fails: diagnose and fix before proceeding — do not create the closeout doc, and do not check off the FD-25/FD-29 bullets that workflow was meant to satisfy.

### E3. Final housekeeping

- [ ] Update `memory.md` per this repo's standing convention, summarizing Parts A-E and the fresh CI result.
- [ ] Commit, push.
