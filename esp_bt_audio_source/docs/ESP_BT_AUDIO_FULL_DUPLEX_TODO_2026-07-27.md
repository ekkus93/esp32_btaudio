# ESP32 Bluetooth Audio Full-Duplex TODO

**Companion specification:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`  
**Implementation branch:** `feature/esp-bt-audio-duplex`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Primary project:** `esp_bt_audio_source/`  
**Status reconciled:** 2026-07-28  
**Reviewed review-fix production/test head before final documentation commits:** `392894daa1b930817639da58c4ba2d590f4b8013`  
**Last verified compile-only implementation head:** `e48341ae665781dd6da6e40c4137bfbead4d1205`

## Status rules

- `[x] Software implemented` means the source change exists and was inspected on the feature branch.
- `[x] Focused coverage present` means a deterministic host test exists and is wired into a maintained runner.
- Older phase host/compile results validate only their recorded older SHAs.
- Production source changed after `e48341ae...`; fresh host CI and fresh ESP-IDF compile-only CI are required for the final documentation head.
- `[ ] Hardware pending` means no physical-board evidence is claimed.
- `[ ] Future` means the production capability is not implemented.
- Do not treat a compile as runtime proof.
- Do not create the final review-fix closeout until current-head host CI and compile-only CI pass.

## Authoritative maintained evidence

All referenced assistant-created files below exist at the exact repository paths:

- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_PROGRESS_2026-07-27.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD09_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD10_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD11_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD12_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD13_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD16_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_TODO_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`

Do not reference any generated review, response, template, or closeout file unless it is committed at the exact named path.

---

# Non-negotiable rules

- [x] Work directly on `feature/esp-bt-audio-duplex`.
- [x] Do not create a helper branch or PR without explicit instruction.
- [x] Keep `main/main.c` as a clean bootstrap.
- [x] Keep Bluetooth profile, HFP, SCO, state, and policy under `components/bt_manager`.
- [x] Keep I2S0 TX and voice conversion under `components/audio_processor`.
- [x] Route command behavior through public manager APIs.
- [x] Reject quiet fallback, silent data loss, fabricated status, fake success, and unbounded retry.
- [x] Keep the incoming HFP callback allocation-free, nonblocking, direct-I2S-free, and per-frame-log-free.
- [x] Make rejected data and failed diagnostics visible through exact errors, stable records, counters, or fault/quarantine state.
- [x] Do not claim HFP downlink playback before FD-18 and FD-19.
- [x] Do not flash hardware without explicit user approval.
- [x] No hardware was flashed during the software or review-fix work recorded here.

---

# Current phase matrix

| Phase | Current classification | Remaining gate |
|---|---|---|
| FD-00 through FD-07 | Software complete; older phase validation recorded | Real profile/SLC hardware behavior |
| FD-08 | Software and review accounting hardening complete | Fresh CI/compile and physical I2S0 validation |
| FD-09 | Software, overlap hardening, and registration publication complete | Fresh CI/compile and callback measurements |
| FD-10 | Software complete at prior phase baseline | Fresh CI and real SCO timing/start-stop behavior |
| FD-11 | Software plus status-unavailable transport hardening complete | Fresh command CI |
| FD-12 | Software complete at prior phase baseline | Physical UART validation |
| FD-13 | Software plus lock-independent health diagnostics complete | Fresh CI and target resource measurements |
| FD-14 | Hardware pending | I2S0 GPIO/wire-format acceptance |
| FD-15 | Hardware pending | Real HFP SLC/SCO with earbuds |
| FD-16 | Software plus `conn_hdl` identity and atomic base-state commit complete | Fresh CI/compile |
| FD-17 | Hardware pending | Simultaneous A2DP playback and HFP microphone |
| FD-18 | Future; not implemented | Playback voice tap |
| FD-19 | Future; not implemented | HFP outgoing PCM send path |
| FD-20 | Blocked by FD-18/FD-19 and hardware | Operational HFP full-duplex CVSD |
| FD-21 | Future and hardware pending | WBS/mSBC support |
| FD-22 through FD-24 | Partial | Complete failure/recovery/race matrix |
| FD-25 | In progress | Current-head host and compile-only workflows |
| FD-26 | Hardware pending | Runtime resource matrix |
| FD-27 | Hardware pending | Functional matrix and soak |
| FD-28 | Partial | Final user/runtime documentation after hardware results |
| FD-29 | Software review complete; validation pending | CI, compile, then review-fix closeout |

---

# FD-00 through FD-07 — Foundation and HFP SLC

## Software implemented

