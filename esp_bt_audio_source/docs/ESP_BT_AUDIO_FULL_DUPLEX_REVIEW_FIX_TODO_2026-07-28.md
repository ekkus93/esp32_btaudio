# ESP32 Bluetooth Audio Full-Duplex Review Fix TODO

**Created:** 2026-07-28  
**Status updated:** 2026-07-28  
**Target branch:** `feature/esp-bt-audio-duplex`  
**Primary TODO:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`  
**Primary project:** `esp_bt_audio_source/`  
**Starting baseline:** `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`  
**Validated review-fix implementation head for compile-only CI:** `e48341ae665781dd6da6e40c4137bfbead4d1205`  
**Scope:** Resolve the concrete post-FD-16 review findings without claiming hardware validation or the unimplemented FD-18/FD-19 HFP downlink.

## Status legend

- `[x]` means the implementation or documentation item exists and was inspected.
- A checked implementation item does not imply that the latest complete host workflow passed.
- The latest ESP-IDF compile-only result is recorded separately from host-test status.
- Hardware-gated tasks remain unchecked until physical evidence exists.

---

# 0. Non-negotiable working rules

- [x] Work directly on `feature/esp-bt-audio-duplex`.
- [x] Do not create a helper branch.
- [x] Do not create a new PR.
- [x] Do not flash hardware without explicit user approval in the current conversation.
- [x] Do not modify `esp_i2s_source/` for this fix set.
- [x] Do not treat a successful compile as runtime proof.
- [x] Do not add quiet fallback, fake success, silent data loss, or unbounded retry behavior.
- [x] Keep HFP/profile/SCO implementation under `components/bt_manager`.
- [x] Route command behavior through public manager APIs.
- [x] Keep I2S0 TX and CVSD conversion under `components/audio_processor`.
- [x] Make added failure paths return an exact error, emit a stable record, increment an explicit counter, or fault/quarantine the owning component.
- [x] Preserve the FD-16 truth boundary: `HFP_FULL` may reserve ownership, but operational HFP downlink playback remains unavailable until FD-18 and FD-19.

No hardware was flashed during this review-fix set.

---

# RF-00 — Branch, baseline, and scope [P0]

## Completed

- [x] Confirm the target branch is `feature/esp-bt-audio-duplex`.
- [x] Record the starting baseline as `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`.
- [x] Inspect the primary full-duplex TODO.
- [x] Inspect the FD-16 closeout.
- [x] Inspect the HFP incoming callback implementation.
- [x] Inspect the HFP audio-control implementation.
- [x] Inspect the duplex policy/runtime and A2DP event integration.
- [x] Inspect the HFP command handler.
- [x] Identify the focused incoming-audio, audio-control, policy/runtime, A2DP integration, and command suites from the maintained host workflow.
- [x] Confirm the final diff remains under `esp_bt_audio_source` source/tests/docs; the temporary workflow experiment was removed and is not present in the final diff.

## Review-fix files changed from the baseline

### Production source and headers

- `esp_bt_audio_source/components/audio_processor/hfp_i2s_output_data.c`
- `esp_bt_audio_source/components/audio_processor/include/hfp_i2s_output.h`
- `esp_bt_audio_source/components/bt_manager/bt_duplex_state_core.c`
- `esp_bt_audio_source/components/bt_manager/bt_events_a2dp.c`
- `esp_bt_audio_source/components/bt_manager/bt_hfp_audio.c`
- `esp_bt_audio_source/components/bt_manager/bt_hfp_manager_fd11.c`
- `esp_bt_audio_source/components/bt_manager/include/bt_duplex_state.h`
- `esp_bt_audio_source/components/bt_manager/include/bt_duplex_state_internal.h`
- `esp_bt_audio_source/components/bt_manager/include/bt_events_a2dp.h`
- `esp_bt_audio_source/components/bt_manager/include/bt_hfp_audio.h`
- `esp_bt_audio_source/components/bt_manager/include/bt_hfp_manager.h`
- `esp_bt_audio_source/components/command_interface/cmd_handlers_hfp_fd11_v2.c`

### Host tests and fixtures

- `esp_bt_audio_source/test/host_test/mocks/mock_audio_and_btstate.c`
- `esp_bt_audio_source/test/host_test/test_bluetooth.c`
- `esp_bt_audio_source/test/host_test/test_bluetooth_shared.h`
- `esp_bt_audio_source/test/host_test/test_bt_a2dp_binding_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_duplex_state.c`
- `esp_bt_audio_source/test/host_test/test_bt_duplex_state_audio_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_concurrency.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_control.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_control_health_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_commands.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_commands_integrity_cases.c`

### Maintained documentation

- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_TODO_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`

The final closeout must compare the exact final documentation head against the starting baseline again before recording its definitive file list.

---

# RF-01 — HFP incoming callback concurrency correctness [P0]

