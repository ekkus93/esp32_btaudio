# ESP32 Bluetooth Audio — Duplex Stabilization Review Fixes Closeout

**Repository:** `ekkus93/esp32_btaudio`  
**Branch:** `feature/esp-bt-audio-duplex`  
**Review-fixes TODO:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_REVIEW_FIXES_TODO_2026-07-30.md`  
**Production/test implementation commit:** `857ff667225602d2e24f5b96acdf11bf02e12d1b`  
**Host-CI enforcement commit:** `cd0175cb95565c5cfb4a99a78f826bfebb33ce87`  
**Validated same-SHA checkpoint:** `15a7bbdafa104f0fd7d7d13a42d92eca2bd68938`  
**Temporary workflow cleanup commit:** `41db360edcd997230c6551ad0cafe52ad832c0ca`  
**Physical hardware validation:** Pending  
**Hardware flashed during this work:** No

---

## 1. Scope completed

This closeout records the software fixes identified by the post-stabilization code review.

Implemented changes:

1. The maintained host workflow now enforces `tools/run_bt_hfp_commands_test.sh` as a required sanitizer gate.
2. Nested focused-runner logs under `build_host_tests/**` are retained in host evidence artifacts.
3. Failures from `bt_duplex_record_stale_operation_event()` are no longer silently discarded.
4. Unbound-event HFP status lookup failures are logged with the peer, rejection reason, and exact error.
5. The original A2DP identity or policy rejection remains authoritative when diagnostic telemetry also fails.
6. `audio_processor_read()` failures in the A2DP source data callback remain zero-byte callback results but now update nonblocking, saturating diagnostics and emit bounded logs.
7. Disconnect error precedence is explicit: `ESP_ERR_NOT_FOUND` is treated as an idempotent no-duplex-session result, an actionable clear failure may replace it, and a hard primary policy error remains authoritative.
8. The original stabilization TODO now points readers to both its software closeout and this review-fix follow-up.
9. The original software closeout now contains the review-follow-up result and exact validation evidence.
10. All temporary patch scripts and one-shot workflows used during implementation have been removed.

No fallback mutation, unlocked reset, fake success, blocking manager mutex, heap allocation, retry loop, or hardware claim was added.

---

## 2. Exact regression coverage

### 2.1 HFP command regression gate

The newly enforced command runner completed:

```text
24 Tests
0 Failures
0 Ignored
```

It covers parser forms, status snapshot integrity, FD-16 policy visibility, unavailable metrics, accepted-versus-completed command semantics, exact backend errors, audio start/stop confirmation, mode parsing, codec generation, diagnostics, overflow handling, transport failures, and health visibility.

### 2.2 A2DP secondary-failure and precedence suite

`test_a2dp_secondary_failures_exact` completed:

```text
12 Tests
0 Failures
0 Ignored
```

The suite now proves:

- generation-diagnostic lock failure preserves the primary error;
- binding-clear lock failure is exact and preserves binding;
- late unbound terminals do not refresh generation or invoke policy/callbacks;
- unbound START remains an exact `ESP_ERR_INVALID_STATE` rejection;
- wrong-peer and stale-handle telemetry recorder failures are visible without state mutation;
- unbound stale-telemetry failure preserves the primary identity rejection;
- HFP status lookup failure on the diagnostic path is visible;
- successful clear plus `ESP_ERR_NOT_FOUND` remains idempotent;
- a clear failure replaces only `ESP_OK` or the idempotent `ESP_ERR_NOT_FOUND` result;
- a hard primary policy error is not replaced by a clear failure.

### 2.3 A2DP data-callback diagnostics

`test_a2dp_data_callback_diagnostics` completed:

```text
4 Tests
0 Failures
0 Ignored
```

The suite proves:

- successful reads return the produced byte count without incrementing failures;
- an exact `audio_processor_read()` error returns zero bytes and becomes observable;
- repeated errors increment a saturating counter while logs remain bounded to the first error and periodic intervals;
- invalid callback requests do not call the audio processor or contaminate read-failure diagnostics.

---

## 3. Same-SHA validation checkpoint

The maintained workflows passed together on:

```text
15a7bbdafa104f0fd7d7d13a42d92eca2bd68938
```

Host evidence:

```text
Workflow: CI — host tests (optimized)
Run ID:   30571344895
Job ID:   90968575645
Result:   SUCCESS
CTest:    81/81 passed, 0 failed
Time:     40.10 seconds
Artifact: 8770952601 (host-test-results)
```

Device compile-only evidence:

```text
Workflow:             CI — device build (compile only)
Run ID:               30571345110
Job ID:               90968576182
Result:               SUCCESS
ESP-IDF:              v5.5.1
Target:               esp32 / ESP32-WROOM-32 configuration
Application binary:   0xFB070 = 1,028,208 bytes
Smallest partition:   0x1B0000 = 1,769,472 bytes
Partition headroom:   0xB4F90 = 741,264 bytes (42% free)
Flash executed:       No
```

Successful enforced host stages included:

- clean full host configure and build;
- all maintained HFP and duplex sanitizer runners;
- the newly enforced 24-test HFP command runner;
- the expanded A2DP binding lifecycle runner under ASan and UBSan;
- Python changed-file lint gate;
- Python unit tests;
- complete CTest;
- evidence artifact upload.

The full-tree Python lint audit remains informational legacy debt. This work does not claim that the repository is globally flake8-clean.

---

## 4. Callback diagnostic design boundary

The A2DP data callback cannot propagate `esp_err_t` through the ESP-IDF callback signature. Returning zero bytes therefore remains the stack-facing failure behavior.

The added diagnostics are deliberately local to the callback path:

- no manager mutex acquisition;
- no dynamic allocation;
- no sleep or retry;
- saturating failure count;
- latest exact error;
- cumulative count of suppressed repetitive logs;
- logging only on the first failure and every 64th failure.

Production writes occur from the serial A2DP data-callback context. UNIT_TEST snapshots are taken only after synchronous callback return.

---

## 5. Final candidate validation rule

This file update is the final ordinary repository change for the review-fix Ralph loop. Its resulting commit SHA must satisfy all of the following before external closeout:

- issue #4 reports host and device runs on that exact same SHA;
- host result is `success` with all enforced stages complete;
- complete CTest remains 81/81 with zero failures;
- device compile-only result is `success` under ESP-IDF v5.5.1;
- expected firmware artifacts and size evidence are present;
- problem steps are `None`;
- no temporary one-shot script or workflow is present in the final diff;
- no generated build output or firmware artifact is committed;
- no hardware flash is executed or implied.

A Git document cannot embed the SHA of the commit that contains itself. The exact final literal SHA, final host run/job, and final device run/job therefore belong in issue #4 and in the external Ralph-loop completion report.

Physical ESP32-WROOM-32 tasks remain pending and must not be represented as passed. FD-18/FD-19 HFP speaker downlink/full-playback work, broad CMake modularization, and the Python cleanup backlog remain outside this TODO.