- [x] Branch, layering, configuration, and single-sync-connection controls.
- [x] HFP AG/HCI profile lifecycle with bounded waits and rollback.
- [x] Authoritative peer/generation-bound duplex state.
- [x] Generation-aware bounded SPSC PCM ring.
- [x] CVSD conversion and stereo-to-mono helpers.
- [x] Generation-bound HFP SLC connect/disconnect operations.
- [x] Exact accepted-versus-completed command semantics.
- [x] Unsafe teardown refusal and quarantine behavior.

## Hardware pending

- [ ] Confirm HFP AG profile events on a real target.
- [ ] Confirm SLC connect/disconnect, reject, timeout, reconnect, and earbud power-cycle behavior.
- [ ] Capture baseline heap, largest block, and task stack measurements.

---

# FD-08 — I2S0 microphone output

## Software implemented

- [x] Validate port, pins, collisions, and unsupported configurations; never silently substitute defaults.
- [x] Configure GPIO32 BCLK, GPIO33 WS/LRCLK, and GPIO27 DOUT for 16 kHz signed 16-bit mono Philips I2S.
- [x] Allocate bounded ring/writer resources before runtime.
- [x] Use one cooperatively stopped writer task.
- [x] Roll back partial initialization in reverse order.
- [x] Quarantine incomplete stop/cleanup rather than deleting a live task.
- [x] Count ring overflow/underflow, inserted silence, short writes, lost bytes, timeout, and quarantine.
- [x] Keep ring consumption, zero-fill, bounded I2S write, and accounting in one writer critical section.
- [x] Do not consume PCM or manufacture uncounted silence when the accounting lock cannot be acquired.
- [x] Require a finite nonzero I2S write timeout; current default is 20 ms.

## Validation pending

- [ ] Pass final HFP I2S output host tests.
- [ ] Pass current-head ESP-IDF compile-only build.
- [ ] Verify clocks and PCM electrically on GPIO32/33/27.
- [ ] Verify receiver mono-slot interpretation and microphone intelligibility.
- [ ] Verify I2S0 does not disturb I2S1, UART2, or A2DP.
- [ ] Measure writer stack, heap, ring, underflow, timeout, and stop behavior.

---

# FD-09 — HFP incoming HCI callback and CVSD routing

## Software implemented

- [x] Register the ESP-IDF incoming HCI callback only after AG profile readiness.
- [x] Publish callback registration through an atomic `UNREGISTERED` → `REGISTERING` → `REGISTERED` state machine.
- [x] Eliminate the former lower-success/post-registration-mutex-reacquire bookkeeping failure.
- [x] Reject reentrant or concurrent registration while publication is pending; never call the lower API twice.
- [x] On lower registration failure, record the exact error, increment failure count, and return to retryable `UNREGISTERED` state.
- [x] Bind accepted PCM to peer, generation, synchronous handle, codec, SLC, and running I2S state.
- [x] Reject null, zero, odd, oversize, inactive, stale, bad-frame, capacity, ring, and unsupported-codec input visibly.
- [x] Use alignment-safe bounded copy and fixed-size conversion arrays.
- [x] Push one whole converted frame to the generation-bound ring.
- [x] Release each non-null ESP-IDF-owned buffer exactly once.
- [x] Reject mSBC visibly until FD-21.
- [x] Use a nonblocking overlap gate and saturating overlap counter.
- [x] Keep ordinary callback counters single-writer.
- [x] Expose overlap through `HFP STATS`.
- [x] Enforce 720 bytes of fixed callback audio arrays under a 1024-byte compile-time ceiling.
- [x] Maintain process-lifetime callback maximum and over-budget diagnostics.

## Focused coverage present

- [x] Registration success, idempotence, lower failure, exact error, and failure count.
- [x] Reentrant registration is rejected until successful publication completes.
- [x] Callback overlap rejection and exact accounting.
- [x] Invalid/stale/bad/unsupported/ring-rejected frame paths.
- [x] Callback timing and lifetime metrics.

## Validation pending

- [ ] Pass incoming-audio sanitizer and allocation-symbol gates at the final head.
- [ ] Pass ESP-IDF v5.5.1 compile-only validation for atomics and callback API.
- [ ] Measure real callback cadence, p99/max duration, overlap, and stack margin.

---

# FD-10 — HFP audio start/stop

## Software implemented

- [x] Require enabled mode, same-peer SLC, and clean transient state.
- [x] Rotate the audio generation.
- [x] Start I2S before requesting SCO/eSCO.
- [x] Require the matching completion event before claiming startup complete.
- [x] Close callback acceptance before stopping the old generation.
- [x] Use bounded disconnect/cleanup waits.
- [x] Fault or quarantine incomplete cleanup rather than returning fake success.
- [x] Preserve the primary failure while making secondary health-report failures visible.

