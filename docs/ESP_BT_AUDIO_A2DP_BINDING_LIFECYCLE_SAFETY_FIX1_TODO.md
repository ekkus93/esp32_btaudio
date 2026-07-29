# ESP32 Bluetooth Audio — A2DP Binding Lifecycle Safety FIX1 TODO

**Repository:** `ekkus93/esp32_btaudio`  
**Target branch:** `feature/esp-bt-audio-duplex`  
**Primary project:** `esp_bt_audio_source`  
**Baseline implementation reviewed:** `0e08b5a083b3971e5a4b56ddec5745387dbfdfce`  
**Status:** Implementation required  
**Scope:** A2DP identity binding reset safety, manager lifecycle ordering, late terminal-event handling, regression coverage, and configuration-change audit

---

## 1. Purpose

This TODO is a self-contained implementation plan for correcting the remaining safety problems in the A2DP binding lifecycle changes introduced by commit `0e08b5a083b3971e5a4b56ddec5745387dbfdfce`.

The baseline commit correctly identified that the static A2DP policy binding can leak across Bluetooth manager init/deinit cycles. However, the current implementation also introduced unsafe fallback behavior and weakened identity validation for unbound terminal audio events.

The work in this file must preserve the valid part of the fix—stale bindings must not survive a confirmed clean lifecycle boundary—while restoring fail-closed synchronization and peer/session identity guarantees.

This file does not depend on any separate review report or unpublished companion document. All required behavior is stated here.

---

## 2. Problems that must be eliminated

### 2.1 Unsynchronized reset fallback

`bt_events_a2dp_reset_binding()` currently clears `s_policy_binding` even when `bt_ctx_lock()` fails. This means shared state can be modified without the mutex that is documented as protecting it.

The current init ordering guarantees that the fallback is taken because the reset is called before `s_bt_ctx_mutex` is created.

### 2.2 Reset during an unconfirmed callback shutdown

`bt_manager_deinit()` currently resets the A2DP binding even when Bluedroid deinitialization failed and callbacks may still be alive. This contradicts the manager's quarantine policy and can expose late callbacks to partially cleared identity state.

### 2.3 Unbound STOP/SUSPEND events are accepted too broadly

`capture_audio_binding()` currently treats every unbound `STOPPED` or `REMOTE_SUSPEND` event as legitimate, sets `bt_ctx.audio_playing = false`, and returns success without validating peer MAC, connection handle, lifecycle serial, or generation.

This allows stale or unrelated terminal events to alter manager state and flow into downstream callbacks without trusted identity.

### 2.4 New behavior lacks direct regression tests

The production lifecycle and event-identity behavior changed, but no focused tests were added for reset failure, clean lifecycle reset, failed teardown preservation, or late terminal-event suppression.

### 2.5 Unrelated `sdkconfig` changes were bundled with the runtime fix

The baseline commit also changed HFP AG, HCI SCO, synchronous connection, I2S, and generated PCM configuration. Those changes may be required, but they must be audited and documented independently from the A2DP lifecycle fix.

---

## 3. Non-negotiable safety invariants

The implementation is not complete unless all of the following are true:

1. `s_policy_binding` is never written without the same synchronization discipline used by normal A2DP event processing.
2. A lock failure never causes an unlocked `memset`, unlocked field update, or best-effort reset.
3. Manager initialization creates the mutex before calling any function that requires that mutex.
4. A reset failure during initialization is visible to the caller and aborts initialization.
5. A2DP binding state is cleared only after Bluetooth callbacks are confirmed stopped, except at process startup before callbacks can possibly exist.
6. If callback shutdown is not confirmed, the binding remains intact and the manager remains quarantined until reboot.
7. An unbound `STARTED` event remains fail-closed and is rejected.
8. An unbound `STOPPED` or `REMOTE_SUSPEND` event does not mutate `bt_ctx`, does not call `bt_audio_state_cb()`, and does not reach the duplex policy adapter.
9. A late terminal event may be treated as an idempotent no-op, but it must not be represented as a successfully bound event.
10. Wrong-peer and stale-handle events cannot affect the active or next session.
11. No test may pass by weakening an exact assertion, suppressing an error, or replacing a failure with a silent fallback.
12. No hardware flashing is part of this TODO. Device validation is compile-only unless the repository owner separately authorizes flashing.

---

## 4. Required implementation sequence

