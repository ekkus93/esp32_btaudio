# ESP32 Bluetooth Audio Full-Duplex TODO

**Companion specification:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`  
**Implementation branch:** `feature/esp-bt-audio-duplex`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Primary project:** `esp_bt_audio_source/`  
**Status reconciled:** 2026-07-28  
**Rule:** Implement one task at a time. Do not combine unrelated refactors.

## Status legend

- `[x] Software implemented` means the source change exists on the feature branch.
- `[x] Host validated` means the cited phase closeout records a passing host CI run for that phase baseline.
- `[x] Compile validated` means the cited ESP-IDF v5.5.1 compile-only build passed.
- `[ ] Hardware pending` means no physical-board result is claimed.
- `[ ] Future phase` means the production capability is not implemented yet.
- A prior phase CI result does not prove a later commit. The review-fix head must receive its own host CI result before its closeout is final.

## Authoritative maintained evidence

The following files exist in the repository and are the evidence used by this reconciled TODO:

- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_PROGRESS_2026-07-27.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD09_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD10_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD11_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD12_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD13_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD16_CLOSEOUT_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_TODO_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`

Do not reference an assistant-created report or closeout unless it exists at the exact repository path.

## Non-negotiable implementation rules

- [x] Work directly on `feature/esp-bt-audio-duplex`.
- [x] Keep `main/main.c` as a clean bootstrap.
- [x] Keep Bluetooth profile, HFP, SCO, and duplex-state logic under `components/bt_manager`.
- [x] Keep I2S0 TX and voice conversion under `components/audio_processor`.
- [x] Route command operations through public manager APIs rather than direct ESP HFP calls.
- [x] Reject quiet fallback, silent data loss, fake success, fabricated status, and unbounded retries.
- [x] Keep callback paths allocation-free, nonblocking, I2S-driver-free, and free of per-frame logging.
- [x] Preserve visible counters or exact errors for rejected data and failed diagnostics.
- [x] Do not claim HFP downlink playback until FD-18 and FD-19 exist.
- [x] Do not flash hardware without explicit user approval in the current conversation.
- [x] No hardware was flashed during the software phases or the 2026-07-28 review-fix work.

---

# Current phase summary

| Phase | Current classification | Evidence / remaining gate |
|---|---|---|
| FD-00 through FD-07 | Software complete; phase host and compile validation recorded | Progress document; real profile/SLC behavior remains hardware-gated |
| FD-08 | Software complete; phase host and compile validation recorded | Progress document; physical I2S0 validation pending |
| FD-09 | Software complete; phase host and compile validation recorded | FD-09 closeout; latest callback review fixes still need current host CI |
| FD-10 | Software complete; phase host and compile validation recorded | FD-10 closeout; real SCO timing pending |
| FD-11 | Software complete; phase host and compile validation recorded | FD-11 closeout; review fix changed status-unavailable semantics |
| FD-12 | Software complete; phase host and compile validation recorded | FD-12 closeout; physical UART behavior pending |
| FD-13 | Software complete; phase host and compile validation recorded | FD-13 closeout; target runtime measurements pending |
| FD-14 | Hardware pending | I2S0 GPIO/wire-format acceptance |
| FD-15 | Hardware pending | Real HFP SLC/SCO with earbuds |
| FD-16 | Software complete; phase host and compile validation recorded | FD-16 closeout; review fix strengthens A2DP lifecycle binding |
| FD-17 | Hardware pending | Simultaneous A2DP playback and HFP microphone |
| FD-18 | Future; not implemented | Playback voice tap |
| FD-19 | Future; not implemented | HFP outgoing PCM send path |
| FD-20 | Hardware pending and blocked by FD-18/FD-19 | HFP full-duplex CVSD |
| FD-21 | Future and hardware pending | WBS/mSBC support |
| FD-22 through FD-24 | Partially covered by earlier work; phase-wide acceptance remains open | Complete failure/recovery/race matrix |
| FD-25 | Partial | Prior phase CI passed; latest review-fix host CI pending |
| FD-26 | Hardware pending | Runtime resource acceptance matrix |
| FD-27 | Hardware pending | Final functional matrix and soak |
| FD-28 | Partial | Maintained documentation still incomplete |
| FD-29 | In progress | Review-fix closeout blocked on latest host CI |

