# ESP Bluetooth Audio Full-Duplex FD-16 Closeout

Date: 2026-07-28  
Branch: `feature/esp-bt-audio-duplex`  
Validated implementation head: `c2aa0269cccf19911d3d8ce638fc6ac2e5d8a125`  
Status: **Software implementation complete**

## 1. Scope completed

FD-16 adds an explicit, host-testable duplex policy engine and binds it to the authoritative A2DP/HFP state machine.

The phase implements these required policies:

- `DISABLED` preserves ordinary A2DP behavior and assigns playback ownership to A2DP.
- `A2DP_MIC` requires an active A2DP stream, HFP SLC, and SCO microphone path.
- `A2DP_MIC` reports an explicit incompatibility when the remote device suspends or stops A2DP during SCO. It does not silently change modes.
- `AUTO` starts with the preferred A2DP-playback plus HFP-microphone policy.
- `AUTO` explicitly changes its effective policy to `HFP_FULL` when A2DP is suspended or stopped during SCO.
- `AUTO` returns to the preferred A2DP-microphone policy when A2DP resumes or SCO stops.
- `HFP_FULL` reserves the HFP downlink owner, but it does not claim that HFP playback is operational before FD-18 and FD-19 exist.
- Every policy result assigns exactly one downlink owner. A2DP and HFP can never simultaneously own the playback downlink.

## 2. Pure policy evaluator

The allocation-free evaluator is implemented in:

- `components/bt_manager/bt_duplex_policy.c`
- `components/bt_manager/include/bt_duplex_policy.h`

Inputs include:

- requested mode
- current effective mode
- A2DP profile state
- A2DP streaming state
- HFP SLC state
- SCO state
- whether A2DP was interrupted during SCO
- the exact interruption reason

Outputs include:

- effective mode
- policy state
- exact policy reason
- exclusive downlink owner
- whether an HFP downlink is requested

The evaluator performs no allocation and has no platform side effects.

## 3. Explicit policy states and reasons

Stable policy states are:

- `SATISFIED`
- `WAITING`
- `INCOMPATIBLE`
- `COMPATIBILITY_REQUIRED`

Stable reasons include:

- `REQUESTED_MODE`
- `WAITING_A2DP_CONNECTION`
- `WAITING_A2DP_STREAM`
- `WAITING_HFP_SLC`
- `WAITING_SCO`
- `REMOTE_SUSPENDED_A2DP_DURING_SCO`
- `A2DP_STOPPED_DURING_SCO`
- `A2DP_RESUMED`
- `SCO_STOPPED`
- `HFP_DOWNLINK_NOT_IMPLEMENTED`

`HFP_DOWNLINK_NOT_IMPLEMENTED` is intentional. Selecting or requesting `HFP_FULL` reserves HFP ownership, but the policy remains `COMPATIBILITY_REQUIRED` until the playback voice tap and outgoing HFP callback are implemented in FD-18 and FD-19. This prevents fake operational success.

## 4. Serialized manager integration

The manager adapter is implemented in:

- `components/bt_manager/bt_hfp_manager_fd16.c`

Policy application is serialized through the existing non-recursive Bluetooth manager lock.

The adapter:

- binds events to the authoritative peer and session generation
- rejects stale generations and wrong peers
- treats expected duplicate events as idempotent
- records process-lifetime evaluations, effective-mode changes, incompatibilities, and A2DP interruptions
- emits no duplicate mode-change event for duplicate input
- preserves lifetime counters across session generations
- reads the configured mode while holding the same manager lock used to create the authoritative session
- never evaluates policy from a read-only getter

A2DP callbacks capture the expected active generation before entering the serialized policy handler. If the active generation changes between callback capture and handling, the update fails closed with `ESP_ERR_INVALID_STATE`.

### Residual ESP-IDF callback limitation

ESP-IDF A2DP callbacks provide a peer address but no independent connection-instance token. Generation capture prevents callbacks racing a session replacement from mutating the replacement session. However, a callback that is already delivered only after a completely new session with the same peer has begun cannot be cryptographically identified as belonging to the old link. This limitation is documented rather than hidden.

## 5. A2DP and HFP event integration

Production A2DP profile and audio events now enter the policy adapter from:

- `components/bt_manager/bt_events_a2dp.c`

HFP profile and SCO transitions enter the same policy path from:

- `components/bt_manager/bt_hfp_ag_events.c`

Unexpected policy errors are logged visibly. They are not converted into success and are not silently ignored.

## 6. Safe default

The configured duplex mode now resets to:

```text
DISABLED
```

This is the safe boot/runtime default. Duplex behavior must be selected explicitly rather than being enabled by an implicit `AUTO` fallback.

## 7. Stable policy event

FD-16 adds this event record:

```text
EVENT|HFP|POLICY|<STATE>|<REASON>|<REQUESTED>|<EFFECTIVE>|<DOWNLINK_OWNER>|<GEN>
```

Examples:

```text
EVENT|HFP|POLICY|INCOMPATIBLE|REMOTE_SUSPENDED_A2DP_DURING_SCO|A2DP_MIC|A2DP_MIC|A2DP|7
EVENT|HFP|POLICY|COMPATIBILITY_REQUIRED|REMOTE_SUSPENDED_A2DP_DURING_SCO|AUTO|HFP_FULL|HFP|7
EVENT|HFP|POLICY|COMPATIBILITY_REQUIRED|HFP_DOWNLINK_NOT_IMPLEMENTED|HFP_FULL|HFP_FULL|HFP|7
```

An effective-mode transition also emits the existing generation-bound `MODE` event with the exact policy reason.

Event delivery failure remains visible through the existing event-delivery failure accounting.

## 8. `HFP STATUS` visibility

`HFP STATUS` now emits a policy record before its final success record:

```text
INFO|HFP|STATUS_POLICY|STATE=<state>,REASON=<reason>,REQUESTED=<mode>,EFFECTIVE=<mode>,DOWNLINK_OWNER=<A2DP|HFP>,HFP_DOWNLINK_REQUESTED=<0|1>,GEN=<generation>
```

When no committed policy decision exists, the record reports explicit unavailable values rather than plausible zeros:

```text
INFO|HFP|STATUS_POLICY|STATE=UNAVAILABLE,REASON=NONE,REQUESTED=NONE,EFFECTIVE=NONE,DOWNLINK_OWNER=NONE,HFP_DOWNLINK_REQUESTED=0,GEN=0
```

If the policy status record cannot be emitted, the command does not emit a final `OK|HFP|STATUS` line.

## 9. Host test coverage

FD-16 adds three focused ASan/UBSan binaries:

1. Real policy evaluator, authoritative state, event, and manager-adapter behavior.
2. Missing-capability safety behavior for `HFP_FULL`.
3. All six prerequisite-arrival orderings for the AUTO policy.

Covered behavior includes:

- invalid arguments
- `DISABLED` behavior
- all waiting prerequisites
- full policy state matrix
- exactly one downlink owner for every matrix permutation
- strict `A2DP_MIC` incompatibility without mode change
- AUTO remote suspend and stopped fallback
- A2DP resume recovery
- SCO-stop recovery
- duplicate event suppression
- stale HFP generation rejection
- stale A2DP profile generation rejection
- stale A2DP audio generation rejection
- configured-mode/session creation serialization
- no recursive manager lock
- exact `MODE` and `POLICY` wire records
- `HFP_FULL` never claiming an unimplemented downlink
- all prerequisite ordering permutations converging to the same safe AUTO result

## 10. Final validation

Validated implementation head:

```text
c2aa0269cccf19911d3d8ce638fc6ac2e5d8a125
```

### Host CI

GitHub Actions run:

```text
CI — host tests (optimized) #1169
Run ID: 30398413012
Result: PASS
```

Results include:

- FD-16 real adapter/policy suite: **10/10 passed**
- FD-16 missing-capability suite: **1/1 passed**
- FD-16 ordering suite: **1/1 passed**
- HFP command suite: **22/22 passed**
- all earlier focused ASan/UBSan full-duplex suites passed
- Python lint gate passed
- Python unit tests passed
- complete CTest: **74/74 passed**, 0 failed

### ESP-IDF device compile-only validation

GitHub Actions run:

```text
CI — device build (compile only) #1059
Run ID: 30398412963
ESP-IDF: v5.5.1
Result: PASS
```

Firmware metrics:

- application image: **1,024,032 bytes** (`0xFA020`)
- factory application partition: **1,769,472 bytes** (`0x1B0000`)
- remaining application-partition headroom: **745,440 bytes** (`0xB5FE0`), **42% free**

No firmware was flashed.

## 11. Hardware boundary

FD-16 is software-complete, but the following hardware phases remain deferred and are not marked complete:

- FD-14: GPIO32/GPIO33/GPIO27 I2S0 output validation
- FD-15: real HFP SLC and SCO validation with earbuds
- FD-17: simultaneous A2DP playback and CVSD microphone validation

Therefore, FD-16 does not prove that a specific earbud model preserves A2DP during SCO. It guarantees that either outcome is represented explicitly and safely by policy state.

## 12. Next software phase

With FD-14, FD-15, and FD-17 deferred, the next software phase is:

```text
FD-18 — Add playback voice tap
```

FD-18 must derive HFP downlink PCM from the already-produced authoritative playback stream. It must not create a second source consumer or claim the HFP compatibility path is operational until FD-19 also implements the outgoing callback.