## Validation pending

- [ ] Pass final audio-control sanitizer tests.
- [ ] Validate real SCO request/event timing and repeated start/stop behavior.

---

# FD-11 — HFP commands

## Software implemented

- [x] `STATUS`, `CONNECT`, `DISCONNECT`, `AUDIO START`, `AUDIO STOP`, `MODE`, `CODEC`, `STATS`, and `RESETSTATS`.
- [x] Exact mode parsing for `DISABLED`, `A2DP_MIC`, `HFP_FULL`, and `AUTO`.
- [x] Consistent status snapshots and baseline-relative statistics reset.
- [x] Exact backend errors and explicit accepted-versus-completed semantics.
- [x] When lower audio start/stop succeeds but status retrieval fails, emit `AUDIO_STATUS_UNAVAILABLE`, `LOWER_OPERATION=SUCCEEDED`, and the exact status error.
- [x] Do not fabricate status fields.
- [x] Propagate command/UART transport failure from the status-unavailable response.

## Validation pending

- [ ] Pass the final HFP command suite, including forced UART write failure.

---

# FD-12 — Event contract

## Software implemented

- [x] Stable PROFILE, AUDIO, MODE, I2S, and HEALTH records.
- [x] Transition/threshold emission instead of per-frame flooding.
- [x] Sanitized fields, stable reasons, generation identity, and duplicate suppression.
- [x] Attempt all configured command UARTs and expose partial delivery failure.
- [x] Avoid recursive health-event failure loops.

## Hardware pending

- [ ] Validate physical UART0/UART2 delivery and concurrent ordering.

---

# FD-13 — Diagnostics and failure visibility

## Software implemented

- [x] Current free internal heap, lifetime minimum heap, and largest internal block.
- [x] HFP app-task and I2S writer minimum-free-stack observations.
- [x] Callback budget, last duration, lifetime maximum, over-budget count, and overlap rejection.
- [x] Explicit `AVAILABLE`, `UNAVAILABLE`, and `NA`; never fake zero.
- [x] Process-lifetime health-report failure count and exact last error use lock-independent atomics.
- [x] Record pre-lock validation, lock, identity/transition, injected, and unlock failures.
- [x] Preserve primary errors and avoid recursive health reporting.
- [x] Expose bounded diagnostics through `HFP STATS`.

## Validation pending

- [ ] Pass final duplex-state, audio-control, diagnostics, and command tests.
- [ ] Record real heap, stack, callback, ring, and health metrics on hardware.

---

# FD-14 and FD-15 — CVSD hardware bring-up

- [ ] Obtain explicit approval before flashing.
- [ ] Connect common ground plus GPIO32 BCLK, GPIO33 WS/LRCLK, and GPIO27 DOUT.
- [ ] Verify 16 kHz signed 16-bit mono Philips timing and captured PCM.
- [ ] Verify CVSD 8-to-16 kHz duplication in captured samples.
- [ ] Test at least 20 start/stop cycles.
- [ ] Test receiver absent/stalled behavior.
- [ ] Pair target earbuds and establish same-peer A2DP plus HFP SLC.
- [ ] Start/stop CVSD SCO and verify incoming microphone PCM.
- [ ] Test power cycle, remote disconnect, wrong peer, and reconnect.
- [ ] Capture binary, heap, stack, callback, ring, I2S, and A2DP measurements.

---

# FD-16 — Duplex policy and A2DP lifecycle identity

## Software implemented

- [x] `DISABLED` preserves ordinary A2DP behavior.
- [x] Strict `A2DP_MIC` reports incompatibility rather than silently changing mode.
- [x] `HFP_FULL` reserves HFP ownership without claiming unimplemented playback.
- [x] `AUTO` may select compatibility-required `HFP_FULL` and emits the transition.
- [x] Serialize mode transitions and assign exactly one downlink owner.
- [x] Treat A2DP suspend/stop during SCO as explicit policy input.
- [x] Use the ESP-IDF A2DP `conn_hdl` from each connection/audio callback as the event-owned connection identity.
- [x] Bind peer, `conn_hdl`, application lifecycle serial, and duplex generation.
- [x] Reject missing binding, wrong peer, and stale same-peer old-handle events.
- [x] Permit legitimate HFP generation rotation only inside the matching A2DP connection.
- [x] Prevent old-handle audio from marking a newer same-peer connection as playing.
- [x] Prevent old-handle disconnect from disconnecting the newer connection or invoking its callback.
- [x] Validate/create binding, mutate `bt_ctx`, and capture callbacks under one lock acquisition.
- [x] Invoke callbacks, forwarding, autostart, and policy only after successful atomic commit and unlock.
- [x] A `bt_ctx_lock()` failure occurs before any binding, state mutation, callback, forwarding, autostart, or policy side effect.