---

# Phase 0 — Baseline, branch integrity, and resource measurements

## FD-00 — Confirm branch and clean scope [P0]

- [x] Branch is `feature/esp-bt-audio-duplex`.
- [x] Companion spec and TODO exist.
- [x] Main-layering check passed at the recorded baseline.
- [x] Work stayed under `esp_bt_audio_source` and maintained documentation.
- [x] No helper branch or new PR was created for the review-fix set.
- [x] No hardware was flashed.

## FD-01 — Capture the pre-HFP build and runtime baseline [P0]

### Compile baseline

- [x] Build the pre-HFP baseline with ESP-IDF v5.5.1.
- [x] Record application image size.
- [x] Record app-partition size and headroom.
- [x] Record static DRAM sections at the software baseline.

### Hardware/runtime baseline still pending

- [ ] Record free internal heap after boot.
- [ ] Record minimum-ever free heap after normal A2DP streaming.
- [ ] Record largest internal free block.
- [ ] Record Bluetooth and audio task stack high-water marks.
- [ ] Record A2DP underrun/drop counters during a ten-minute baseline run.
- [ ] Complete a before/after runtime resource table on the target board.

## FD-02 — Enable HFP AG/HCI configuration without opening audio [P0]

- [x] Enable HFP Audio Gateway support.
- [x] Disable HFP client role.
- [x] Select the HCI SCO data path.
- [x] Limit synchronous BR/EDR connections to one.
- [x] Keep WBS/mSBC disabled for the initial CVSD path.
- [x] Preserve A2DP and the existing BLE-memory-release policy.
- [x] Confirm compile-only app-partition headroom remains above 256 KiB.
- [x] Keep HFP audio closed at this phase.

---

# Phase 2 — Pure state, ring, and conversion modules

## FD-03 — Authoritative duplex state [P0]

- [x] Typed duplex, A2DP, HFP profile/audio, codec, I2S, and health states.
- [x] One synchronized authoritative snapshot.
- [x] Same-peer and generation enforcement.
- [x] Legal-transition validation.
- [x] Protected 64-bit counters on the 32-bit target.
- [x] No `volatile` synchronization.
- [x] Focused sanitizer suite passed at the phase baseline.
- [x] ESP-IDF compile-only build passed at the phase baseline.

## FD-04 — Bounded SPSC PCM ring [P0]

- [x] Fixed-capacity, nonblocking, whole-frame semantics.
- [x] No overwrite of unread data.
- [x] Generation-bound reset and stale-operation rejection.
- [x] Explicit overflow, underflow, occupancy, peak, and total counters.
- [x] Producer/consumer stress and sanitizer validation passed at the phase baseline.
- [x] ESP-IDF compile-only build passed at the phase baseline.

## FD-05 — Voice PCM conversion helpers [P0]

- [x] CVSD 8 kHz to 16 kHz sample duplication.
- [x] Signed stereo-to-mono conversion with widened arithmetic.
- [x] Stateful 8 kHz and 16 kHz conversion.
- [x] Chunk-boundary continuity and explicit reset.
- [x] Capacity and consumed/produced accounting prevents hidden loss.
- [x] Focused sanitizer validation passed at the phase baseline.
- [x] ESP-IDF compile-only build passed at the phase baseline.

---

# Phase 3 — HFP AG lifecycle and SLC operations

## FD-06 — HFP AG profile module [P0]

- [x] Initialize/deinitialize HFP AG in the correct Bluetooth lifecycle.
- [x] Register and normalize HFP AG events.
- [x] Preserve exact initialization and rollback failures.
- [x] Roll back partial manager initialization in reverse order.
- [x] Refuse unsafe callback-state destruction while Bluedroid may still deliver callbacks.
- [x] Wrong-peer events are rejected and counted.
- [x] Focused lifecycle and manager rollback suites passed at the phase baseline.
- [x] ESP-IDF compile-only build passed at the phase baseline.
- [ ] Confirm real profile events on the target board.