Perform the tasks in the order listed. Do not skip ahead and then compensate with test-only behavior.

---

## Task A2DP-FIX1-01 — Establish the baseline and protect scope

- [ ] Confirm the active branch is `feature/esp-bt-audio-duplex`.
- [ ] Confirm the branch contains baseline commit `0e08b5a083b3971e5a4b56ddec5745387dbfdfce` or a direct descendant.
- [ ] Record the current branch head in the implementation notes or final commit message.
- [ ] Inspect all callers and declarations of:
  - [ ] `bt_events_a2dp_reset_binding()`
  - [ ] `bt_events_a2dp_test_reset_binding()`
  - [ ] `capture_audio_binding()`
  - [ ] `bt_manager_init()`
  - [ ] `bt_manager_deinit()`
  - [ ] the init rollback path labeled `fail:` in `bt_manager.c`
- [ ] Identify the existing host-test target that compiles the real `bt_events_a2dp.c` implementation.
- [ ] Identify the existing platform-sync mock mechanism for injecting mutex create/lock failures.
- [ ] Do not create a new branch or pull request.
- [ ] Do not modify production behavior outside the A2DP binding lifecycle, terminal-event handling, and strictly necessary diagnostics.

**Acceptance condition:** The implementer can name every production call site and the exact host-test target that exercises the real code before modifying anything.

---

## Task A2DP-FIX1-02 — Make binding reset a checked, lock-respecting operation

### A2DP-FIX1-02.1 — Change the API contract

- [ ] Change the production declaration from:

```c
void bt_events_a2dp_reset_binding(void);
```

  to:

```c
esp_err_t bt_events_a2dp_reset_binding(void);
```

- [ ] Update the header documentation to state:
  - [ ] The function requires the Bluetooth manager context mutex to exist.
  - [ ] The function acquires the mutex internally.
  - [ ] The function returns the exact lock error without changing the binding.
  - [ ] The function returns `ESP_OK` only after the binding is cleared under lock.
  - [ ] The function must not be called after the mutex has been deleted.
- [ ] Update non-ESP/non-unit-test inline stubs to return `ESP_OK` rather than being `void`.

### A2DP-FIX1-02.2 — Remove the unlocked fallback

- [ ] Replace the current implementation with fail-closed behavior equivalent to:

```c
esp_err_t bt_events_a2dp_reset_binding(void)
{
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) {
        return err;
    }

    memset(&s_policy_binding, 0, sizeof(s_policy_binding));
    bt_ctx_unlock();
    return ESP_OK;
}
```

- [ ] Do not retain any branch that clears `s_policy_binding` after lock failure.
- [ ] Do not add a second “unsafe,” “force,” “best effort,” or “no-lock” public reset function.
- [ ] Do not hide a reset failure behind logging alone.
- [ ] Preserve exact `esp_err_t` values returned by `bt_ctx_lock()`.

### A2DP-FIX1-02.3 — Update the test reset helper

- [ ] Change `bt_events_a2dp_test_reset_binding()` to return `esp_err_t`, or make it explicitly assert/propagate the production reset result in the test fixture.
- [ ] Do not let the test helper restore the removed unlocked fallback.
- [ ] Ensure tests that need pristine state fail visibly if reset cannot acquire the lock.

**Acceptance condition:** A repository search shows no unlocked write to `s_policy_binding` in any reset path.

---

## Task A2DP-FIX1-03 — Correct manager initialization ordering

### A2DP-FIX1-03.1 — Create the mutex before resetting guarded state

- [ ] In `bt_manager_init()`, move the reset so it occurs only after `s_bt_ctx_mutex` is successfully created.
- [ ] Keep the reset before Bluetooth controller/Bluedroid/profile initialization and before any callback registration.
- [ ] Use an ordering equivalent to:

```c
s_bt_ctx_mutex = platform_mutex_create();
if (s_bt_ctx_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create bt_ctx mutex");
    return ESP_ERR_NO_MEM;
}

esp_err_t reset_err = bt_events_a2dp_reset_binding();
if (reset_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to reset A2DP binding during init: %s",
             esp_err_to_name(reset_err));
    platform_mutex_delete(s_bt_ctx_mutex);
    s_bt_ctx_mutex = NULL;
    return reset_err;
}

s_autostart_enabled = true;
```