## Focused coverage present

- [x] Missing binding, wrong peer, stale old handle, reconnect rotation, and valid generation refresh.
- [x] Forced unavailable `bt_ctx` mutex proves no partial state or downstream side effect.

## Capability boundary

- [x] `HFP_FULL` remains `COMPATIBILITY_REQUIRED` with `HFP_DOWNLINK_NOT_IMPLEMENTED` until FD-18/FD-19.
- [ ] Do not mark operational HFP full-duplex playback complete.

## Validation pending

- [ ] Pass final Bluetooth/A2DP integration and FD-16 policy suites.
- [ ] Pass current-head ESP-IDF compile-only validation.

---

# FD-17 — Simultaneous A2DP playback and HFP microphone hardware gate

- [ ] Start known continuous A2DP playback.
- [ ] Start HFP microphone audio.
- [ ] Record whether each target earbud keeps, suspends, or stops A2DP.
- [ ] Record exact A2DP/HFP events and policy output.
- [ ] Verify microphone PCM continues through I2S0.
- [ ] Verify I2S1 and existing playback counters remain stable.
- [ ] Confirm strict-mode success or explicit incompatibility, never silent fallback.

---

# FD-18 and FD-19 — HFP compatibility downlink [future]

## FD-18 playback voice tap

- [ ] Tap canonical playback PCM without adding a second source consumer.
- [ ] Downmix and resample with explicit bounded phase state.
- [ ] Write to a bounded generation-bound downlink ring.
- [ ] Disable work when HFP downlink is inactive.
- [ ] Reset phase/ring on session or codec change.
- [ ] Preserve source priority and A2DP timing.
- [ ] Count overflow and reject hidden loss.

## FD-19 outgoing PCM send path

- [ ] Verify the installed ESP-IDF v5.5.1 outgoing API contract before implementation.
- [ ] Read only from the bounded downlink ring.
- [ ] Perform no callback-path allocation, blocking wait, or resampling.
- [ ] Bind outgoing data to peer, generation, codec, and active audio state.
- [ ] Count required silence and threshold repeated underrun visibly.
- [ ] Add full/partial/empty/inactive/codec-transition/timing tests.

---

# FD-20 and FD-21 — Operational HFP full duplex and mSBC [future/hardware]

- [ ] Implement FD-18 and FD-19 before FD-20.
- [ ] Validate simultaneous earbud speaker downlink and microphone-to-I2S0 CVSD.
- [ ] Verify no false A2DP-streaming status.
- [ ] Test AUTO transitions and all playback sources.
- [ ] Enable WBS/mSBC only after CVSD acceptance.
- [ ] Measure flash/DRAM growth.
- [ ] Handle negotiation, fallback, codec changes, and stale old-codec data explicitly.
- [ ] Add transition/fallback/flush/strict-mode tests and hardware validation.

---

# FD-22 through FD-24 — Failure, teardown, and race hardening

- [ ] Reconcile all thresholds in one maintained table.
- [ ] Verify isolated/repeated/sustained underflow and overflow boundaries.
- [ ] Complete failure injection for callback/profile/SLC/ring/I2S/task/SCO/deinit stages.
- [ ] Prove exact errors, no leaked allocation/channel/task, and no false running state.
- [ ] Complete A2DP-first/HFP-first disconnect, ACL loss, SCO race, codec-after-disconnect, reconnect-before-stale-event, stop/mode-change, and duplicate-event matrices.
- [ ] Prove stale events cannot resurrect stopped sessions.
- [ ] Prove counters remain monotonic except explicit baseline reset.

---

# FD-25 — Host and device compile validation [P0]

## Historical evidence only

- [x] Older phase host CI passed through the FD-16 phase baseline.
- [x] ESP-IDF v5.5.1 compile-only run `30407430615` passed for `e48341ae665781dd6da6e40c4137bfbead4d1205`.
- [x] Historical image: 1,025,808 bytes (`0xFA710`).
- [x] Historical partition headroom: 743,664 bytes (`0xB58F0`), 42% free.
- [x] No hardware was flashed.

## Required for the final documentation head