## FD-07 — Generation-bound HFP SLC connect/disconnect APIs [P0]

- [x] Strict MAC parsing and same-peer requirement.
- [x] BtAppTask-owned lower-layer request dispatch.
- [x] Accepted request is distinct from asynchronous completion.
- [x] Operations carry peer, generation, serial, deadline, and exact result.
- [x] Stale/late events are ignored and counted.
- [x] Bounded request/watchdog waits replace unbounded waits.
- [x] Timeout does not fabricate disconnected state.
- [x] Focused sanitizer and compile-only validation passed at the phase baseline.
- [ ] Confirm real SLC connect/disconnect behavior with the target earbuds.

---

# Phase 4 — I2S0 microphone output and CVSD data flow

## FD-08 — Add I2S0 TX output component [P0]

### Software implemented

- [x] Validate I2S port and configured pins at startup.
- [x] Reject conflicts with I2S1, UART2, UART0, flash-connected, input-only, nonexistent, duplicate, strapping, and known-bad pins according to explicit policy.
- [x] Never replace invalid configured pins with defaults.
- [x] Use GPIO32 BCLK, GPIO33 WS/LRCLK, and GPIO27 DOUT for the configured wire contract.
- [x] Allocate bounded ring and writer storage during initialization/start.
- [x] Create one gated writer task.
- [x] Enable I2S only after initialization is complete.
- [x] Roll back channel, mode, task, and storage failures in reverse order.
- [x] Stop the writer cooperatively and wait for explicit exit.
- [x] Quarantine on stop/cleanup timeout rather than deleting a live task.
- [x] Count inserted silence and threshold sustained underflow.
- [x] Fault or quarantine repeated write/disable failures visibly.
- [x] Reject stale generations and partial frames.
- [x] Host sanitizer suite passed at the FD-08 baseline.
- [x] ESP-IDF compile-only build passed at the FD-08 baseline.

### Hardware pending

- [ ] Verify BCLK frequency and duty cycle.
- [ ] Verify WS/LRCLK is 16 kHz with the expected Philips timing.
- [ ] Verify GPIO27 DOUT carries intelligible microphone PCM.
- [ ] Verify the receiver's mono-slot interpretation.
- [ ] Verify I2S0 does not disturb I2S1, UART2, or A2DP.
- [ ] Measure runtime heap, stack, underflow, overflow, and timeout counters.

## FD-09 — Add HFP HCI incoming callback and CVSD routing [P0]

### Software implemented

- [x] Register the current ESP-IDF incoming HCI callback only after HFP profile readiness.
- [x] Bind accepted audio to the authoritative peer, audio generation, synchronous handle, codec, SLC state, and running I2S state.
- [x] Validate null, zero, odd, oversize, capacity, inactive, stale, bad-frame, and unsupported-codec inputs.
- [x] Perform an alignment-safe bounded copy.
- [x] Push exactly one whole CVSD frame to the generation-bound I2S ring.
- [x] Perform no application allocation, blocking wait, direct I2S call, resampling, or per-frame logging in the callback.
- [x] Release each non-null ESP-IDF-owned audio buffer exactly once.
- [x] Expose exact accept/drop/error/timing counters.
- [x] Reject mSBC visibly until FD-21.
- [x] Phase host sanitizer and compile-only validation passed.

### Review-fix additions at the latest implementation head