- [ ] Preserve the repository's preferred ordering for `s_autostart_enabled`, but do not reset shared A2DP state before mutex creation.
- [ ] Ensure no callbacks can run before the reset completes.

### A2DP-FIX1-03.2 — Handle initialization reset failure transactionally

- [ ] If binding reset fails immediately after mutex creation:
  - [ ] Log the exact error.
  - [ ] Delete the newly created mutex.
  - [ ] Set `s_bt_ctx_mutex = NULL`.
  - [ ] Return the exact reset error.
  - [ ] Do not continue controller, Bluedroid, profile, or scan-mode initialization.
- [ ] Verify `bt_ctx.initialized` remains false.
- [ ] Verify no partial resource flags are left set.
- [ ] Do not convert the error to generic `ESP_FAIL`.

**Acceptance condition:** It is impossible for normal init to call the checked reset while `s_bt_ctx_mutex == NULL`.

---

## Task A2DP-FIX1-04 — Reset only after callback shutdown is confirmed

### A2DP-FIX1-04.1 — Correct normal deinit

- [ ] Remove the unconditional `bt_events_a2dp_reset_binding()` call from the common tail of `bt_manager_deinit()`.
- [ ] Call the checked reset only in the `callbacks_stopped == true` path.
- [ ] Perform the reset while `s_bt_ctx_mutex` still exists and before deleting that mutex.
- [ ] Use a sequence equivalent to:

```c
if (callbacks_stopped) {
    bt_hfp_ag_force_cleanup_after_stack_shutdown();
    bt_duplex_state_deinit();

    esp_err_t reset_err = bt_events_a2dp_reset_binding();
    if (reset_err != ESP_OK) {
        ESP_LOGE(TAG, "A2DP binding reset during deinit failed: %s",
                 esp_err_to_name(reset_err));
        if (first_error == ESP_OK) {
            first_error = reset_err;
        }
    }

    ESP_LOGI(TAG, "Bluetooth manager deinitialized");
} else {
    ESP_LOGE(TAG,
             "Bluedroid shutdown was not confirmed; preserving callback-owned state and quarantining manager");
}
```

- [ ] Adjust ordering if necessary so every component that can still inspect the binding is shut down before the reset, but the mutex remains valid until after reset.
- [ ] If reset fails, make the failure visible through `first_error` and quarantine the manager.
- [ ] Do not clear the binding when `callbacks_stopped == false`.
- [ ] Do not delete the mutex when callbacks may still be alive.

### A2DP-FIX1-04.2 — Correct init rollback

- [ ] In the `bt_manager_init()` failure rollback path, clear the A2DP binding only when callback shutdown has been confirmed.
- [ ] Perform the reset before deleting `s_bt_ctx_mutex`.
- [ ] If reset fails during rollback:
  - [ ] Log both the original initialization error and reset error.
  - [ ] Preserve the original initialization error as the primary return value.
  - [ ] Mark cleanup incomplete.
  - [ ] Quarantine the manager.
- [ ] If callback shutdown is not confirmed:
  - [ ] Preserve the binding.
  - [ ] Preserve callback-owned state.
  - [ ] Quarantine the manager.
  - [ ] Do not attempt an unlocked cleanup.

### A2DP-FIX1-04.3 — Preserve clean reinitialization semantics

- [ ] Verify a successful deinit clears the binding before mutex deletion.
- [ ] Verify a subsequent init starts with an empty binding.
- [ ] Verify a failed/unconfirmed teardown prevents reinit through the existing quarantine gate.
- [ ] Do not clear quarantine to make a test pass.

**Acceptance condition:** Every production binding reset occurs either before callback registration during init or after confirmed callback shutdown during teardown, always with a valid mutex.

---

## Task A2DP-FIX1-05 — Restore fail-closed handling for unbound audio events

### A2DP-FIX1-05.1 — Remove state mutation from the unbound terminal-event path

- [ ] Delete the behavior that performs:

```c
bt_ctx.audio_playing = false;
return ESP_OK;
```

  merely because the event is not `BT_A2DP_AUDIO_STARTED`.

- [ ] When `s_policy_binding.valid == false`:
  - [ ] Reject `BT_A2DP_AUDIO_STARTED` with `ESP_ERR_INVALID_STATE`.
  - [ ] Treat `BT_A2DP_AUDIO_STOPPED` as an ignored terminal event by returning `ESP_ERR_NOT_FOUND`.
  - [ ] Treat `BT_A2DP_AUDIO_REMOTE_SUSPENDED` as an ignored terminal event by returning `ESP_ERR_NOT_FOUND`.
