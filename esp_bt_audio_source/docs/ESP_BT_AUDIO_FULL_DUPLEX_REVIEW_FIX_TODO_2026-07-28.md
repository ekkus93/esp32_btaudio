# ESP32 Bluetooth Audio Full-Duplex Review Fix TODO

**Created:** 2026-07-28  
**Target branch:** `feature/esp-bt-audio-duplex`  
**Primary TODO:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`  
**Primary project:** `esp_bt_audio_source/`  
**Scope:** Fix the concrete issues found in the post-FD-16 code review. This is not a replacement for the full-duplex TODO. Hardware-gated phases remain pending.

---

## 0. Non-negotiable working rules

- [ ] Work directly on `feature/esp-bt-audio-duplex` only.
- [ ] Do not create helper branches.
- [ ] Do not create a new PR.
- [ ] Do not flash hardware unless Phillip explicitly approves it in the current conversation.
- [ ] Do not touch `esp_i2s_source/` for this fix set.
- [ ] Do not treat successful compile as proof of runtime correctness.
- [ ] Do not add quiet fallback, fake success, silent data loss, or unbounded retries.
- [ ] Keep HFP/profile/SCO implementation under `components/bt_manager`; command code must route through public manager APIs only.
- [ ] Keep I2S0 TX and voice conversion code under `components/audio_processor`.
- [ ] Every failure path added here must either return the exact error, emit a stable visible record, increment an explicit counter, or intentionally quarantine/fault the owning component.
- [ ] Preserve the current FD-16 truth boundary: `HFP_FULL` may reserve HFP downlink ownership, but must not report operational full-duplex playback until FD-18/FD-19 implement the downlink.

---

## 1. Review findings to fix

This TODO covers these review findings:

1. Possible multi-writer race in HFP incoming callback 64-bit counters.
2. Same-peer stale A2DP events can still be misapplied to a newer session because the production A2DP callback captures the current generation at handling time.
3. Some health-reporting calls discard `bt_duplex_set_health()` failures.
4. `HFP AUDIO START` / `HFP AUDIO STOP` can return an OK command record while status retrieval failed.
5. HFP incoming callback stack/cadence/heap behavior still needs explicit validation and gates.
6. The original full-duplex TODO checkboxes are stale after FD-08 through FD-13 and FD-16 software work.

---

# RF-00 — Confirm branch, baseline, and review scope [P0]

## Required work

- [ ] Confirm the current branch is `feature/esp-bt-audio-duplex`.
- [ ] Confirm no unrelated local changes are present before editing.
- [ ] Record the starting commit SHA in the closeout notes for this fix set.
- [ ] Read these files before editing:
  - [ ] `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
  - [ ] `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_FD16_CLOSEOUT_2026-07-28.md`
  - [ ] `esp_bt_audio_source/components/bt_manager/bt_hfp_audio.c`
  - [ ] `esp_bt_audio_source/components/bt_manager/bt_hfp_audio_control.c`
  - [ ] `esp_bt_audio_source/components/bt_manager/bt_duplex_policy_runtime.c`
  - [ ] `esp_bt_audio_source/components/bt_manager/bt_events_a2dp.c`
  - [ ] `esp_bt_audio_source/components/command_interface/cmd_handlers_hfp_fd11_v2.c`
- [ ] Identify the existing focused host test binaries for:
  - [ ] HFP incoming audio callback.
  - [ ] HFP audio control.
  - [ ] FD-16 policy/runtime behavior.
  - [ ] HFP command handlers.

## Acceptance

- [ ] Starting commit and touched files are listed in the closeout section of this document or a companion closeout doc.
- [ ] No edits outside the review-fix scope are included.

---

# RF-01 — Make HFP incoming callback counters concurrency-correct [P0]

## Problem

`components/bt_manager/bt_hfp_audio.c` uses a custom `sequence/low/high` counter for 64-bit values. The reader side avoids torn reads, but the writer side performs a non-atomic read/modify/write of `low` and `high`. That is safe only if there is exactly one writer. The callback path tracks `active_callbacks`, so the code itself admits callback overlap is possible or at least worth measuring.

If two callbacks update the same counter concurrently, increments can be lost. That is a silent observability failure.

## Required work

- [ ] Decide and document the actual concurrency contract for ESP-IDF HFP incoming audio callbacks:
  - [ ] Serialized by ESP-IDF / BT stack, or
  - [ ] Potentially concurrent and must be multi-writer safe.