- [x] Add a nonblocking overlap gate rather than relying on undocumented callback serialization.
- [x] Reject overlap immediately and increment `callback_overlap_rejections`.
- [x] Keep the callback counters single-writer while the gate is held.
- [x] Preserve process-lifetime callback maximum and over-budget counters.
- [x] Expose overlap rejection through `HFP STATS`.
- [x] Add focused overlap/concurrency coverage.
- [x] Add compile-time callback audio-array bounds: 720 bytes currently used under a 1024-byte ceiling.
- [x] Record the maintained callback resource budget document.
- [x] Latest ESP-IDF v5.5.1 compile-only build passed at commit `e48341ae665781dd6da6e40c4137bfbead4d1205`.
- [ ] Verify the complete host workflow at the latest review-fix/documentation head.
- [ ] Measure real callback cadence, duration, overlap, and stack margins on hardware.

## FD-10 — Add explicit HFP audio start/stop [P0]

### Software implemented

- [x] Require an enabled duplex mode, same-peer SLC, and clean transient state.
- [x] Allocate a new audio generation.
- [x] Start I2S before requesting SCO/eSCO.
- [x] Do not report startup complete until the matching connected event arrives.
- [x] Roll I2S back after immediate request failure or bounded timeout handling.
- [x] Close callback acceptance before stopping the old generation.
- [x] Request disconnect and wait a bounded time for completion.
- [x] Stop I2S cooperatively while preserving peer identity and counters.
- [x] Fault/quarantine incomplete cleanup rather than returning fake success.
- [x] Phase host sanitizer and compile-only validation passed.

### Hardware pending

- [ ] Validate real SCO/eSCO request and event timing.
- [ ] Validate synchronous-handle behavior and real callback frame cadence.
- [ ] Validate repeated start/stop and disconnect behavior with target earbuds.

---

# Phase 5 — Commands, events, and diagnostics

## FD-11 — Add HFP command handlers [P1]

- [x] Implement `HFP STATUS`.
- [x] Implement `HFP CONNECT <mac>` and `HFP DISCONNECT`.
- [x] Implement `HFP AUDIO START` and `HFP AUDIO STOP`.
- [x] Implement exact `DISABLED`, `A2DP_MIC`, `HFP_FULL`, and `AUTO` mode selection.
- [x] Implement `HFP CODEC`, `HFP STATS`, and safe `HFP RESETSTATS`.
- [x] Route all Bluetooth work through public manager APIs.
- [x] Keep direct ESP HFP APIs out of the command component.
- [x] Distinguish accepted asynchronous operations from completed operations.
- [x] Preserve exact errors and reject invalid forms/modes.
- [x] Obtain one consistent status snapshot.
- [x] Use non-destructive baseline reset semantics while streaming is stopped.
- [x] Phase host sanitizer and compile-only validation passed.

### Review-fix command semantics

- [x] A successful lower `AUDIO START` or `AUDIO STOP` followed by status-read failure emits `ERR|HFP|AUDIO_STATUS_UNAVAILABLE|...`.
- [x] Include `LOWER_OPERATION=SUCCEEDED` and the exact status error.
- [x] Do not fabricate a status snapshot or plausible zero values.
- [x] Keep ordinary fully observed success output unchanged.
- [x] Add focused START and STOP status-unavailable tests.
- [ ] Verify the complete host workflow at the latest review-fix/documentation head.

## FD-12 — Add HFP event contract [P1]

- [x] Implement stable PROFILE, AUDIO, MODE, I2S, and HEALTH records.
- [x] Emit only meaningful state transitions or health thresholds.
- [x] Avoid per-frame and per-counter event flooding.
- [x] Sanitize/validate fields and use stable reason tokens.
- [x] Include the authoritative session generation.
- [x] Suppress duplicate transitions.
- [x] Attempt every configured command UART and expose partial delivery failure.
- [x] Avoid recursive health-event failure loops.
- [x] Phase host sanitizer and compile-only validation passed.
- [ ] Validate physical UART0/UART2 electrical delivery and concurrent record ordering on hardware.

## FD-13 — Add memory, timing, and stack diagnostics [P1]

### Software implemented