- [ ] For all unbound event types:
  - [ ] Do not modify `bt_ctx.audio_playing`.
  - [ ] Do not fabricate a lifecycle serial.
  - [ ] Do not fabricate a duplex generation.
  - [ ] Do not call `bt_audio_state_cb()`.
  - [ ] Do not call `bt_manager_hfp_handle_a2dp_audio_event()`.
- [ ] Preserve existing exact rejection behavior for wrong peer and stale connection handle when a binding is valid.

Recommended control flow:

```c
if (!s_policy_binding.valid) {
    const bool terminal =
        bound->state == BT_A2DP_AUDIO_STOPPED ||
        bound->state == BT_A2DP_AUDIO_REMOTE_SUSPENDED;

    if (terminal) {
        increment_u64_saturating(
            &s_policy_binding.late_terminal_events_ignored);
        bt_ctx_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    increment_u64_saturating(
        &s_policy_binding.missing_binding_rejections);
    bt_ctx_unlock();
    record_rejected_unbound_event(bound->peer_mac);
    return ESP_ERR_INVALID_STATE;
}
```

### A2DP-FIX1-05.2 — Ensure handler-level suppression

- [ ] Confirm `prepare_audio_event()` propagates `ESP_ERR_NOT_FOUND` unchanged.
- [ ] Confirm `bt_events_handle_a2dp_audio()` returns before logging a successful state transition, invoking `bt_audio_state_cb()`, or applying duplex policy when identity preparation returns `ESP_ERR_NOT_FOUND`.
- [ ] Keep `report_policy_result()` from logging expected `ESP_ERR_NOT_FOUND` terminal races as hard failures.
- [ ] Do not convert `ESP_ERR_NOT_FOUND` to `ESP_OK` just to simplify the handler.

### A2DP-FIX1-05.3 — Do not add a tombstone unless evidence requires it

- [ ] Do not add disconnected-peer tombstone state in this fix unless an existing test or ESP-IDF contract proves it is necessary.
- [ ] Treat the authoritative disconnect transition as sufficient to set `audio_playing = false`.
- [ ] Document that a later STOP/SUSPEND is redundant and ignored, not “successfully rebound.”

**Acceptance condition:** An unbound terminal event is an observable no-op, not a partially applied event.

---

## Task A2DP-FIX1-06 — Add explicit diagnostics without creating noisy failures

### A2DP-FIX1-06.1 — Add an ignored-terminal counter

- [ ] Add a saturating counter to `a2dp_policy_binding_t`:

```c
uint64_t late_terminal_events_ignored;
```

- [ ] Preserve this counter across `clear_binding_if_identity()` in the same way existing rejection/sync counters are preserved across a connection clear.
- [ ] Clear it during a full manager lifecycle reset together with the rest of `s_policy_binding`.
- [ ] Add the field to the unit-test binding snapshot structure and getter.
- [ ] Do not reuse `missing_binding_rejections` for expected post-disconnect terminal races; keep hard rejection and benign ignored-event diagnostics distinct.

### A2DP-FIX1-06.2 — Add bounded runtime visibility

- [ ] Add a debug-level log for an ignored terminal event that includes:
  - [ ] Event state.
  - [ ] Peer MAC.
  - [ ] Connection handle.
  - [ ] Reason token such as `NO_ACTIVE_BINDING`.
- [ ] Do not emit an error-level log for the expected redundant terminal race.
- [ ] Retain error visibility for unbound START, wrong-peer, stale-handle, and generation failures.
- [ ] Do not silently discard lock failures or reset failures.

**Acceptance condition:** Operators can distinguish a benign late terminal event from a rejected attempt to start or alter an active session.

---

## Task A2DP-FIX1-07 — Add focused regression tests

Use the existing A2DP event/identity and manager lifecycle host-test targets. Do not create tests that compile only an inline no-op stub when the purpose is to validate production behavior.

### A2DP-FIX1-07.1 — Reset API tests

- [ ] Test that reset clears every binding identity field under a valid mutex:
  - [ ] `valid`
  - [ ] `peer_mac`
  - [ ] `conn_handle`
  - [ ] `lifecycle_serial`
  - [ ] `last_duplex_generation`
  - [ ] all rejection/sync/ignored counters
