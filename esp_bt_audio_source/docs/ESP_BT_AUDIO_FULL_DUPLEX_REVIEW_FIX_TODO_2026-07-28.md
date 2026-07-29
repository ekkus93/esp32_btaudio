# ESP32 Bluetooth Audio Full-Duplex Review Fix TODO

**Created:** 2026-07-28  
**Status updated:** 2026-07-28  
**Target branch:** `feature/esp-bt-audio-duplex`  
**Primary TODO:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`  
**Starting baseline:** `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`  
**Reviewed production/test head before this documentation commit:** `392894daa1b930817639da58c4ba2d590f4b8013`  
**Last verified compile-only implementation head:** `e48341ae665781dd6da6e40c4137bfbead4d1205`  
**Scope:** Correct the concrete post-FD-16 concurrency, stale-event, failure-visibility, command-integrity, and resource-accounting findings without claiming hardware validation or the unimplemented FD-18/FD-19 HFP downlink.

## Status rules

- `[x]` means the source, test, or documentation item exists and was inspected.
- A checked implementation or test item does not mean the current head passed CI.
- The compile-only run for `e48341ae...` is historical evidence only. Production source changed afterward.
- Fresh host CI and fresh ESP-IDF compile-only CI are required for the final documentation head.
- Hardware-gated work remains unchecked until physical evidence exists.
- Do not create the final closeout until all required software, host-CI, and compile-only gates pass.

---

# RF-00 — Branch, baseline, and scope [complete]

- [x] Work directly on `feature/esp-bt-audio-duplex`.
- [x] Do not create a helper branch.
- [x] Do not create a new PR.
- [x] Do not modify `esp_i2s_source/`.
- [x] Do not flash hardware without explicit user approval.
- [x] Record starting baseline `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`.
- [x] Keep the review-fix changes under `esp_bt_audio_source/` source, tests, and maintained docs.
- [x] Confirm no generated build products or logs appear in the baseline-to-code-head changed-file list.
- [x] Confirm the temporary workflow experiment is absent from the final diff.

No hardware was flashed. No branch or PR was created.

## Baseline-to-reviewed-code-head changed files

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

- `esp_bt_audio_source/test/host_test/mocks/include/esp_bt.h`
- `esp_bt_audio_source/test/host_test/mocks/include/mock_uart.h`
- `esp_bt_audio_source/test/host_test/mocks/mock_audio_and_btstate.c`
- `esp_bt_audio_source/test/host_test/mocks/mock_uart.c`
- `esp_bt_audio_source/test/host_test/test_bluetooth.c`
- `esp_bt_audio_source/test/host_test/test_bluetooth_shared.h`
- `esp_bt_audio_source/test/host_test/test_bt_a2dp_binding_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_duplex_state.c`
- `esp_bt_audio_source/test/host_test/test_bt_duplex_state_audio_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_duplex_state_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_concurrency.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_control.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_audio_control_health_cases.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_commands.c`
- `esp_bt_audio_source/test/host_test/test_bt_hfp_commands_integrity_cases.c`

### Maintained documentation

- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_TODO_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`

The final closeout must compare its exact final SHA against the starting baseline again because the documentation commits themselves advance the branch.

---

# RF-01 — HFP incoming callback concurrency [software complete]

## Implemented contract

- [x] Use a nonblocking `atomic_flag` callback-overlap gate.
- [x] Reject overlap immediately; never spin or wait in the callback.
- [x] Release the ESP-IDF-owned buffer exactly once when a production overlap is rejected.
- [x] Increment saturating `callback_overlap_rejections`.
- [x] Keep ordinary callback frame/byte/error counters single-writer while the gate is held.
- [x] Preserve process-lifetime callback maximum-duration and over-budget counters.
- [x] Expose overlap through manager diagnostics and `HFP STATS` as `OVERLAP_REJECT`.
- [x] Preserve baseline-relative reset semantics without rewriting process-lifetime sources.

## Focused coverage present