- [ ] Pass HFP incoming-audio sanitizer tests and allocation gate.
- [ ] Pass HFP audio-control sanitizer tests.
- [ ] Pass duplex-state and health-report diagnostics tests.
- [ ] Pass HFP command tests including forced transport failure.
- [ ] Pass Bluetooth/A2DP stale-handle and lock-failure integration tests.
- [ ] Pass HFP I2S writer/accounting tests.
- [ ] Pass FD-16 policy/runtime tests.
- [ ] Pass changed-Python lint and Python unit tests.
- [ ] Pass full CTest.
- [ ] Record exact host workflow run ID, final SHA, conclusion, and test counts.
- [ ] Pass fresh ESP-IDF v5.5.1 compile-only workflow.
- [ ] Record exact device-build run ID, final SHA, image size, partition size, and headroom.
- [ ] Confirm no flash.

---

# FD-26 and FD-27 — Runtime resources and final hardware acceptance

- [x] Historical compile-only partition headroom exceeded 256 KiB.
- [ ] Confirm current-head partition headroom exceeds 256 KiB.
- [ ] Record heap and stack checkpoints at boot, A2DP, HFP SLC, CVSD SCO, simultaneous mode, future HFP downlink, mSBC, and post-stop.
- [ ] Minimum internal heap at least 32 KiB or explicit reviewed exception.
- [ ] Largest internal block at least 16 KiB or explicit reviewed exception.
- [ ] Every affected task has measured stack margin.
- [ ] Callback p99 below 500 microseconds and maximum below 2 milliseconds, or explicit reviewed exceptions.
- [ ] No persistent heap loss over 100 start/stop cycles.
- [ ] Thirty-minute simultaneous playback/microphone soak.
- [ ] Zero sustained overflow, underflow, timeout, invalid-frame, watchdog, panic, brownout, or reboot failures.

---

# FD-28 — Maintained documentation

- [ ] Update README and architecture after hardware behavior is known.
- [ ] Update command/event protocol documentation.
- [ ] Add final wiring and receiver checklist.
- [ ] Document CVSD/mSBC behavior and earbud compatibility.
- [ ] Document counters, failure interpretation, and recovery.
- [ ] Add final runtime resource measurements.
- [x] State explicitly that microphone audio does not start automatically at boot. See `README.md` (HFP full-duplex microphone audio note) and `ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md` §8. Behavior was already correct (single call site: the `HFP AUDIO START` command handler); this closes the documentation gap only.
- [ ] Update `memory.md` with verified final results.

---

# FD-29 — Final review and handoff

## Review-fix software complete

- [x] Callback overlap is fail-closed and visible.
- [x] Same-peer stale A2DP events use the ESP-IDF event-owned connection handle.
- [x] A2DP identity, binding, and base-state mutation commit atomically or not at all.
- [x] Health-report failures are visible without relying on the state mutex.
- [x] Status-unavailable command results and transport failures cannot look like ordinary success.
- [x] Callback stack-array bounds are statically enforced and documented.
- [x] I2S writer zero-fill and write loss are accounted inside one bounded critical section.
- [x] HFP callback registration success is published atomically with no vulnerable post-registration relock.
- [x] Focused tests cover both final software failure boundaries.
- [x] Final post-production static/manual sweeps completed.
- [x] No generated build/log artifacts are in the reviewed changed-file list.
- [x] No hardware was flashed.

## Closeout blockers

- [ ] Pass final host CI at the final documentation head.
- [ ] Pass final ESP-IDF compile-only CI at the same head.
- [ ] Record exact workflow IDs, final SHA, test counts, image metrics, and headroom.
- [ ] Create `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md` only after both workflows pass.
- [ ] Record final changed files, no-flash status, limitations, and pending hardware.

---

# Full project definition of done

- [ ] One real HFP AG SLC and SCO/eSCO connection operates safely.
- [ ] CVSD microphone PCM reaches I2S0 at the fixed wire format.
- [ ] GPIO32/33/27 are physically verified.
- [ ] A2DP plus HFP microphone behavior is known for every target earbud.
- [ ] FD-18 and FD-19 provide the real HFP compatibility downlink.
- [ ] Operational HFP full-duplex compatibility mode works when A2DP is suspended.
- [ ] mSBC is implemented and validated if retained in release scope.
- [x] Implemented mode changes, incompatibilities, unavailable metrics, rejected frames, and supported fallbacks are explicit.
- [x] The HFP incoming callback is designed not to block, allocate, call direct I2S, or log per frame.
- [ ] Current-head full host CI passes.
- [ ] Current-head ESP-IDF compile-only CI passes with more than 256 KiB headroom.
- [ ] Runtime heap, stack, callback, and soak gates pass or receive explicit reviewed exceptions.
- [ ] Final thirty-minute hardware soak passes.
- [ ] Maintained documentation and `memory.md` are current.
- [x] Hardware was not flashed without explicit approval.
