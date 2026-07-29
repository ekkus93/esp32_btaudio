# ESP32 Bluetooth Audio Full-Duplex TODO

**Companion specification:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`  
**Implementation branch:** `feature/esp-bt-audio-duplex`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Primary project:** `esp_bt_audio_source/`  
**Status reconciled:** 2026-07-28  
**Review-fix production head before final documentation commits:** `e98153195b40212d1e7b96ff48bb27950bb8b264`  
**Last verified compile-only implementation head:** `e48341ae665781dd6da6e40c4137bfbead4d1205`

## Status rules

- `[x] Software implemented` means the source change exists on the feature branch.
- `[x] Prior phase host validated` means the cited phase closeout records a passing host run for that older phase baseline.
- `[x] Compile validated` applies only to the exact SHA recorded with the result.
- Production code changed after `e48341ae...`; the current branch therefore requires fresh host CI and fresh ESP-IDF compile-only CI.
- `[ ] Hardware pending` means no physical-board result is claimed.
- `[ ] Future` means the production capability is not implemented.
- Do not mark the project or review-fix closeout complete while P0 software blockers, CI, compile, or hardware gates remain.

## Authoritative maintained evidence

All referenced files below exist at the exact repository paths:

- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_PROGRESS_2026-07-27.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD09_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD10_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD11_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD12_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD13_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD16_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_TODO_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`

Do not reference an assistant-created report, response file, template, or closeout unless it is committed at the exact named path.

---

# Non-negotiable rules

- [x] Work directly on `feature/esp-bt-audio-duplex`.
- [x] Do not create a helper branch or new PR without explicit instruction.
- [x] Keep `main/main.c` as a clean bootstrap.
- [x] Keep Bluetooth profile, HFP, SCO, duplex state, and policy under `components/bt_manager`.
- [x] Keep I2S0 TX and voice conversion under `components/audio_processor`.
- [x] Route command operations through public manager APIs.
- [x] Reject quiet fallback, silent data loss, fabricated status, fake success, and unbounded retry.
- [x] Keep the HFP incoming callback allocation-free, nonblocking, direct-I2S-free, and per-frame-log-free.
- [x] Make rejected data and failed diagnostics visible through exact errors, stable records, counters, or fault/quarantine state.
- [x] Do not claim HFP downlink playback before FD-18 and FD-19.
- [x] Do not flash hardware without explicit user approval in the current conversation.
- [x] No hardware was flashed during the software or review-fix work recorded here.

---

# Current phase matrix

| Phase | Current classification | Remaining gate |
|---|---|---|
| FD-00 through FD-07 | Software complete; prior phase host/compile evidence recorded | Real profile/SLC behavior remains hardware-gated |
| FD-08 | Software complete; prior phase host/compile evidence recorded | Physical I2S0 output and resource validation |
| FD-09 | Software plus callback review fixes implemented | Fresh host CI, fresh compile, hardware callback measurements |
| FD-10 | Software complete at prior phase baseline | Real SCO timing/start-stop behavior |
| FD-11 | Software plus status-unavailable transport fix implemented | Fresh command tests/CI |
| FD-12 | Software complete at prior phase baseline | Physical UART validation |
| FD-13 | Software plus lock-independent health diagnostics implemented | Fresh CI and target runtime measurements |
| FD-14 | Hardware pending | GPIO32/33/27 I2S0 wire-format acceptance |
| FD-15 | Hardware pending | Real HFP SLC/SCO with earbuds |
| FD-16 | Software plus `conn_hdl` stale-event hardening implemented | A2DP lock-failure blocker, fresh CI/compile |
| FD-17 | Hardware pending | Simultaneous A2DP playback and HFP microphone |
| FD-18 | Future; not implemented | Playback voice tap |
| FD-19 | Future; not implemented | HFP outgoing PCM send path |
| FD-20 | Blocked by FD-18/FD-19 and hardware | HFP full-duplex CVSD |
| FD-21 | Future and hardware pending | WBS/mSBC support |
| FD-22 through FD-24 | Partial | Complete failure/recovery/race matrix |
| FD-25 | Partial | Current-head host and device compile workflows |
| FD-26 | Hardware pending | Runtime resource matrix |
| FD-27 | Hardware pending | Functional matrix and soak |
| FD-28 | Partial | Final maintained documentation after hardware results |
| FD-29 | In progress | Resolve software blockers, CI/compile, then closeout |

---

# FD-00 through FD-07 — Foundation and HFP SLC