- [ ] Test that a forced mutex-lock failure:
  - [ ] Is returned exactly.
  - [ ] Leaves every binding field unchanged.
  - [ ] Does not perform an unlocked write.
- [ ] Test reset with no mutex returns `ESP_ERR_INVALID_STATE` and leaves state unchanged where a safe fixture can establish this condition.

### A2DP-FIX1-07.2 — Clean init/deinit lifecycle tests

- [ ] Establish a valid binding for peer A and connection handle A.
- [ ] Perform a successful manager deinit.
- [ ] Reinitialize the manager.
- [ ] Verify the old binding is absent.
- [ ] Verify peer B can establish a new binding without being rejected as wrong peer or stale handle.
- [ ] Verify lifecycle counters and serial semantics match the intended full-reset contract.

### A2DP-FIX1-07.3 — Failed teardown preservation tests

- [ ] Inject Bluedroid deinit failure so callback shutdown is not confirmed.
- [ ] Verify the manager is quarantined.
- [ ] Verify `s_policy_binding` remains unchanged.
- [ ] Verify the mutex remains available to potentially live callbacks.
- [ ] Verify reinit is rejected with `ESP_ERR_INVALID_STATE`.
- [ ] Verify no best-effort reset is attempted.

### A2DP-FIX1-07.4 — Init rollback tests

- [ ] Inject an initialization failure after callbacks have been registered.
- [ ] Case A: callback shutdown succeeds.
  - [ ] Verify binding reset occurs before mutex deletion.
  - [ ] Verify no binding survives.
- [ ] Case B: callback shutdown fails.
  - [ ] Verify binding is preserved.
  - [ ] Verify manager is quarantined.
  - [ ] Verify original init error is returned.
- [ ] Inject reset-lock failure during rollback and verify cleanup is marked incomplete without replacing the original init error.

### A2DP-FIX1-07.5 — Late terminal-event tests

For both `STOPPED` and `REMOTE_SUSPEND`:

- [ ] Establish a valid A2DP session.
- [ ] Deliver the authoritative `DISCONNECTED` event so the binding is cleared and `audio_playing` is already false.
- [ ] Deliver the late terminal event.
- [ ] Verify:
  - [ ] The event handler does not mutate `bt_ctx`.
  - [ ] `bt_audio_state_cb()` is not called.
  - [ ] The duplex policy adapter is not called.
  - [ ] No generation refresh is attempted.
  - [ ] `late_terminal_events_ignored` increases by exactly one.
  - [ ] Hard rejection counters do not increase.
  - [ ] The handler does not report fake success.

### A2DP-FIX1-07.6 — Unbound START test

- [ ] Deliver `STARTED` with no active binding.
- [ ] Verify:
  - [ ] It returns/reports `ESP_ERR_INVALID_STATE` through the existing path.
  - [ ] `missing_binding_rejections` increases by exactly one.
  - [ ] `late_terminal_events_ignored` does not increase.
  - [ ] `bt_ctx.audio_playing` remains unchanged.
  - [ ] No callback or policy update is delivered.

### A2DP-FIX1-07.7 — Cross-session stale-event tests

- [ ] Complete session A and clear its binding.
- [ ] Establish session B with a different peer and/or connection handle.
- [ ] Deliver a delayed STOP/SUSPEND from session A.
- [ ] Verify session B remains fully unchanged.
- [ ] Verify the stale event cannot clear session B's `audio_playing` state.
- [ ] Verify wrong-peer/stale-handle diagnostics are exact when a valid session-B binding exists.

### A2DP-FIX1-07.8 — Test quality requirements

- [ ] Every test must assert exact counter deltas rather than assuming lifetime counters start at zero.
- [ ] Every fixture must reset state through checked APIs and fail if reset fails.
- [ ] Do not weaken assertions to `>=` where exactly one event is required.
- [ ] Do not add sleeps to hide ordering bugs.
- [ ] Do not ignore sanitizer output.
- [ ] Ensure ASan and UBSan remain enabled for the focused host-test target.

**Acceptance condition:** The new tests fail against commit `0e08b5a...` and pass only after the production fixes are implemented.

---

## Task A2DP-FIX1-08 — Audit the bundled `sdkconfig` changes