- [x] Capture current free internal heap.
- [x] Capture process-lifetime minimum free heap.
- [x] Capture current largest internal free block.
- [x] Capture process-lifetime minimum-free-stack observations for BtAppTask and the HFP I2S writer.
- [x] Capture callback budget, last duration, process-lifetime maximum, and process-lifetime over-budget count.
- [x] Expose explicit available/unavailable states and `NA`; never fake zero.
- [x] Expose diagnostics through bounded `HFP STATS` records.
- [x] Preserve historical/lifetime metrics across `HFP RESETSTATS`.
- [x] Fail the command before emitting partial stats if an unexpected diagnostic source fails.
- [x] Record firmware size and partition headroom in phase validation notes.
- [x] Phase host sanitizer and compile-only validation passed.

### Review-fix health-report diagnostics

- [x] Make failed `bt_duplex_set_health()` reporting visible.
- [x] Add process-lifetime `health_report_failures` and `last_health_report_error` diagnostics.
- [x] Preserve the primary operation failure and avoid recursive health reporting.
- [x] Include health-report failure information in `HFP STATS`.
- [x] Add failure-injection coverage.
- [ ] Verify the complete host workflow at the latest review-fix/documentation head.

### Hardware measurements pending

- [ ] Record real heap values at every operating checkpoint.
- [ ] Record real task stack high-water marks.
- [ ] Record real callback latency and over-budget count under sustained CVSD traffic.
- [ ] Confirm callback overlap rejections remain zero in normal operation or investigate any nonzero value.

---

# Phase 6 — CVSD hardware bring-up

## FD-14 — Validate GPIO32/33/27 I2S0 output [P0 hardware gate]

- [ ] Obtain explicit user approval before flashing.
- [ ] Connect common ground, GPIO32 BCLK, GPIO33 WS/LRCLK, and GPIO27 DOUT.
- [ ] Configure the receiver for 16 kHz, signed 16-bit mono Philips I2S slave operation.
- [ ] Verify BCLK frequency and duty cycle.
- [ ] Verify WS/LRCLK frequency and phase.
- [ ] Verify DOUT changes with microphone input.
- [ ] Verify no accidental output/reconfiguration occurs on I2S1.
- [ ] Verify the I2S0 clock source does not disturb I2S1 APLL use.
- [ ] Capture and inspect received PCM.
- [ ] Verify CVSD 8-to-16 kHz duplication in captured samples.
- [ ] Test at least 20 start/stop cycles.
- [ ] Test receiver absent and stalled behavior.
- [ ] Confirm normal-operation timeout, overflow, underflow, and cooperative-stop acceptance.

## FD-15 — Validate real HFP SLC and SCO with earbuds [P0 hardware gate]

- [ ] Pair the target earbuds through the existing flow.
- [ ] Establish A2DP and same-peer HFP SLC.
- [ ] Start CVSD HFP audio explicitly.
- [ ] Confirm CVSD negotiation and incoming microphone PCM.
- [ ] Stop HFP audio without disconnecting A2DP.
- [ ] Repeat after earbud power cycle and remote disconnect.
- [ ] Reject events from a different paired device.
- [ ] Capture binary, heap, stack, callback, ring, I2S, and A2DP measurements.

---

# Phase 7 — A2DP plus HFP microphone coexistence

## FD-16 — Add explicit duplex policy engine [P0]

### Software implemented

- [x] `DISABLED` preserves ordinary A2DP behavior.
- [x] `A2DP_MIC` requires the A2DP playback and HFP microphone prerequisites.
- [x] `HFP_FULL` reserves HFP downlink ownership without claiming playback capability.
- [x] `AUTO` begins with the preferred A2DP-plus-HFP-microphone policy.
- [x] Process A2DP suspend/stop during SCO as an explicit policy event.
- [x] Strict `A2DP_MIC` reports incompatibility rather than silently changing mode.
- [x] `AUTO` may select compatibility-required `HFP_FULL` and emits the transition.
- [x] Serialize mode transitions.
- [x] Assign exactly one downlink owner.
- [x] Implement the pure policy state matrix, recovery, ordering, stale-generation, and duplicate-event tests.
- [x] Phase host CI and ESP-IDF compile-only validation passed.

### Review-fix A2DP lifecycle binding