## Implemented

- [x] Branch/scope and layering controls.
- [x] HFP AG/HCI configuration with HFP client disabled and one synchronous connection.
- [x] Authoritative synchronized duplex state with peer and generation validation.
- [x] Bounded generation-aware SPSC PCM ring.
- [x] CVSD 8 kHz to 16 kHz conversion and stereo-to-mono helpers.
- [x] HFP AG lifecycle with rollback and unsafe-teardown refusal.
- [x] Generation-bound HFP SLC connect/disconnect operations with bounded request/watchdog waits.
- [x] Exact distinction between accepted asynchronous requests and completion events.
- [x] Prior focused host sanitizer and ESP-IDF compile-only validation recorded in maintained phase evidence.

## Still pending

- [ ] Capture current target heap, largest-block, stack, and A2DP baseline measurements.
- [ ] Confirm real HFP AG profile and SLC events on hardware.
- [ ] Confirm remote reject, timeout, reconnect, and earbud power-cycle behavior.

---

# FD-08 — I2S0 microphone output

## Implemented

- [x] Validate I2S port and configured pins; never substitute defaults for invalid pins.
- [x] Reject I2S1, UART, flash, input-only, nonexistent, duplicate, strapping, and known-bad conflicts according to explicit policy.
- [x] Use GPIO32 BCLK, GPIO33 WS/LRCLK, and GPIO27 DOUT for the configured contract.
- [x] Allocate bounded ring/writer storage before runtime.
- [x] Use one cooperatively stopped writer task.
- [x] Roll back channel/mode/task/storage failures in reverse order.
- [x] Quarantine incomplete stop/cleanup rather than deleting a live task.
- [x] Count inserted silence, underflow, overflow, write loss, timeout, and quarantine events.
- [x] Keep ring consumption, zero-fill, bounded I2S write, and accounting in one writer critical section.
- [x] Do not consume PCM or insert uncounted silence if accounting lock acquisition fails.

## Pending

- [ ] Fresh host tests after writer-accounting changes.
- [ ] Fresh ESP-IDF compile-only build.
- [ ] Verify 16 kHz Philips I2S clocks and PCM on GPIO32/33/27.
- [ ] Verify receiver mono-slot interpretation and captured microphone intelligibility.
- [ ] Verify I2S0 does not disturb I2S1, UART2, or A2DP.
- [ ] Measure heap, stack, underflow, overflow, timeout, and stop behavior.

---

# FD-09 — HFP incoming HCI callback and CVSD routing

## Implemented

- [x] Register the current ESP-IDF incoming HCI callback only after profile readiness.
- [x] Bind accepted audio to peer, generation, synchronous handle, codec, SLC, and running I2S state.
- [x] Reject null, zero, odd, oversize, inactive, stale, bad-frame, capacity, ring, and unsupported-codec inputs visibly.
- [x] Use alignment-safe bounded copy and fixed-size conversion arrays.
- [x] Push one whole converted frame to the generation-bound ring.
- [x] Release every non-null ESP-IDF-owned audio buffer exactly once.
- [x] Reject mSBC visibly until FD-21.
- [x] Add a nonblocking overlap gate and saturating `callback_overlap_rejections`.
- [x] Keep ordinary callback counters single-writer.
- [x] Expose overlap through `HFP STATS`.
- [x] Enforce a 720-byte combined fixed audio-array budget under a 1024-byte compile-time ceiling.
- [x] Maintain process-lifetime callback maximum and over-budget diagnostics.

## Pending

- [ ] Pass the incoming-audio sanitizer suite and allocation-symbol gate at the final head.
- [ ] Resolve/disposition callback-registration bookkeeping after successful lower registration followed by application-lock failure.
- [ ] Add deterministic coverage for that registration bookkeeping failure.
- [ ] Measure real callback cadence, p99/max duration, overlap, and stack margins.

---

# FD-10 — HFP audio start/stop

## Implemented

- [x] Require enabled mode, same-peer SLC, and clean transient state.
- [x] Rotate audio generation.
- [x] Start I2S before requesting SCO/eSCO.
- [x] Require matching completion event before claiming startup complete.
- [x] Close callback acceptance before stopping the old generation.
- [x] Use bounded disconnect/cleanup waits.
- [x] Fault/quarantine incomplete cleanup rather than returning fake success.
- [x] Preserve exact primary failure while making secondary health-report failures visible.

## Pending