- [ ] Do not rely on undocumented callback serialization.
- [ ] Implement one of the following fixes:
  - [ ] Preferred: make the callback counters multi-writer safe without blocking for long periods.
  - [ ] Acceptable only if proven: add an explicit serialization guard and fail visibly if overlap occurs.
- [ ] Preserve callback constraints:
  - [ ] No heap allocation from the callback path.
  - [ ] No blocking wait from the callback path.
  - [ ] No I2S driver call from the callback path.
  - [ ] No per-frame logging from the callback path.
- [ ] Ensure the lifetime callback max and over-budget counters remain process-lifetime values and are not reset by HFP profile teardown or `HFP RESETSTATS`.

## Suggested implementation option A — lock-free saturating 64-bit CAS where available

Use this only if the toolchain target supports lock-free 64-bit atomics on the host and the target, or if the target wrapper proves correctness. Do not assume this is true on ESP32.

```c
static void counter64_add_saturating(atomic_uint_fast64_t *counter,
                                     uint64_t amount)
{
    uint64_t old = atomic_load_explicit(counter, memory_order_relaxed);
    for (;;) {
        uint64_t next = UINT64_MAX - old < amount ? UINT64_MAX : old + amount;
        if (atomic_compare_exchange_weak_explicit(counter, &old, next,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}
```

## Suggested implementation option B — short critical section around counter update

Use this if 64-bit atomics are not lock-free on ESP32. The critical section must only cover integer updates. Do not include memory copies, conversion, I2S push, logging, or health/event calls.

```c
typedef struct {
    portMUX_TYPE mux;
    uint64_t value;
} bt_hfp_audio_counter64_t;

static void counter64_add(bt_hfp_audio_counter64_t *counter, uint64_t amount)
{
    if (counter == NULL || amount == 0) return;
    portENTER_CRITICAL(&counter->mux);
    counter->value = UINT64_MAX - counter->value < amount
        ? UINT64_MAX
        : counter->value + amount;
    portEXIT_CRITICAL(&counter->mux);
}
```

Adapt for host tests using the repository's platform shim instead of raw FreeRTOS when needed.

## Required tests

- [ ] Add a focused unit test that simulates two or more concurrent writers updating the same HFP audio counter.
- [ ] Verify no increments are lost after repeated concurrent updates.
- [ ] Verify saturation behavior at or near `UINT64_MAX` if saturation is implemented.
- [ ] Verify callback snapshot reads do not return torn values.
- [ ] Verify `HFP RESETSTATS` does not clear process-lifetime callback max/over-budget fields.
- [ ] Run the HFP incoming callback sanitizer suite.

## Acceptance

- [ ] No callback counter can lose increments under concurrent callback execution.
- [ ] No callback counter reader can observe a torn value.
- [ ] Callback path still has no heap allocation, no blocking wait, no direct I2S call, and no per-frame logging.

---

# RF-02 — Harden same-peer stale A2DP event handling [P0]

## Problem

FD-16 added `expected_generation` validation to the policy adapter. That rejects wrong-generation calls if the caller has a real generation token. The production A2DP callback currently captures the generation by reading the current manager status at callback-handling time. A late A2DP event for the same peer can therefore be stamped with the newer current generation and applied to the wrong session.

This is not a wrong-peer problem. It is a same-peer stale-event problem.

## Required work

- [ ] Audit `components/bt_manager/bt_events_a2dp.c` and all call paths into:
  - [ ] `bt_manager_hfp_handle_a2dp_profile_event()`
  - [ ] `bt_manager_hfp_handle_a2dp_audio_event()`
- [ ] Add an explicit A2DP policy binding token that is captured when the A2DP session is first associated with a duplex generation.
- [ ] Ensure all later A2DP profile/audio policy calls use the bound generation token, not a freshly-read current generation unless this is the first binding event.
- [ ] Reject or ignore A2DP audio events if no A2DP generation binding exists.
- [ ] On A2DP disconnect, clear the binding only after the disconnect event has been applied or explicitly rejected.
- [ ] Preserve existing A2DP playback behavior when HFP/duplex is disabled.
- [ ] Do not create a false new duplex session for a late same-peer event after an old disconnect if the manager no longer considers that A2DP session active.

## Suggested shape