- [x] Add an explicit A2DP connection-lifecycle binding containing peer, lifecycle serial/source serial, and duplex generation.
- [x] Require an existing binding for A2DP audio events.
- [x] Validate lifecycle and peer before refreshing the current duplex generation after legitimate HFP audio-generation rotation.
- [x] Reject missing-binding, wrong-peer, and stale reconnect-boundary events visibly.
- [x] Clear a disconnect binding only after the disconnect event is applied or explicitly rejected.
- [x] Add focused no-binding, valid-rotation, wrong-peer, and stale-serial tests.
- [x] Latest ESP-IDF v5.5.1 compile-only build passed at commit `e48341ae665781dd6da6e40c4137bfbead4d1205`.
- [ ] Verify the complete host workflow at the latest review-fix/documentation head.

### Capability boundary

- [x] `HFP_FULL` remains `COMPATIBILITY_REQUIRED` with an explicit missing-downlink reason until FD-18 and FD-19 are implemented.
- [ ] Do not mark operational HFP full-duplex playback complete.

## FD-17 — Hardware-test simultaneous A2DP playback and CVSD microphone [P0 hardware gate]

- [ ] Start known continuous A2DP playback.
- [ ] Start HFP microphone audio.
- [ ] Record whether each target earbud keeps or suspends A2DP.
- [ ] Record exact A2DP/HFP events and policy output.
- [ ] Verify microphone PCM continues through I2S0.
- [ ] Verify I2S1 and existing playback counters remain stable.
- [ ] Confirm strict-mode success or explicit incompatibility, never silent fallback.

---

# Phase 8 — HFP full-duplex compatibility downlink

## FD-18 — Add playback voice tap [P0 future]

- [ ] Tap canonical playback PCM already produced by the authoritative audio engine.
- [ ] Do not create a second source consumer.
- [ ] Downmix stereo to mono safely.
- [ ] Resample to the negotiated HFP rate with explicit phase state.
- [ ] Write to a bounded generation-bound HFP downlink ring.
- [ ] Disable tap work when HFP downlink is inactive.
- [ ] Reset phase/ring on session or codec change.
- [ ] Preserve existing source priority and A2DP timing.
- [ ] Count ring overflow and reject hidden loss.
- [ ] Add host tests for sample counts, chunk continuity, disabled path, overflow, and source priority.

## FD-19 — Implement HFP outgoing PCM send path [P0 future]

The installed ESP-IDF v5.5.1 API contract must be verified before implementation; do not copy a deprecated pull-callback design blindly.

- [ ] Read only from the bounded downlink ring.
- [ ] Perform no callback-path allocation, blocking wait, or resampling.
- [ ] Follow the installed ESP-IDF send/ready contract exactly.
- [ ] Supply or withhold data according to that contract.
- [ ] Count every supplied silence byte if silence is required.
- [ ] Threshold repeated underruns into visible health state.
- [ ] Bind outgoing data to peer, generation, codec, and active audio state.
- [ ] Add full/partial/empty/inactive/codec-transition/timing tests.

## FD-20 — Hardware-test HFP full-duplex CVSD [P0 hardware gate]

Blocked until FD-18 and FD-19 are implemented and host validated.

- [ ] Confirm earbud speaker audio uses HFP voice downlink.
- [ ] Confirm earbud microphone simultaneously reaches I2S0.
- [ ] Confirm no false A2DP-streaming status.
- [ ] Measure downlink underruns and silence.
- [ ] Verify every existing playback source reaches the tap without changing source priority.
- [ ] Test AUTO transition to/from compatibility mode.

---

# Phase 9 — mSBC wideband speech

## FD-21 — Enable and validate WBS/mSBC [P1 future + hardware]

- [ ] Enable WBS/mSBC only after CVSD paths are accepted.
- [ ] Measure flash and DRAM growth.
- [ ] Handle codec negotiation and fallback explicitly.
- [ ] Route 16 kHz incoming PCM without CVSD duplication.
- [ ] Configure the downlink tap for 16 kHz mono.
- [ ] Flush old-generation/old-codec data on codec switch.
- [ ] Never claim mSBC before confirmed negotiation.
- [ ] Add transition, flush, fallback, strict-mode, and unexpected-codec host tests.
- [ ] Negotiate and validate mSBC on compatible hardware.