### A2DP-FIX1-08.1 — Classify every configuration delta

- [ ] Review the baseline commit's `esp_bt_audio_source/sdkconfig` changes individually:
  - [ ] HFP AG enablement.
  - [ ] HFP client disablement.
  - [ ] HFP audio data path selection.
  - [ ] BR/EDR synchronous connection count.
  - [ ] HFP I2S port, pins, sample rate, DMA, task, timeout, and threshold values.
  - [ ] Removed PCM role/edge/frame-shape generated options.
- [ ] For each delta, classify it as:
  - [ ] Required for current full-duplex/HFP software compilation.
  - [ ] Required for planned hardware validation but not yet runtime-validated.
  - [ ] Automatically regenerated by ESP-IDF/Kconfig.
  - [ ] Accidental or stale and should be reverted.

### A2DP-FIX1-08.2 — Keep configuration work separate from lifecycle behavior

- [ ] Do not make additional unrelated `sdkconfig` changes in the A2DP runtime-safety commit.
- [ ] If any current delta is accidental, revert it in a dedicated configuration commit.
- [ ] If all deltas are required, add a short repository note or commit message documenting why they are present.
- [ ] Do not rewrite or force-push branch history solely to separate the already-existing commit unless the repository owner explicitly requests history rewriting.
- [ ] Confirm GPIO32, GPIO33, and GPIO27 remain marked as hardware-validation pending if they have not been tested on the target board.

### A2DP-FIX1-08.3 — Regenerate and validate deterministically

- [ ] Run the repository's normal ESP-IDF configuration/reconfigure path.
- [ ] Verify a clean reconfigure does not introduce unexplained churn.
- [ ] Verify the device build uses HCI SCO rather than accidentally switching to PCM.
- [ ] Verify HFP AG and one synchronous BR/EDR connection remain enabled if required by the current feature set.
- [ ] Record that configuration validation was compile-only and did not prove physical pin correctness.

**Acceptance condition:** Every `sdkconfig` delta has an explicit reason, and no hardware capability is claimed merely because compilation succeeds.

---

## Task A2DP-FIX1-09 — Run focused and complete validation

### A2DP-FIX1-09.1 — Focused host validation

- [ ] Build and run the A2DP event/identity test target with ASan and UBSan.
- [ ] Run the manager lifecycle/profile rollback tests.
- [ ] Run the full-duplex state and policy tests.
- [ ] Re-run the HFP audio-control test that previously exposed lifetime-counter isolation issues:

```bash
bash esp_bt_audio_source/tools/run_bt_hfp_audio_control_test.sh
```

- [ ] Re-run the HFP command test that previously exposed the missing UART mock symbol:

```bash
bash esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh
```

- [ ] Confirm no linker, ASan, UBSan, leak, or Unity failures.

### A2DP-FIX1-09.2 — Complete host suite

- [ ] Run the repository's complete optimized host CI command locally where possible.
- [ ] Run complete CTest with `--output-on-failure`.
- [ ] Run Python lint gates.
- [ ] Run Python unit tests.
- [ ] Save the exact command lines and pass counts in the final implementation summary.

### A2DP-FIX1-09.3 — Device compile-only validation

- [ ] Run the same ESP-IDF version used by CI.
- [ ] Build `esp_bt_audio_source` from a clean or correctly reconfigured build directory.
- [ ] Confirm HFP AG, HCI SCO, and synchronous connection configuration are reflected in the build.
- [ ] Record application size and partition headroom.
- [ ] Do not flash hardware.
- [ ] Do not claim runtime audio validation from a compile-only build.

### A2DP-FIX1-09.4 — GitHub Actions verification

- [ ] Push the implementation to `feature/esp-bt-audio-duplex`.
- [ ] Confirm both workflows pass:
  - [ ] `CI — host tests (optimized)`
  - [ ] `CI — device build (compile only)`
- [ ] Inspect the actual failed-step logs if either workflow fails; do not rely only on the green/red summary icon.
- [ ] Record the final commit SHA and workflow run IDs.

**Acceptance condition:** Both complete CI workflows pass on the final commit, and all focused regression tests are present in the repository.

---

## Task A2DP-FIX1-10 — Documentation and closeout

