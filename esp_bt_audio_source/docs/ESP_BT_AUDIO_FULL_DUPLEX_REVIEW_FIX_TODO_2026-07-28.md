# ESP32 Bluetooth Audio Full-Duplex Review Fix TODO

**Created:** 2026-07-28  
**Status updated:** 2026-07-28  
**Target branch:** `feature/esp-bt-audio-duplex`  
**Primary TODO:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`  
**Starting baseline:** `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`  
**Current reviewed code head before this documentation commit:** `e98153195b40212d1e7b96ff48bb27950bb8b264`  
**Last verified compile-only implementation head:** `e48341ae665781dd6da6e40c4137bfbead4d1205`  
**Scope:** Correct concrete post-FD-16 safety/integrity findings without claiming hardware validation or the unimplemented FD-18/FD-19 HFP downlink.

## Status rules

- `[x]` means the source, test, or documentation item exists and was inspected.
- A checked implementation item does not mean the current head passed CI.
- The compile-only run for `e48341ae...` is historical evidence only. Production code changed after that SHA, so the current head requires a new host workflow and a new ESP-IDF compile-only workflow.
- Hardware-gated work remains unchecked until physical evidence exists.
- Do not create the final closeout while any P0 software blocker, required CI result, compile result, or final static sweep is open.

---

# RF-00 — Branch, baseline, and scope [P0]

- [x] Work directly on `feature/esp-bt-audio-duplex`.
- [x] Do not create a helper branch.
- [x] Do not create a new PR.
- [x] Do not modify `esp_i2s_source/`.
- [x] Do not flash hardware without explicit user approval in the current conversation.
- [x] Keep changes under `esp_bt_audio_source` source, tests, and maintained documentation.
- [x] Record starting baseline `0f77b149f4479252ffe32d67bf4cc9640f9b4a52`.
- [x] Confirm no generated build/log artifacts are present in the baseline-to-head changed-file list.
- [x] Confirm the temporary workflow experiment is absent from the final diff.

No hardware was flashed. No new branch or PR was created.

---

# RF-01 — HFP incoming callback concurrency correctness [P0]

## Implemented

- [x] Add a nonblocking `atomic_flag` overlap gate.
- [x] Reject overlap immediately; never spin or wait in the callback.
- [x] Release the ESP-IDF-owned audio buffer exactly once on production overlap rejection.
- [x] Add saturating `callback_overlap_rejections` accounting.
- [x] Keep ordinary callback frame/byte/error counters single-writer while the gate is held.
- [x] Preserve process-lifetime maximum callback duration and over-budget counters.
- [x] Expose overlap rejection through manager diagnostics and `HFP STATS` as `OVERLAP_REJECT`.
- [x] Preserve baseline-relative reset semantics without rewriting live lifetime sources.

## Focused coverage present

- [x] Pause one accepted callback.
- [x] Launch four overlapping callbacks.
- [x] Verify immediate rejection rather than serialization.
- [x] Verify exact rejection count.
- [x] Verify only the accepted callback mutates ordinary callback counters.

## Static callback sweep

- [x] No application allocation in the hard callback path.
- [x] No mutex/semaphore wait in the hard callback path.
- [x] No direct I2S driver call in the hard callback path.
- [x] No resampling beyond the bounded pure conversion helper.
- [x] No per-frame logging.
- [x] No `volatile` synchronization.
- [x] No unbounded callback retry loop.

## Remaining

- [ ] Pass the incoming-audio sanitizer suite at the final code/documentation head.
- [ ] Pass the callback allocation-symbol gate at the final head.
- [ ] Measure callback p99/max duration, overlap, and task/callback stack margins on hardware.

---

# RF-02 — Same-peer stale A2DP event hardening [P0]

## Implemented identity contract

ESP-IDF v5.5 supplies `conn_hdl` in both A2DP connection-state and audio-state callback records. The application now uses that event-owned handle instead of inventing a current-generation token.

- [x] Bind peer MAC, ESP-IDF `conn_hdl`, application lifecycle serial, and current duplex generation.
- [x] Validate peer and `conn_hdl` before mutating `bt_ctx`, invoking user callbacks, invoking autostart, or forwarding policy state.
- [x] Require an existing binding for A2DP audio events.
- [x] Reject missing-binding events fail-closed.
- [x] Reject wrong-peer events without mutating the valid connection.
- [x] Reject stale same-peer events carrying the old connection handle after reconnect.
- [x] Refresh duplex generation only after peer, lifecycle serial, and connection handle match.
- [x] Permit legitimate HFP audio-generation rotation inside one A2DP connection.
- [x] Clear a disconnect binding only when its peer, lifecycle serial, and connection handle match.
- [x] Preserve A2DP-only mode with generation zero when no duplex session exists.

## Focused coverage present

- [x] First profile event creates a binding.
- [x] Disconnect/reconnect rotates application lifecycle serial.
- [x] Audio without a binding is rejected and counted.
- [x] Legitimate HFP generation rotation is accepted.
- [x] Wrong peer is rejected before base-state mutation.
- [x] Same-peer audio from the old `conn_hdl` cannot mark the new connection as playing.
- [x] Same-peer disconnect from the old `conn_hdl` cannot disconnect the new connection or invoke the disconnect callback.

## Remaining P0 control-plane issue

- [ ] If `bt_ctx_lock()` fails after identity acceptance, connected/disconnected/audio handlers must fail closed before invoking downstream callbacks or policy, and must not leave a newly created binding looking usable.
- [ ] Add deterministic host failure injection for the base-state lock failure path or another reviewable proof that the failure is explicit and leaves no partial state.

This is a rare infrastructure-failure path, but the current code still continues after some failed `bt_ctx_lock()` calls. It therefore remains a closeout blocker rather than being described as harmless.

## Remaining validation

- [ ] Pass Bluetooth/A2DP integration tests at the final head.
- [ ] Pass FD-16 policy/runtime ordering tests at the final head.
- [ ] Pass ESP-IDF v5.5.1 compile-only validation using the real A2DP API definitions.

---

# RF-03 — Health-report failure visibility [P0]

## Implemented

- [x] Move process-lifetime health-report failure count outside the authoritative state mutex.
- [x] Use lock-free 32-bit atomics suitable for the ESP32 target.
- [x] Expose the saturating count as `uint64_t` through the public diagnostics API.
- [x] Preserve `last_health_report_error` independently of the state mutex.
- [x] Record invalid enum/text failures before lock acquisition.
- [x] Record state-lock failure.
- [x] Record injected, identity-validation, illegal-downgrade, and unlock failures.
- [x] Preserve the primary operation error if an unlock error also occurs.
- [x] Avoid recursive health reporting.
- [x] Preserve process-lifetime values across ordinary session rotation and deinit/reinit.
- [x] Keep test reset explicit and isolated.
- [x] Expose count and last error through manager diagnostics and `HFP STATS`.

## Focused coverage present

- [x] Inject a post-lock report failure without mutating intended health state.
- [x] Reject an invalid health enum before lock and verify diagnostics still increment.
- [x] Verify exact last error.
- [x] Exercise audio-control timeout/cleanup callers.
- [x] Verify the primary operation failure is not converted to success.

## Remaining

- [ ] Pass duplex-state, audio-control, diagnostics, and command suites at the final head.
- [ ] Compile current atomics against ESP-IDF v5.5.1 for ESP32.

---

# RF-04 — `HFP AUDIO START/STOP` status-unavailable semantics [P1]

## Implemented wire contract

When the lower audio operation succeeds but the follow-up status snapshot fails:

```text
ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=START,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=<exact esp_err>
ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=STOP,LOWER_OPERATION=SUCCEEDED,STATUS_ERROR=<exact esp_err>
```

- [x] Preserve the fact that the lower operation succeeded.
- [x] Return a wire-level `ERR`, not ordinary fully observed success.
- [x] Include the exact `esp_err_to_name()` result.
- [x] Do not fabricate a status snapshot or zero-valued fields.
- [x] Preserve ordinary success output when status retrieval succeeds.
- [x] Fail visibly with `AUDIO_STATUS_LINE_TOO_LONG` if the bounded line cannot be formatted.
- [x] Propagate `cmd_send_response()` transport failure from both status-unavailable branches.

## Focused coverage present

- [x] START lower operation succeeds and status retrieval fails.
- [x] STOP lower operation succeeds and status retrieval fails.
- [x] Neither response can match `AUDIO_STARTED`/`AUDIO_STOPPED` success.
- [x] Exact status error is present.
- [x] Forced UART write failure propagates to `cmd_execute()` as a command transport error.

## Remaining

- [ ] Pass the HFP command sanitizer/CTest suite at the final head.

---

# RF-05 — Callback stack and I2S resource/accounting gates [P1 software; P0 hardware]

## Callback resource bounds implemented

- [x] Define CVSD conversion factor and maximum input samples in the public I2S contract.
- [x] Derive fixed aligned and converted array sizes from those constants.
- [x] Define combined callback audio-array bytes.
- [x] Enforce a compile-time 1024-byte ceiling.
- [x] Current aligned input array: 240 bytes.
- [x] Current converted output array: 480 bytes.
- [x] Current combined fixed audio arrays: 720 bytes.
- [x] Maintain last/max/over-budget callback timing diagnostics.
- [x] Maintain accept/drop/invalid/inactive/stale/bad/unsupported/ring-reject/overlap counters.
- [x] Maintain `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`.

## I2S accounting hardening implemented after the prior compile

- [x] Acquire the writer state lock before consuming the SPSC ring.
- [x] Keep ring consumption, zero-fill, silence counters, underflow counters, bounded I2S write, lost-byte accounting, and fault/quarantine transition in one bounded critical section.
- [x] Do not consume PCM or insert silence when authoritative accounting cannot be acquired.
- [x] Avoid an unlock/re-lock window after PCM has been consumed.
- [x] Keep the producer callback independent of the writer mutex.
- [x] Use the configured finite write timeout; default is 20 ms and zero is rejected by configuration validation.

## Historical compile-only evidence

Run `30407430615` passed for `e48341ae665781dd6da6e40c4137bfbead4d1205`:

- application image: 1,025,808 bytes (`0xFA710`);
- factory partition: 1,769,472 bytes (`0x1B0000`);
- remaining headroom: 743,664 bytes (`0xB58F0`), 42% free;
- no hardware flashed.

Production source changed after that SHA. This run does not validate the current head.

## Remaining

- [ ] Pass HFP I2S output host tests at the final head.
- [ ] Pass a fresh ESP-IDF v5.5.1 compile-only workflow at the final head.
- [ ] Record current image size and partition headroom.
- [ ] Measure real heap, stack, callback timing, ring, underflow, timeout, and overlap behavior on hardware.

---

# RF-06 — Reconcile the primary full-duplex TODO [P1]

- [x] Distinguish implemented software from prior phase CI, current-head CI, compile-only evidence, hardware pending, and future work.
- [x] Mark FD-08 through FD-13 and FD-16 software status accurately.
- [x] Keep FD-14, FD-15, FD-17, FD-20, and FD-21 hardware work incomplete.
- [x] Keep FD-18 and FD-19 unimplemented.
- [x] Keep phase-wide failure/recovery/resource/soak acceptance open.
- [x] Reference only files that exist at the exact repository paths.
- [ ] Update the primary TODO again after the final code head and workflow run IDs are known.

---

# RF-07 — Final regression and static sweeps [P0]

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

- [ ] ESP-IDF v5.5.1 compile-only workflow passes at the final production/documentation head.
- [ ] Record exact workflow run ID and final SHA.
- [ ] Record image size, partition size, and remaining headroom.
- [ ] Confirm the workflow performs no flash.

## Static sweep results

- [x] Callback allocation/blocking/direct-I2S/per-frame-log sweep passed.
- [x] `volatile` synchronization sweep passed for modified production files.
- [x] Live-task deletion shortcut sweep passed for modified production files.
- [x] Generated build/log artifact sweep passed.
- [x] Status-unavailable fake-success and transport-error sweep passed.
- [x] Same-peer stale A2DP event token was upgraded to the real ESP-IDF connection handle.
- [x] Health-report diagnostics no longer depend on the mutex whose failure they diagnose.
- [x] Silent/uncounted I2S zero-fill was removed from the writer path.
- [ ] Resolve the A2DP `bt_ctx_lock()` partial-state issue under RF-02.
- [ ] Resolve or explicitly disposition the HFP callback-registration bookkeeping issue below.

## Remaining P1 registration bookkeeping issue

`bt_hfp_audio_register_callback()` can receive successful lower-layer registration and then fail to reacquire its application mutex. It returns failure and the callback remains fail-closed for audio acceptance, so this is not silent PCM acceptance. However, it returns generic `ESP_FAIL` and application bookkeeping can disagree with the lower layer until profile teardown.

- [ ] Make callback registration bookkeeping authoritative after lower-layer success, or quarantine/fault it with exact failure visibility.
- [ ] Add deterministic host coverage for successful lower registration followed by bookkeeping failure.

---

# RF-08 — Closeout and handoff [P1 blocked]

Do not create:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md`