---

# Phase 10 — Failure hardening and recovery

## FD-22 — Thresholded health and recovery [P0 open]

Earlier phases implement several threshold, fault, quarantine, and explicit-recovery mechanisms, but the phase-wide matrix is not complete.

- [ ] Reconcile all compile-time/configurable thresholds in one maintained table.
- [ ] Verify isolated, repeated, and sustained underflow/overflow boundaries.
- [ ] Verify I2S timeout, stop timeout, invalid callback, and health-report failure behavior.
- [ ] Verify fault persistence and quarantine rejection.
- [ ] Verify explicit recovery only after cleanup proof.
- [ ] Verify no automatic restart over an unproven worker.

## FD-23 — Partial-start and teardown matrix [P0 open]

- [ ] Complete failure injection for callback/profile/SLC/ring/I2S/task/SCO/deinit stages.
- [ ] For every injection, prove exact error visibility.
- [ ] Prove no leaked allocation, channel, task, or false running state.
- [ ] Prove a later start is safe or explicitly blocked by quarantine.
- [ ] Include health-report failure as a secondary-diagnostic failure without losing the primary cause.

## FD-24 — Event ordering and disconnect races [P0 open]

- [ ] Complete A2DP-first and HFP-first disconnect orderings.
- [ ] Complete ACL-loss and SCO connect/disconnect race coverage.
- [ ] Complete codec-after-disconnect and reconnect-before-stale-event coverage.
- [ ] Complete user stop/mode-change versus remote event races.
- [ ] Prove no stale event resurrects a stopped session.
- [ ] Prove one generation owns each task/buffer.
- [ ] Prove counters remain monotonic unless explicitly baseline-reset.

---

# Phase 11 — Full regression, resource gates, documentation, and handoff

## FD-25 — Complete host and device test suites [P0 partial]

### Verified at prior phase baselines

- [x] Full host CI passed through the FD-16 phase baseline.
- [x] Focused state, policy, ring, converter, HFP lifecycle, SLC, incoming audio, audio control, command, event, and diagnostics suites existed and passed at their cited baselines.
- [x] ESP-IDF v5.5.1 compile-only validation passed through FD-16.
- [x] Latest review-fix ESP-IDF compile-only build passed at `e48341ae665781dd6da6e40c4137bfbead4d1205`.
- [x] Latest image size: 1,025,808 bytes (`0xFA710`).
- [x] Factory partition: 1,769,472 bytes (`0x1B0000`).
- [x] Latest compile-only partition headroom: 743,664 bytes (`0xB58F0`), 42% free.
- [x] No hardware was flashed.

### Still required for the review-fix head

- [ ] Pass the HFP incoming-audio sanitizer suite.
- [ ] Pass the HFP audio-control sanitizer suite.
- [ ] Pass the HFP command sanitizer suite.
- [ ] Pass FD-16 policy/runtime and A2DP binding integration tests.
- [ ] Pass the full CTest suite.
- [ ] Pass changed-Python lint and Python unit gates.
- [ ] Record the exact host workflow run ID and result for the final documentation head.

### Device-test and hardware coverage still pending

- [ ] Complete applicable on-device/mock test binaries.
- [ ] Obtain approval before any device-test flash.
- [ ] Record and restore the exact production firmware after approved device testing.

## FD-26 — Run resource acceptance matrix [P0 hardware pending]

- [x] Compile-only app-partition headroom exceeds 256 KiB.
- [ ] Record heap and stack checkpoints at boot, A2DP connection/stream, HFP SLC, CVSD SCO, simultaneous mode, HFP downlink, mSBC, and post-stop.
- [ ] Minimum internal heap is at least 32 KiB or has an explicit reviewed exception.
- [ ] Largest internal block is at least 16 KiB or has an explicit reviewed exception.
- [ ] Every affected task has measured stack margin.
- [ ] Callback p99 is below 500 microseconds or has an explicit reviewed exception.
- [ ] Callback maximum is below 2 milliseconds or has an explicit reviewed exception.
- [ ] No persistent heap loss occurs over 100 start/stop cycles.