- [ ] Update function comments for the checked reset API.
- [ ] Add a concise code comment explaining why unbound terminal events return `ESP_ERR_NOT_FOUND` without state mutation.
- [ ] Add a concise code comment explaining why the binding is preserved when callback shutdown is unconfirmed.
- [ ] Do not add comments that claim a race is safe without stating the identity/lifecycle condition that makes it safe.
- [ ] Produce a final implementation summary containing:
  - [ ] Files changed.
  - [ ] Behavioral changes.
  - [ ] Exact test commands.
  - [ ] Test counts/results.
  - [ ] CI run IDs.
  - [ ] Device image size/headroom.
  - [ ] Remaining hardware-validation boundaries.
- [ ] Mark this TODO complete only after all required tests and both workflows pass.

---

## 5. Suggested file-level changes

The exact test filenames may vary based on the existing target layout, but production changes should remain concentrated in:

- `esp_bt_audio_source/components/bt_manager/bt_events_a2dp.c`
- `esp_bt_audio_source/components/bt_manager/include/bt_events_a2dp.h`
- `esp_bt_audio_source/components/bt_manager/bt_manager.c`
- Existing A2DP event/identity host tests
- Existing Bluetooth manager lifecycle/rollback host tests
- Existing test mocks for mutex failure and callback-shutdown failure injection
- `esp_bt_audio_source/sdkconfig` only if the configuration audit requires a dedicated correction

Do not create duplicate production implementations or parallel binding state.

---

## 6. Recommended commit breakdown

Use small, reviewable commits on the existing branch:

1. `fix(bt): make A2DP binding reset fail closed`
   - Checked reset API
   - No unlocked fallback
   - Init/deinit/rollback ordering

2. `fix(bt): ignore unbound terminal A2DP events safely`
   - No state mutation
   - No callbacks/policy delivery
   - Explicit ignored-event diagnostics

3. `test(bt): cover A2DP binding lifecycle and late events`
   - Lock failure
   - Clean reinit
   - Failed teardown preservation
   - Late terminal and cross-session stale events

4. Optional only if needed: `config(bt): document or correct HFP build settings`
   - Configuration-only changes
   - No runtime lifecycle changes

Do not squash unrelated configuration, runtime, and test work into one opaque commit unless repository policy requires squashing.

---

## 7. Prohibited shortcuts

The following approaches are explicitly unacceptable:

- [ ] Clearing `s_policy_binding` after a mutex lock failure.
- [ ] Adding a `force_reset` function that writes without synchronization.
- [ ] Calling reset before the mutex exists.
- [ ] Clearing callback-owned identity after Bluedroid shutdown failure.
- [ ] Returning `ESP_OK` for an event that was not identity-bound.
- [ ] Calling downstream callbacks for an ignored unbound event.
- [ ] Silently changing peer/generation/handle checks to make ordering tests pass.
- [ ] Resetting counters in production initialization solely to satisfy absolute-value test assertions.
- [ ] Weakening exact test assertions to ranges.
- [ ] Adding sleeps, retries, or ignored errors to stabilize tests.
- [ ] Treating compile success as hardware validation.
- [ ] Flashing the ESP32-S3 without explicit authorization.
- [ ] Creating a new branch or pull request without explicit authorization.

---

## 8. Final definition of done

This FIX1 is complete only when all boxes below are checked:

- [ ] Binding reset returns `esp_err_t` and never writes after lock failure.
- [ ] Init creates the mutex before reset and aborts transactionally on reset failure.
- [ ] Clean deinit resets the binding before mutex deletion.
- [ ] Failed/unconfirmed callback shutdown preserves the binding and quarantines the manager.
- [ ] Init rollback follows the same callback-safety rule.
- [ ] Unbound START is rejected fail-closed.
- [ ] Unbound STOP/SUSPEND is ignored without state mutation, callback delivery, or policy delivery.
- [ ] Ignored late terminal events have distinct bounded diagnostics.
- [ ] Clean reinit, lock failure, failed teardown, late terminal, and cross-session stale-event tests exist and pass.
- [ ] The tests execute the real production implementation under ASan/UBSan.
- [ ] All existing HFP audio-control and HFP command tests still pass.
- [ ] Complete host CI passes.
- [ ] Device compile-only CI passes.
- [ ] `sdkconfig` changes are classified and documented or corrected.
- [ ] No silent fallback, fake success, or unsafe best-effort cleanup remains.
- [ ] Final implementation commit SHA and workflow run IDs are recorded.