- [ ] Fresh audio-control sanitizer tests.
- [ ] Real SCO request/event timing, synchronous-handle, callback cadence, and repeated start/stop validation.

---

# FD-11 — HFP commands

## Implemented

- [x] `HFP STATUS`, `CONNECT`, `DISCONNECT`, `AUDIO START`, `AUDIO STOP`, `MODE`, `CODEC`, `STATS`, and `RESETSTATS`.
- [x] Exact mode parsing for `DISABLED`, `A2DP_MIC`, `HFP_FULL`, and `AUTO`.
- [x] Consistent status snapshots and baseline-relative statistics reset.
- [x] Exact backend errors and explicit accepted-versus-completed semantics.
- [x] If lower audio start/stop succeeds but status retrieval fails, emit `AUDIO_STATUS_UNAVAILABLE` with `LOWER_OPERATION=SUCCEEDED` and exact status error.
- [x] Do not fabricate status fields.
- [x] Propagate UART/command-transport failure from the status-unavailable response.

## Pending

- [ ] Pass the final HFP command suite, including forced UART write failure.

---

# FD-12 — Event contract

## Implemented

- [x] Stable PROFILE, AUDIO, MODE, I2S, and HEALTH records.
- [x] Transition/threshold-based emission rather than per-frame flooding.
- [x] Sanitized fields, stable reason tokens, generation identity, and duplicate suppression.
- [x] Attempt every configured command UART and expose partial delivery failure.
- [x] Avoid recursive health-event failure loops.

## Pending

- [ ] Physical UART0/UART2 electrical delivery and concurrent ordering validation.

---

# FD-13 — Diagnostics

## Implemented

- [x] Current free internal heap, process-lifetime minimum heap, and largest internal block.
- [x] HFP app-task and I2S writer minimum-free-stack observations.
- [x] Callback budget, last duration, process-lifetime maximum, and over-budget count.
- [x] Explicit available/unavailable states and `NA`; never fake zero.
- [x] Process-lifetime health-report failure count and last error using lock-independent atomics.
- [x] Record pre-lock validation, lock, operation-validation, illegal-transition, and unlock failures.
- [x] Preserve primary errors and avoid recursive health reporting.
- [x] Expose diagnostics through bounded `HFP STATS` records.

## Pending

- [ ] Pass final duplex-state, audio-control, diagnostics, and command tests.
- [ ] Record real heap, stack, callback, ring, and health-report metrics on hardware.

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

## Implemented policy

- [x] `DISABLED` preserves ordinary A2DP behavior.
- [x] Strict `A2DP_MIC` reports incompatibility instead of silently changing mode.
- [x] `HFP_FULL` reserves HFP ownership without claiming unimplemented playback.
- [x] `AUTO` may select compatibility-required `HFP_FULL` and emits the transition.
- [x] Serialize mode transitions and assign exactly one downlink owner.
- [x] Treat A2DP suspend/stop during SCO as explicit policy input.

## Implemented stale-event hardening

- [x] Use ESP-IDF A2DP `conn_hdl` from connection and audio callback records as the event-owned connection identity.
- [x] Bind peer, `conn_hdl`, lifecycle serial, and duplex generation.
- [x] Validate identity before base-state mutation, user callbacks, autostart, and policy forwarding.
- [x] Reject missing binding, wrong peer, and stale same-peer old-handle events.
- [x] Allow legitimate HFP generation rotation only inside the matching A2DP connection.
- [x] Prevent old-handle audio from marking a newer same-peer connection as playing.
- [x] Prevent old-handle disconnect from disconnecting the newer connection.

## P0 blocker

- [ ] If `bt_ctx_lock()` fails after identity acceptance, fail closed before callbacks/policy and do not leave a new binding usable.
- [ ] Add deterministic failure coverage or equivalent proof for this path.

## Capability boundary

- [x] `HFP_FULL` remains `COMPATIBILITY_REQUIRED` with `HFP_DOWNLINK_NOT_IMPLEMENTED` until FD-18/FD-19.
- [ ] Do not mark operational HFP full-duplex playback complete.

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
- [ ] Downmix safely and resample to the negotiated HFP rate with explicit phase state.
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
- [ ] Count every required silence byte and threshold repeated underrun visibly.
- [ ] Add full/partial/empty/inactive/codec-transition/timing tests.

---

# FD-20 and FD-21 — HFP full duplex and mSBC [future/hardware]