- [x] Pause one accepted callback.
- [x] Launch four overlapping callbacks.
- [x] Verify immediate rejection rather than waiting or serialization.
- [x] Verify exact overlap accounting.
- [x] Verify only the accepted callback mutates ordinary callback counters.

## Static callback sweep

- [x] No application allocation.
- [x] No mutex or semaphore wait.
- [x] No direct I2S driver call.
- [x] No per-frame logging.
- [x] No `volatile` synchronization.
- [x] No unbounded retry loop.

## Remaining gates

- [ ] Pass the final incoming-audio sanitizer suite.
- [ ] Pass the callback allocation-symbol gate.
- [ ] Measure callback cadence, p99/max duration, overlap, and stack margins on hardware.

---

# RF-02 — Same-peer stale A2DP events and atomic base-state commit [software complete]

## Implemented identity contract

ESP-IDF v5.5 supplies `conn_hdl` in A2DP connection-state and audio-state callback records. That event-owned handle is now the connection-instance token.

- [x] Bind peer MAC, ESP-IDF `conn_hdl`, application lifecycle serial, and current duplex generation.
- [x] Require an existing binding for A2DP audio events.
- [x] Reject missing-binding events fail-closed.
- [x] Reject wrong-peer events without mutating the valid connection.
- [x] Reject same-peer events carrying an old connection handle after reconnect.
- [x] Refresh duplex generation only after peer, lifecycle serial, and `conn_hdl` match.
- [x] Permit legitimate HFP generation rotation within one matching A2DP connection.
- [x] Clear a disconnect binding only when peer, serial, and handle match.
- [x] Preserve A2DP-only operation with generation zero when no duplex session exists.

## Atomic base-state commit

- [x] Acquire `bt_ctx` once for identity validation/binding and base-state mutation.
- [x] Capture user callbacks and callback arguments while the same lock is held.
- [x] Invoke callbacks, connection forwarding, autostart, and policy only after successful commit and unlock.
- [x] Remove the former second `bt_ctx_lock()` from connected, disconnected, started, stopped, and suspended handlers.
- [x] A lock failure occurs before binding creation, `bt_ctx` mutation, user callback, autostart, connection forwarding, or policy forwarding.
- [x] Do not add a recovery fallback that guesses whether partial state was committed.

## Focused coverage present

- [x] First profile event creates a binding.
- [x] Disconnect/reconnect rotates the application lifecycle serial.
- [x] Audio without a lifecycle binding is rejected and counted.
- [x] Legitimate HFP generation rotation is accepted.
- [x] Wrong peer is rejected before base-state mutation.
- [x] Old-handle audio cannot mark a newer same-peer connection as playing.
- [x] Old-handle disconnect cannot disconnect the newer connection or invoke its callback.
- [x] Forced missing `bt_ctx` mutex rejects a CONNECTED event with no binding, state mutation, callback, forwarding, policy call, or autostart attempt.

## Remaining gates

- [ ] Pass Bluetooth/A2DP integration tests at the final head.
- [ ] Pass FD-16 policy/runtime ordering tests at the final head.
- [ ] Pass ESP-IDF v5.5.1 compile-only validation against the real `conn_hdl` API fields.

---

# RF-03 — Health-report failure visibility [software complete]

- [x] Keep process-lifetime health-report failure count outside the authoritative state mutex.
- [x] Use lock-free 32-bit atomics suitable for the ESP32 target.
- [x] Expose the saturating count as `uint64_t` through the public diagnostics API.
- [x] Preserve `last_health_report_error` independently of the state mutex.
- [x] Record invalid argument, state-lock, injected, identity, transition, and unlock failures.
- [x] Preserve the primary operation error when health reporting is secondary.
- [x] Avoid recursive health reporting.
- [x] Preserve values across normal session rotation and deinit/reinit.
- [x] Keep test reset explicit.
- [x] Expose the count and exact last error through manager diagnostics and `HFP STATS`.

## Focused coverage present