## FD-27 — Final hardware acceptance and soak [P0 hardware pending]

- [ ] Existing A2DP-only behavior.
- [ ] CVSD A2DP plus HFP microphone.
- [ ] CVSD HFP full duplex after FD-18/FD-19.
- [ ] mSBC modes after FD-21.
- [ ] Receiver absent/stalled, earbud loss, reconnect, and repeated mode changes.
- [ ] Every existing playback source.
- [ ] Thirty-minute simultaneous playback/microphone soak.
- [ ] Zero sustained overflow, underflow, timeout, invalid-frame, watchdog, panic, brownout, or reboot failures.
- [ ] Record RF conditions, hardware models, codec, mode, and final counters.

## FD-28 — Update maintained documentation [P1 partial]

- [ ] Update `esp_bt_audio_source/README.md` after hardware behavior is known.
- [ ] Update architecture and command/event protocol documentation.
- [ ] Add the final wiring and receiver checklist.
- [ ] Document CVSD/mSBC behavior and earbud compatibility.
- [ ] Document counters, failure interpretation, and recovery.
- [ ] Add final runtime resource measurements.
- [ ] State explicitly that microphone audio does not start automatically at boot.
- [ ] Update `memory.md` with verified final results.

## FD-29 — Final code review and handoff [P0 in progress]

### Review-fix implementation present

- [x] Callback overlap is fail-closed and visible.
- [x] A2DP lifecycle binding prevents stale reconnect-boundary events from self-stamping with the current session.
- [x] Health-reporting failures are visible.
- [x] Status-unavailable command results cannot look like ordinary success.
- [x] Callback stack-array bounds are statically enforced and documented.
- [x] This TODO distinguishes software, compile, host-baseline, hardware, and future status.

### Final review/closeout still required

- [ ] Verify the latest full host CI run.
- [ ] Complete the final ignored-error/callback-allocation/blocking/I2S/logging/static-synchronization sweep.
- [ ] Complete the final silent-zero-fill/fake-success/unbounded-retry/stale-event sweep.
- [ ] Confirm no generated build or log artifacts are committed.
- [ ] Create `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md` only after the review-fix host workflow passes.
- [ ] Record starting and ending SHAs, exact files changed, tests, compile result, no-flash status, limitations, and pending hardware work.
- [ ] Confirm every referenced assistant-created document exists at its exact path.

---

# Definition of done

Do not mark the full-duplex project complete until all of the following are true:

- [ ] One real HFP AG SLC and SCO/eSCO connection operates safely.
- [ ] CVSD microphone PCM reaches I2S0 at the fixed wire format.
- [ ] mSBC microphone PCM is implemented and hardware-validated if FD-21 remains in release scope.
- [ ] GPIO32/33/27 are verified physically.
- [ ] A2DP plus HFP microphone behavior is known for each target earbud.
- [ ] FD-18 and FD-19 provide the real HFP compatibility downlink.
- [ ] HFP full-duplex compatibility mode works when A2DP is suspended.
- [x] Mode changes, incompatibilities, unavailable metrics, rejected frames, and implemented fallbacks are represented explicitly in software.
- [x] The implemented incoming callback is designed not to block, allocate, call I2S, resample, or log per frame.
- [ ] Latest full host CI passes after all review-fix documentation commits.
- [x] Latest ESP-IDF compile-only build passes with more than 256 KiB app-partition headroom.
- [ ] Runtime heap, stack, callback, and soak gates pass or receive explicit reviewed exceptions.
- [ ] The final thirty-minute hardware soak passes.
- [ ] Maintained documentation and `memory.md` are current.
- [x] Hardware was not flashed without explicit user approval.