## Implemented contract

ESP-IDF callback serialization is not assumed. The application enforces a nonblocking single-callback execution contract:

- [x] Add an `atomic_flag` callback overlap gate.
- [x] Acquire the gate without spinning or blocking.
- [x] Reject a concurrent callback immediately.
- [x] Increment a saturating `callback_overlap_rejections` counter.
- [x] Release the ESP-IDF-owned audio buffer on production overlap rejection.
- [x] Keep the callback frame/byte/error 64-bit counters single-writer while the gate is held.
- [x] Preserve process-lifetime callback maximum-duration and over-budget counters.
- [x] Preserve the existing no-allocation, no-wait, no-direct-I2S, and no-per-frame-log constraints.
- [x] Expose overlap rejection through the manager snapshot and `HFP STATS` as `OVERLAP_REJECT`.
- [x] Keep `HFP RESETSTATS` baseline-relative rather than rewriting live lifetime metrics.

## Tests present

- [x] Pause one callback inside the focused fixture.
- [x] Launch four overlapping callback attempts.
- [x] Verify overlapping callbacks are rejected rather than serialized by a wait.
- [x] Verify rejection accounting is exact.
- [x] Verify the accepted callback remains the only writer of normal callback counters.
- [x] Retain callback lifetime/reset regression coverage.

## Remaining validation

- [ ] Pass the incoming-audio sanitizer suite at the final documentation head.
- [ ] Pass the callback allocation-symbol gate at the final documentation head.
- [ ] Measure overlap, callback duration, and callback/task stack margins on the target board.

## Acceptance status

- [x] The source design prevents lost multi-writer increments by rejecting overlap before normal counter updates.
- [x] A reader cannot observe a normal counter being concurrently written by multiple callbacks.
- [x] Overlap is visible rather than silently discarded.
- [ ] Final host CI evidence is still required.

---

# RF-02 — Same-peer stale A2DP event hardening [P0]

## Implemented binding

A raw duplex generation cannot remain fixed for the entire A2DP connection because a legitimate HFP audio start rotates the audio generation. The production integration therefore uses an explicit A2DP connection-lifecycle binding:

- [x] Bind the A2DP peer.
- [x] Bind an A2DP lifecycle serial and source serial.
- [x] Track the currently associated duplex generation.
- [x] Capture lifecycle serial/peer before event processing.
- [x] Validate the same lifecycle and peer before refreshing the current duplex generation after a legitimate HFP audio-generation rotation.
- [x] Require an existing binding for A2DP audio events.
- [x] Reject missing-binding events fail-closed.
- [x] Reject wrong-peer events without mutating the valid binding.
- [x] Reject stale reconnect-boundary/source-serial events.
- [x] Clear a disconnect binding only after the disconnect event is applied or explicitly rejected.
- [x] Preserve A2DP-only behavior when duplex/HFP mode is disabled.

## Tests present

- [x] A2DP audio with no lifecycle binding is rejected.
- [x] Legitimate HFP audio-generation rotation within the same A2DP lifecycle is accepted after lifecycle validation.
- [x] Wrong-peer events are rejected.
- [x] Stale source-serial/reconnect-boundary events are rejected.
- [x] Integration fixtures expose binding snapshot/reset behavior explicitly.

## Residual lower-layer limitation

ESP-IDF A2DP callbacks do not provide a cryptographically unique connection-instance token. The application can enforce its captured lifecycle serial/source-serial boundary, but it cannot invent a lower-layer token that the API does not supply. This limitation must remain documented rather than being hidden by stamping every event with whatever generation is current.

## Remaining validation

- [ ] Pass A2DP integration and FD-16 policy/order suites at the final documentation head.
- [ ] Confirm the final closeout describes the ESP-IDF token limitation and the exact application-level boundary.

## Acceptance status

- [x] Production events no longer blindly self-stamp with the current generation.
- [x] A captured stale lifecycle/source serial cannot mutate the newer binding.
- [ ] Final host CI evidence is still required.

---

# RF-03 — Health-report failure visibility [P0]

## Implemented diagnostics

- [x] Instrument the authoritative `bt_duplex_set_health()` failure boundary.
- [x] Add process-lifetime `health_report_failures`.
- [x] Add `last_health_report_error`.
- [x] Preserve the primary operation error when health reporting is a secondary failure.
- [x] Avoid recursive health reporting for a health-report failure.
- [x] Preserve the health-report failure counter across session rotations.
- [x] Make `HFP RESETSTATS` reporting baseline-relative without erasing the lifetime source.
- [x] Expose the counter and last error in diagnostics and `HFP STATS`.
- [x] Add test-only injection of a health-report result.

## Tests present