- [x] Inject post-lock health-report failure without mutating intended health state.
- [x] Reject an invalid enum before lock and verify diagnostics increment.
- [x] Verify exact last error.
- [x] Exercise audio-control timeout and cleanup callers.
- [x] Verify the primary operation failure is not converted to success.

## Remaining gates

- [ ] Pass duplex-state, audio-control, diagnostics, and command suites at the final head.
- [ ] Compile the atomics against ESP-IDF v5.5.1 for ESP32.

---

# RF-04 — `HFP AUDIO START/STOP` status-unavailable semantics [software complete]

When the lower audio operation succeeds but the follow-up status snapshot fails:

```text
ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=START,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=<exact esp_err>
ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=STOP,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=<exact esp_err>
```

- [x] Preserve the fact that the lower operation succeeded.
- [x] Return wire-level `ERR`, not ordinary fully observed success.
- [x] Include the exact `esp_err_to_name()` result.
- [x] Do not fabricate a status snapshot or zero-valued fields.
- [x] Preserve ordinary success output when status retrieval succeeds.
- [x] Fail visibly with `AUDIO_STATUS_LINE_TOO_LONG` if formatting fails.
- [x] Propagate `cmd_send_response()` transport failure from both status-unavailable branches.
- [x] Cover START failure-to-observe, STOP failure-to-observe, ordinary success, exact error text, and forced UART write failure.

## Remaining gate

- [ ] Pass the final HFP command sanitizer/CTest suite.

---

# RF-05 — Callback resources and I2S accounting [software complete; hardware pending]

## Callback resource bounds

- [x] Define CVSD conversion factor and maximum input samples in the public I2S contract.
- [x] Derive aligned and converted fixed-array sizes from those constants.
- [x] Define combined callback audio-array bytes.
- [x] Enforce a compile-time 1024-byte ceiling.
- [x] Current aligned input: 240 bytes.
- [x] Current converted output: 480 bytes.
- [x] Current combined fixed audio arrays: 720 bytes.
- [x] Maintain callback timing and accept/drop/error/overlap counters.
- [x] Maintain `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`.

## I2S accounting hardening

- [x] Acquire the writer state lock before consuming the SPSC ring.
- [x] Keep ring consumption, zero-fill, silence/underflow accounting, bounded I2S write, lost-byte accounting, and fault/quarantine transition in one bounded critical section.
- [x] Do not consume PCM or insert silence when authoritative accounting cannot be acquired.
- [x] Avoid an unlock/re-lock window after PCM consumption.
- [x] Keep the producer callback independent of the writer mutex.
- [x] Require a finite nonzero write timeout; current default is 20 ms.

## Historical compile-only evidence

Run `30407430615` passed for `e48341ae665781dd6da6e40c4137bfbead4d1205`:

- application image: 1,025,808 bytes (`0xFA710`);
- factory partition: 1,769,472 bytes (`0x1B0000`);
- remaining headroom: 743,664 bytes (`0xB58F0`), 42% free;
- no hardware flashed.

Production source changed after that SHA, so this run does not validate the current head.

## Remaining gates

- [ ] Pass HFP I2S output host tests at the final head.
- [ ] Pass a fresh ESP-IDF v5.5.1 compile-only workflow.
- [ ] Record current image size, partition size, and headroom.
- [ ] Measure heap, stack, callback timing, ring, underflow, timeout, and overlap behavior on hardware.

---

# RF-06 — Primary TODO reconciliation [software status reconciled]

- [x] Distinguish implemented software, older phase CI, current-head CI, compile-only evidence, hardware pending, and future work.
- [x] Mark FD-08 through FD-13 and FD-16 software status accurately.
- [x] Keep FD-14, FD-15, FD-17, FD-20, and FD-21 hardware work incomplete.
- [x] Keep FD-18 and FD-19 unimplemented.
- [x] Keep phase-wide failure/recovery/resource/soak acceptance open.
- [x] Reference only files that exist at exact repository paths.
- [x] Reconcile the two final software blockers as resolved at code/test head `392894daa1b930817639da58c4ba2d590f4b8013`.
- [ ] Update workflow IDs, final documentation SHA, and final validation status after fresh CI completes.