Create a small runtime-owned A2DP policy binding, guarded by the same manager lock used for `bt_ctx`:

```c
typedef struct {
    bool valid;
    char peer_mac[BT_DUPLEX_MAC_STR_LEN];
    uint32_t generation;
    uint32_t serial;
} bt_a2dp_policy_binding_t;
```

Rules:

- First connected/connecting event may create or bind the generation.
- Audio events require an existing binding.
- Wrong peer rejects without mutating the binding.
- Disconnect clears the binding only after state handling is complete.
- Reconnect of the same MAC receives a new serial/generation boundary.

## Required tests

- [ ] Same-peer stale A2DP audio event from old generation is rejected after reconnect.
- [ ] Same-peer stale A2DP disconnect from old generation cannot clear the new generation.
- [ ] Wrong-peer A2DP event still rejects and increments the existing wrong-peer path.
- [ ] First valid A2DP connect creates/binds a duplex session when appropriate.
- [ ] A2DP audio event with no binding returns `ESP_ERR_INVALID_STATE` or a documented visible rejection.
- [ ] Existing A2DP-only tests still pass with HFP mode disabled.
- [ ] FD-16 ordering-permutation tests still pass.

## Acceptance

- [ ] A late same-peer event cannot mutate a newer session.
- [ ] A2DP events no longer self-stamp with whatever generation happens to be current.
- [ ] The residual limitation, if any remains because ESP-IDF lacks a lower-layer session token, is explicitly documented in the closeout and tested as far as possible.

---

# RF-03 — Make health-report failures visible [P0]

## Problem

Some fault/cleanup paths call a local helper that discards the return value from `bt_duplex_set_health()`. That means the code can fail to record the very fault/degraded/quarantine condition it is trying to surface.

## Required work

- [ ] Replace void helper patterns like this:

```c
static void set_health(...) {
    (void)bt_duplex_set_health(...);
}
```

with an error-returning helper.

- [ ] Add a visible counter for failed health reporting, likely in the HFP audio-control snapshot/stats:
  - [ ] `health_report_failures`
  - [ ] `last_health_report_error`
- [ ] Whenever health reporting fails:
  - [ ] Preserve the primary operation error where that is the actionable cause.
  - [ ] Increment `health_report_failures`.
  - [ ] Store `last_health_report_error`.
  - [ ] Emit/log one bounded error if the context allows logging outside callbacks.
- [ ] Do not attempt recursive health reporting for a health-report failure.
- [ ] Update `HFP STATS` to include the new health-report failure counter.
- [ ] Update command tests to verify the counter appears in the stable stats output.

## Suggested helper shape

```c
static esp_err_t set_health_checked(uint32_t generation,
                                    const char *peer_mac,
                                    bt_audio_health_t health,
                                    esp_err_t cause,
                                    const char *text)
{
    esp_err_t err = bt_duplex_set_health(generation, peer_mac, health,
                                         cause, text);
    if (err != ESP_OK) {
        record_health_report_failure(err);
    }
    return err;
}
```

Use the returned error to decide whether a cleanup path should return the original cause or the health-report failure. Default policy: return the original operation failure, but make the health-report failure visible through counters and last-error diagnostics.

## Required tests

- [ ] Inject `bt_duplex_set_health()` failure in HFP audio connect timeout path.
- [ ] Inject `bt_duplex_set_health()` failure in HFP audio disconnect timeout path.
- [ ] Inject `bt_duplex_set_health()` failure in unexpected remote SCO cleanup path.
- [ ] Verify each path increments `health_report_failures`.
- [ ] Verify each path stores `last_health_report_error`.
- [ ] Verify primary operation errors are not replaced by fake success.
- [ ] Verify `HFP STATS` includes health-report failures.

## Acceptance

- [ ] No health-reporting failure is silently discarded.
- [ ] Fault/degraded/quarantine visibility paths are themselves observable when they fail.

---

# RF-04 — Tighten `HFP AUDIO START/STOP` status-unavailable command semantics [P1]

## Problem

`HFP AUDIO START` and `HFP AUDIO STOP` can send an OK response with subtype `AUDIO_STARTED_STATUS_UNAVAILABLE` or `AUDIO_STOPPED_STATUS_UNAVAILABLE` if the operation succeeds but the follow-up status read fails. That is not fake success for the start/stop operation, but scripts that only check top-level OK may miss that the status snapshot failed.