- [x] Inject a health-report failure without mutating the intended health state.
- [x] Verify failure count and last error visibility.
- [x] Exercise connect-timeout, disconnect-timeout, and unexpected-remote-SCO cleanup call paths through focused audio-control health cases.
- [x] Verify the primary operation failure is not converted to success.
- [x] Verify command statistics include health-report failure visibility.

## Remaining validation

- [ ] Pass duplex-state, audio-control, diagnostics, and command suites at the final documentation head.

## Acceptance status

- [x] A health-reporting failure is no longer silently discarded.
- [x] The visibility mechanism can itself report failure without recursion.
- [ ] Final host CI evidence is still required.

---

# RF-04 — `HFP AUDIO START/STOP` status-unavailable semantics [P1]

## Implemented protocol

When the lower audio operation succeeds but the follow-up status snapshot fails, the command now emits an error record that cannot be confused with ordinary fully observed success:

```text
ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=START,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=<exact esp_err>
ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=STOP,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=<exact esp_err>
```

- [x] Preserve the fact that the lower operation succeeded.
- [x] Return top-level `ERR` because the requested command result is not fully observable.
- [x] Include the exact `esp_err_to_name()` value.
- [x] Do not fabricate a status snapshot or zero-valued fields.
- [x] Keep ordinary success output unchanged when status retrieval succeeds.
- [x] Fail visibly with `AUDIO_STATUS_LINE_TOO_LONG` if the bounded record cannot be formatted.

## Tests present

- [x] START succeeds at the lower layer and status retrieval fails.
- [x] STOP succeeds at the lower layer and status retrieval fails.
- [x] The output cannot match ordinary `AUDIO_STARTED` or `AUDIO_STOPPED` success.
- [x] The exact status error string is present.
- [x] Ordinary success behavior remains covered.

## Remaining validation

- [ ] Pass the HFP command sanitizer suite at the final documentation head.

## Acceptance status

- [x] A client cannot mistake status-unavailable for ordinary fully observed success.
- [x] No status fields are fabricated.
- [ ] Final host CI evidence is still required.

---

# RF-05 — Callback stack, cadence, and resource gates [P1 software; P0 hardware]

## Software bounds implemented

- [x] Define the CVSD conversion factor in the public I2S output contract.
- [x] Define the maximum accepted incoming CVSD samples.
- [x] Define fixed aligned and converted array sizes from those constants.
- [x] Define the combined callback audio-array budget.
- [x] Add compile-time assertions that reject frame/conversion changes exceeding the budget.
- [x] Current aligned input array: 120 `int16_t` samples / 240 bytes.
- [x] Current converted array: 240 `int16_t` samples / 480 bytes.
- [x] Current combined fixed audio arrays: 720 bytes.
- [x] Static ceiling: 1024 bytes.
- [x] Maintain callback last/max/over-budget diagnostics.
- [x] Maintain incoming accept/drop/invalid/stale/bad/unsupported and overlap counters.
- [x] Add `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`.

## Compile-only evidence

GitHub Actions device compile run `30407430615` validated commit `e48341ae665781dd6da6e40c4137bfbead4d1205`:

- [x] ESP-IDF v5.5.1 compile-only job passed.
- [x] Application image: 1,025,808 bytes (`0xFA710`).
- [x] Factory app partition: 1,769,472 bytes (`0x1B0000`).
- [x] Remaining partition headroom: 743,664 bytes (`0xB58F0`), 42% free.
- [x] No hardware was flashed.

The build emitted an unrelated existing warning in `bt_scan.c` for `any_empty_names` being set but unused. That warning is not converted into a review-fix success claim and remains separate cleanup debt.

## Hardware gates still pending

- [ ] Obtain explicit approval before flashing.
- [ ] Run real CVSD SCO traffic with the target earbuds.
- [ ] Capture `HFP STATS` before, during, and after SCO.
- [ ] Measure BtAppTask minimum-free-stack bytes.
- [ ] Measure HFP I2S writer minimum-free-stack bytes.
- [ ] Measure callback maximum/p99 duration and over-budget count.
- [ ] Confirm overlap rejection remains zero in normal operation or investigate every nonzero value.
- [ ] Measure heap minimum-free and largest internal block during lifecycle churn.
- [ ] Confirm no watchdog, stack overflow, callback stall, ring overflow, sustained underflow, or teardown race.

## Acceptance status

- [x] Software bounds are explicit and statically enforced.
- [x] Compile-only validation passed.
- [ ] Host CI at the final documentation head is pending.
- [ ] Hardware resource acceptance is pending.

---

# RF-06 — Reconcile the primary full-duplex TODO [P1]

- [x] Update `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md` on the feature branch.
- [x] Distinguish software implementation, prior phase host validation, latest compile-only validation, hardware pending, and future phases.
- [x] Mark FD-08 through FD-13 software work accurately.
- [x] Mark FD-16 software work accurately.
- [x] Keep FD-14, FD-15, FD-17, FD-20, and FD-21 hardware work incomplete.
- [x] Keep FD-18 and FD-19 unimplemented.
- [x] Keep later phase-wide failure/recovery/resource/soak acceptance open.
- [x] Reference only documents that exist at their exact repository paths.
- [x] Record that the latest review-fix host CI remains pending.