until RF-02, RF-07, fresh host CI, and fresh compile-only CI pass.

The eventual closeout must include:

- [ ] Starting baseline SHA.
- [ ] Final validated code/documentation SHA.
- [ ] Exact final changed-file list.
- [ ] RF-01 through RF-06 summaries.
- [ ] Exact host workflow result and test counts.
- [ ] Exact compile-only workflow result and firmware metrics.
- [ ] Explicit no-flash statement.
- [ ] Pending FD-14, FD-15, FD-17, resource, and soak gates.
- [ ] Explicit FD-18/FD-19 not-implemented statement.
- [ ] ESP-IDF A2DP `conn_hdl` identity contract.
- [ ] Updated primary TODO status.
- [ ] Confirmation every referenced assistant-created file exists.

---

# Current definition-of-done status

- [x] Callback overlap is nonblocking, fail-closed, visible, and covered by focused tests.
- [x] Same-peer reconnect events use the ESP-IDF event-owned connection handle.
- [x] Health-report failure diagnostics are lock-independent and visible.
- [x] Status-unavailable results cannot masquerade as success, including transport failure.
- [x] Callback stack-array bounds are explicit and statically enforced.
- [x] I2S writer zero-fill and write-loss accounting is authoritative in the normal lock-acquired path.
- [ ] A2DP base-state lock failure cannot leave partial lifecycle state.
- [ ] HFP callback-registration bookkeeping failure has an authoritative disposition.
- [ ] Final host CI passes.
- [ ] Final ESP-IDF v5.5.1 compile-only CI passes.
- [x] Hardware was not flashed.
- [ ] Final closeout exists.