- [ ] Implement FD-18/FD-19 before FD-20.
- [ ] Validate simultaneous earbud speaker downlink and microphone-to-I2S0 CVSD.
- [ ] Verify no false A2DP-streaming status.
- [ ] Test AUTO transitions and every playback source.
- [ ] Enable WBS/mSBC only after CVSD acceptance.
- [ ] Measure flash/DRAM growth.
- [ ] Handle negotiation/fallback and codec changes explicitly.
- [ ] Flush old-generation/old-codec data.
- [ ] Add transition/fallback/flush/strict-mode tests and hardware validation.

---

# FD-22 through FD-24 — Failure, teardown, and race hardening

- [ ] Reconcile all thresholds in one maintained table.
- [ ] Verify isolated/repeated/sustained underflow and overflow boundaries.
- [ ] Complete failure injection for callback/profile/SLC/ring/I2S/task/SCO/deinit stages.
- [ ] Prove exact errors, no leaked allocations/channels/tasks, and no false running state.
- [ ] Complete A2DP-first/HFP-first disconnect, ACL loss, SCO race, codec-after-disconnect, reconnect-before-stale-event, stop/mode-change race, and duplicate-event matrices.
- [ ] Prove stale events cannot resurrect stopped sessions.
- [ ] Prove counters remain monotonic except explicit baseline reset.

---

# FD-25 — Host and device compile validation [P0]

## Historical evidence only

- [x] Prior phase host CI passed through the FD-16 phase baseline.
- [x] ESP-IDF v5.5.1 compile-only run `30407430615` passed for `e48341ae665781dd6da6e40c4137bfbead4d1205`.
- [x] Historical image: 1,025,808 bytes (`0xFA710`).
- [x] Historical partition headroom: 743,664 bytes (`0xB58F0`), 42% free.
- [x] No hardware was flashed.

## Required for current branch head

- [ ] Pass HFP incoming-audio sanitizer tests and allocation gate.
- [ ] Pass HFP audio-control sanitizer tests.
- [ ] Pass duplex-state and health-report diagnostics tests.
- [ ] Pass HFP command tests including transport failure.
- [ ] Pass Bluetooth/A2DP stale-handle integration tests.
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
- [ ] State explicitly that microphone audio does not start automatically at boot.
- [ ] Update `memory.md` with verified final results.

---

# FD-29 — Final review and handoff

## Completed review-fix work

- [x] Callback overlap is fail-closed and visible.
- [x] Same-peer stale A2DP events use the event-owned ESP-IDF connection handle.
- [x] Health-report failures are visible without relying on the state mutex.
- [x] Status-unavailable command results and transport failures cannot look like ordinary success.
- [x] Callback stack-array bounds are statically enforced and documented.
- [x] I2S writer zero-fill/write loss is accounted in the normal lock-acquired path.
- [x] No generated build/log artifacts are in the changed-file list.
- [x] No hardware was flashed.

## Closeout blockers

- [ ] Resolve A2DP base-state lock-failure partial state.
- [ ] Resolve or explicitly disposition HFP callback-registration bookkeeping failure.
- [ ] Pass final host CI.
- [ ] Pass final ESP-IDF compile-only CI.
- [ ] Re-run final static/manual sweeps after the last production change.
- [ ] Create `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md` only after all software/CI/compile blockers pass.
- [ ] Record final SHAs, exact changed files, test results, compile metrics, no-flash status, limitations, and pending hardware.

---

# Full project definition of done

- [ ] One real HFP AG SLC and SCO/eSCO connection operates safely.
- [ ] CVSD microphone PCM reaches I2S0 at the fixed wire format.
- [ ] GPIO32/33/27 are physically verified.
- [ ] A2DP plus HFP microphone behavior is known for every target earbud.
- [ ] FD-18 and FD-19 provide the real HFP compatibility downlink.
- [ ] HFP full-duplex compatibility mode works when A2DP is suspended.
- [ ] mSBC is implemented and validated if retained in release scope.
- [x] Implemented mode changes, incompatibilities, unavailable metrics, rejected frames, and supported fallbacks are explicit.
- [x] The HFP incoming callback is designed not to block, allocate, call I2S, or log per frame.
- [ ] Current-head full host CI passes.
- [ ] Current-head ESP-IDF compile-only CI passes with more than 256 KiB headroom.
- [ ] Runtime heap, stack, callback, and soak gates pass or receive explicit reviewed exceptions.
- [ ] Final thirty-minute hardware soak passes.
- [ ] Maintained documentation and `memory.md` are current.
- [x] Hardware was not flashed without explicit approval.