The reconciled primary TODO commit is `fadfe569f11f7676dfae7f5603e2d11ba721fe09`.

---

# RF-07 — Focused regression and final static sweeps [P0]

## Host workflow coverage exists

The maintained `.github/workflows/ci-host-tests.yml` includes:

- HFP incoming-audio sanitizer tests;
- HFP audio-control sanitizer tests;
- HFP command tests through CTest;
- FD-16 duplex policy sanitizer tests;
- A2DP integration through the Bluetooth host target;
- changed-Python lint;
- Python unit tests;
- the full CTest suite.

## Required final run

- [ ] HFP incoming-audio sanitizer suite passes at the final documentation head.
- [ ] HFP audio-control sanitizer suite passes at the final documentation head.
- [ ] HFP command suite passes at the final documentation head.
- [ ] FD-16 policy/runtime suite passes at the final documentation head.
- [ ] A2DP lifecycle-binding integration tests pass at the final documentation head.
- [ ] Full CTest passes at the final documentation head.
- [ ] Changed-Python lint and Python unit gates pass.
- [ ] Record the exact workflow run ID, commit SHA, job conclusion, and test counts.

## Device compile-only validation

- [x] ESP-IDF v5.5.1 compile-only build passed for `e48341ae665781dd6da6e40c4137bfbead4d1205`.
- [x] Image and partition headroom are recorded under RF-05.
- [x] No hardware was flashed.
- [ ] Re-run compile-only validation only if the final documentation changes unexpectedly affect production/build inputs; documentation-only commits do not substitute for host CI.

## Final manual/static sweeps still required

- [ ] Search the callback path for application allocation, logging, blocking waits, and direct I2S calls.
- [ ] Search modified production code for ignored `esp_err_t` results.
- [ ] Search for external live-task deletion shortcuts.
- [ ] Search modified code for `volatile` synchronization.
- [ ] Search zero-fill/silence paths for missing counters.
- [ ] Search command paths for success after lower task/profile/I2S/status failure.
- [ ] Search A2DP/HFP state mutation for missing peer, generation, lifecycle, or binding validation.
- [ ] Confirm no generated build/log artifacts are committed.

## Acceptance status

- [x] Compile-only validation passed.
- [ ] Focused host tests at the final documentation head are pending.
- [ ] Full host CI at the final documentation head is pending.
- [ ] Final static/manual sweeps are pending.
- [x] Hardware validation is listed as pending rather than complete.

---

# RF-08 — Closeout and handoff [P1 blocked]

Do not create the final closeout until RF-07 host CI and final sweeps pass.

Required path after validation:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md`

The final closeout must contain:

- [ ] Starting baseline SHA `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`.
- [ ] Final validated documentation/code SHA.
- [ ] Exact final changed-file list.
- [ ] Summary of RF-01 through RF-06.
- [ ] Exact focused and full host workflow results.
- [ ] Compile-only run `30407430615` and firmware metrics.
- [ ] Explicit statement that no hardware was flashed.
- [ ] Pending FD-14, FD-15, FD-17, runtime resource, and soak gates.
- [ ] Explicit statement that FD-18/FD-19 HFP downlink is not implemented.
- [ ] A2DP lower-layer connection-token limitation and application lifecycle-binding boundary.
- [ ] Updated primary TODO status.
- [ ] Confirmation that every referenced assistant-created document exists.

## Acceptance status

- [ ] Closeout exists at the required path.
- [x] No new helper branch was created.
- [x] No new PR was created.
- [x] No hardware was flashed.
- [ ] Final closeout is blocked only by RF-07 evidence/sweeps, not by an unreported failure.

---

# Definition of done for this review-fix TODO

- [x] Callback overlap is concurrency-safe, nonblocking, rejected visibly, and covered by focused tests.
- [x] A2DP policy events use an explicit connection-lifecycle binding rather than blindly using the current generation.
- [x] Health-reporting failures are visible through diagnostics and tests.
- [x] `HFP AUDIO START/STOP` status-unavailable results cannot be mistaken for ordinary success.
- [x] Callback stack-array bounds and the software resource budget are documented and statically enforced.
- [x] The primary full-duplex TODO distinguishes completed software from pending hardware/future work.
- [ ] Focused host tests pass at the final documentation head.
- [ ] Full host CI passes at the final documentation head.
- [x] ESP-IDF v5.5.1 compile-only build passes at the reviewed implementation head.
- [x] Hardware was not flashed without explicit approval.
- [ ] Final static/manual sweeps pass.
- [ ] The final closeout document is committed.