---

# RF-07 — Final regression and static sweeps [static complete; CI pending]

## Static/manual sweep results after the final production change

- [x] Callback allocation/blocking/direct-I2S/per-frame-log sweep passed.
- [x] `volatile` synchronization sweep passed for modified production files.
- [x] Live-task deletion shortcut sweep passed for modified production files.
- [x] Generated build/log artifact sweep passed.
- [x] Status-unavailable fake-success and transport-error sweep passed.
- [x] Same-peer stale A2DP events use real ESP-IDF `conn_hdl` identity.
- [x] A2DP identity and base-state commit use one lock acquisition.
- [x] Health-report diagnostics do not depend on the mutex whose failure they diagnose.
- [x] Silent/uncounted I2S zero-fill was removed from the writer path.
- [x] HFP lower-layer callback registration success is published without a post-registration mutex reacquire.
- [x] Reentrant/concurrent registration cannot call the lower registration API twice while publication is pending.
- [x] Baseline-to-reviewed-code-head file audit contains only source, tests, and maintained docs.

## Required host workflow

The maintained `.github/workflows/ci-host-tests.yml` includes the required sanitizer suites, Python gates, and full CTest run.

- [ ] HFP incoming-audio sanitizer suite passes.
- [ ] HFP audio-control sanitizer suite passes.
- [ ] HFP command suite passes.
- [ ] Duplex-state tests pass.
- [ ] FD-16 policy/runtime suite passes.
- [ ] Bluetooth/A2DP integration and stale-handle tests pass.
- [ ] HFP I2S output tests pass.
- [ ] Changed-Python lint passes.
- [ ] Python unit tests pass.
- [ ] Full CTest passes.
- [ ] Record exact workflow run ID, final SHA, job conclusion, and test counts.

## Required device compile-only workflow

- [ ] ESP-IDF v5.5.1 compile-only workflow passes at the final documentation head.
- [ ] Record exact workflow run ID and final SHA.
- [ ] Record image size, partition size, and remaining headroom.
- [ ] Confirm the workflow performs no flash.

---

# RF-08 — Closeout and handoff [blocked only on fresh CI/compile]

Do not create:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md`

until the final host workflow and final ESP-IDF compile-only workflow pass.

The eventual closeout must include:

- [ ] Starting baseline SHA.
- [ ] Final validated documentation SHA.
- [ ] Exact final changed-file list.
- [ ] RF-01 through RF-06 summaries.
- [ ] Exact host workflow result and test counts.
- [ ] Exact compile-only workflow result and firmware metrics.
- [ ] Explicit no-flash statement.
- [ ] Pending FD-14, FD-15, FD-17, resource, and soak gates.
- [ ] Explicit FD-18/FD-19 not-implemented statement.
- [ ] ESP-IDF A2DP `conn_hdl` identity and atomic base-state contract.
- [ ] Atomic HFP callback-registration publication contract.
- [ ] Updated primary TODO status.
- [ ] Confirmation every referenced assistant-created file exists.

---

# Current definition-of-done status

- [x] Callback overlap is nonblocking, fail-closed, visible, and covered by focused tests.
- [x] Same-peer reconnect events use the ESP-IDF event-owned connection handle.
- [x] A2DP identity, binding, and base-state mutation commit atomically or not at all.
- [x] Health-report failure diagnostics are lock-independent and visible.
- [x] Status-unavailable results cannot masquerade as success, including transport failure.
- [x] Callback stack-array bounds are explicit and statically enforced.
- [x] I2S writer zero-fill and write-loss accounting is authoritative when the bounded critical section is acquired.
- [x] HFP callback registration cannot succeed below the application while publication fails on a later mutex reacquire.
- [x] Final post-production static/manual sweeps are complete.
- [ ] Final host CI passes.
- [ ] Final ESP-IDF v5.5.1 compile-only CI passes.
- [x] Hardware was not flashed.
- [ ] Final closeout exists.