## Required work

- [ ] Decide the command protocol contract for operation success plus status-read failure.
- [ ] Do not return a plain OK that looks equivalent to ordinary `AUDIO_STARTED` or `AUDIO_STOPPED`.
- [ ] Implement one of these approaches:
  - [ ] Preferred: emit an `ERR|HFP|STATUS_UNAVAILABLE|...` record after the operation success record and before final return.
  - [ ] Acceptable: return `ERR|HFP|AUDIO_STATUS_UNAVAILABLE|OPERATION=START,STATUS_ERROR=<err>` while explicitly documenting that the lower operation may have succeeded.
  - [ ] Acceptable for backwards compatibility: keep OK subtype, but also increment a visible command/status warning counter and add tests that clients can distinguish it.
- [ ] Ensure exact `esp_err_t` names are preserved in the wire output.
- [ ] Do not add a fallback status snapshot or fake zeros.

## Required tests

- [ ] `HFP AUDIO START` lower operation succeeds but `bt_manager_hfp_get_status()` fails.
- [ ] `HFP AUDIO STOP` lower operation succeeds but `bt_manager_hfp_get_status()` fails.
- [ ] Verify response cannot be confused with ordinary `AUDIO_STARTED` or `AUDIO_STOPPED`.
- [ ] Verify exact ESP error string is present.
- [ ] Verify ordinary success output remains unchanged when status retrieval succeeds.

## Acceptance

- [ ] A command client cannot accidentally treat status-unavailable as a normal fully-observed success.
- [ ] No status fields are fabricated when status read fails.

---

# RF-05 — Add explicit callback stack/cadence/resource validation gates [P1 software, P0 hardware before final full-duplex]

## Problem

The incoming callback path and I2S push path use bounded stack arrays. This may be acceptable, but the real ESP32 callback/task stack margins are not yet validated under hardware timing.

## Required work

- [ ] Add a maintained documentation section describing callback stack use:
  - [ ] Incoming callback local aligned buffer size.
  - [ ] CVSD push conversion buffer size.
  - [ ] Maximum supported incoming CVSD samples per callback.
  - [ ] Expected worst-case stack contribution from these buffers.
- [ ] Add compile-time assertions for fixed maximum frame assumptions:
  - [ ] `HFP_I2S_CVSD_MAX_INPUT_SAMPLES` remains bounded.
  - [ ] converted sample buffer cannot exceed intended stack budget.
- [ ] Extend diagnostics or stats if needed so hardware runs can record:
  - [ ] HFP app task minimum stack high-water mark.
  - [ ] I2S writer task minimum stack high-water mark.
  - [ ] Callback max duration lifetime.
  - [ ] Callback over-budget lifetime count.
  - [ ] Incoming callback accept/drop/invalid/stale/bad/unsupported counters.
- [ ] Do not claim hardware validation in software-only closeout.

## Hardware-gated validation checklist

Do not run these until Phillip approves flashing and hardware testing.

- [ ] Build and flash only after explicit approval.
- [ ] Run real CVSD SCO with earbuds.
- [ ] Feed microphone audio long enough to exercise the callback path.
- [ ] Capture `HFP STATS` before, during, and after SCO.
- [ ] Confirm stack margins are above the reviewed threshold.
- [ ] Confirm callback max duration is below the reviewed maximum.
- [ ] Confirm callback over-budget counter stays zero or receives explicit reviewed exception.
- [ ] Confirm no ring overflows after stabilization.
- [ ] Confirm no sustained I2S underflows after stabilization.

## Acceptance

- [ ] Software contains explicit bounds and documentation for callback stack usage.
- [ ] Final full-duplex completion remains blocked until hardware resource gates pass or receive explicit reviewed exception.

---

# RF-06 — Reconcile the original TODO checkboxes without falsely completing hardware work [P1]

## Problem

`ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md` is stale. Some FD-08 through FD-13 and FD-16 software work is implemented, but hardware tasks remain incomplete. Leaving all original checklist boxes unchecked makes future handoff confusing; checking hardware boxes would be dishonest.

## Required work

- [ ] Update the original TODO file so it accurately distinguishes:
  - [ ] Software implemented.
  - [ ] Host tested.
  - [ ] ESP-IDF compile-only tested.
  - [ ] Hardware pending.
  - [ ] Future phase not implemented.
- [ ] Do not mark FD-14, FD-15, FD-17, FD-20, FD-21 hardware work complete.
- [ ] Do not mark FD-18 or FD-19 complete until actual HFP downlink voice tap/outgoing callback exists.
- [ ] Add short notes under FD-08 through FD-13 that hardware validation remains pending where applicable.
- [ ] Add a short note under FD-16 that software policy is complete but real simultaneous A2DP/HFP behavior is FD-17 hardware validation.
- [ ] Include links to closeout documents that actually exist in the repository.
- [ ] Do not reference assistant-created files that are not committed at the exact path.

## Acceptance

- [ ] A future implementation agent can tell exactly what is done and what remains.
- [ ] No hardware task is checked off as complete without actual hardware evidence.
- [ ] No missing companion document is referenced.

---

# RF-07 — Focused regression suite for this review-fix set [P0]

## Required host tests

Run the focused suites affected by this TODO after code changes:

- [ ] HFP incoming audio callback suite.
- [ ] HFP audio-control suite.
- [ ] HFP command handler suite.
- [ ] FD-16 policy/runtime suite.
- [ ] A2DP event integration tests.
- [ ] Full host CTest suite.
- [ ] Changed Python lint/unit gates used by the repository.

## Required device compile-only validation

- [ ] Run ESP-IDF v5.5.1 compile-only build for `esp_bt_audio_source`.
- [ ] Record app binary size and partition headroom.
- [ ] Confirm no hardware flashing occurred.

## Required static/manual sweeps

- [ ] Search HFP callback path for `malloc`, `calloc`, `free`, logging, blocking waits, and direct I2S calls.
- [ ] Search for ignored `esp_err_t` results in new/modified code.
- [ ] Search for `vTaskDelete(` in source code and confirm no external live-task deletion shortcut was added.
- [ ] Search for `volatile` in new/modified code and confirm it is not used as synchronization.
- [ ] Search for zero-fill/silence paths and confirm counters are incremented.
- [ ] Search for success command responses after lower-level task/profile/I2S failures.
- [ ] Search for A2DP/HFP events that mutate state without peer/generation or binding-token validation.

## Acceptance

- [ ] All focused host tests pass under AddressSanitizer/UndefinedBehaviorSanitizer where supported.
- [ ] Full host CI passes.
- [ ] ESP-IDF v5.5.1 compile-only build passes.
- [ ] Any skipped hardware validation is explicitly listed as pending, not complete.

---

# RF-08 — Closeout and handoff [P1]

## Required closeout contents

Create a closeout document only after RF-01 through RF-07 are complete. Suggested path:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_CLOSEOUT_2026-07-28.md`

The closeout must include:

- [ ] Starting commit SHA.
- [ ] Ending commit SHA.
- [ ] Exact files changed.
- [ ] Summary of each fixed review issue.
- [ ] Tests run and results.
- [ ] ESP-IDF compile-only build result.
- [ ] Whether hardware was flashed. Expected answer for software-only fix set: no.
- [ ] Remaining hardware-gated work.
- [ ] Remaining known limitations, especially any same-peer stale-event limitation that could not be fully eliminated.
- [ ] Updated TODO status.

## Acceptance

- [ ] Closeout is committed on `feature/esp-bt-audio-duplex`.
- [ ] No new branch is created.
- [ ] No new PR is created.
- [ ] The repo contains every assistant-created document referenced by the closeout.

---

# Definition of done for this review-fix TODO

Do not mark this review-fix TODO complete until:

- [ ] HFP incoming callback counters are concurrency-correct or callback serialization is explicitly enforced and tested.
- [ ] Same-peer stale A2DP events cannot mutate a newer session, or any residual ESP-IDF limitation is explicitly documented and bounded by tests.
- [ ] Health-reporting failures are visible through counters/status/stats and are tested.
- [ ] `HFP AUDIO START/STOP` status-unavailable behavior cannot be mistaken for ordinary fully-observed success.
- [ ] Callback stack/cadence/resource validation gates are documented and integrated into diagnostics/acceptance.
- [ ] The original full-duplex TODO accurately reflects software-complete versus hardware-pending work.
- [ ] Focused host tests pass.
- [ ] Full host CI passes.
- [ ] ESP-IDF v5.5.1 compile-only build passes.
- [ ] Hardware was not flashed without explicit approval.
