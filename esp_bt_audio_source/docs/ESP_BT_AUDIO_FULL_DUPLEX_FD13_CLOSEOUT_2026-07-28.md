# ESP32 Bluetooth Audio Full-Duplex — FD-13 Closeout

**Branch:** `feature/esp-bt-audio-duplex`  
**Draft PR:** #2  
**Validated implementation head:** `5d656a55bb2e09c7924996958a60757a31783113`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Hardware flashing:** Not performed

## 1. Phase result

FD-13 is software-complete for the memory, timing, and stack diagnostics defined in `ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`.

The public HFP manager now exposes one bounded diagnostic snapshot containing:

- current free internal heap;
- process-lifetime minimum free heap;
- current largest free internal heap block;
- process-lifetime minimum free stack observed by `BtAppTask`;
- process-lifetime minimum free stack observed by the HFP I2S writer task;
- configured incoming-callback budget;
- most recent incoming-callback duration;
- process-lifetime maximum incoming-callback duration;
- process-lifetime incoming-callback over-budget count.

The command layer exposes the snapshot through stable `HFP STATS` records. Unsupported or not-yet-observed metrics are reported explicitly as unavailable. They are never represented as a plausible zero measurement.

## 2. Public diagnostic API

FD-13 adds this manager-owned API:

```c
esp_err_t bt_manager_hfp_get_diagnostics(
    bt_hfp_manager_diagnostics_t *out);
```

The command component obtains diagnostics only through this public API. It does not include ESP HFP, heap-capability, FreeRTOS task, or I2S implementation headers.

The output is committed only after every required source has either:

1. returned a valid value; or
2. returned an explicitly supported unavailable result.

Unexpected errors are returned exactly and leave the caller's output structure unchanged.

Before collecting resource metrics, the API verifies that the HFP manager can provide an authoritative status snapshot. This prevents a command from reporting resource data as an HFP diagnostic when the manager itself is unavailable.

## 3. Stable `HFP STATS` records

FD-13 appends these records to the existing multi-line `HFP STATS` response:

```text
INFO|HFP|STATS_RESOURCE1|HEAP_STATE=<AVAILABLE|UNAVAILABLE>,FREE_INTERNAL_BYTES=<n|NA>,MIN_FREE_HEAP_BYTES_LIFETIME=<n|NA>,LARGEST_INTERNAL_BLOCK_BYTES=<n|NA>
INFO|HFP|STATS_RESOURCE2|HFP_APP_TASK_STATE=<AVAILABLE|UNAVAILABLE>,HFP_APP_MIN_FREE_STACK_BYTES_LIFETIME=<n|NA>,I2S_WRITER_TASK_STATE=<AVAILABLE|UNAVAILABLE>,I2S_WRITER_MIN_FREE_STACK_BYTES_LIFETIME=<n|NA>
INFO|HFP|STATS_CALLBACK|STATE=<AVAILABLE|UNAVAILABLE>,BUDGET_US=<n|NA>,LAST_US=<n|NA>,MAX_US_LIFETIME=<n|NA>,OVER_BUDGET_LIFETIME=<n|NA>
```

The existing `HFP STATS` records remain unchanged. This avoids silently changing the meaning or field order of the FD-11 wire contract.

A successful response still ends with:

```text
OK|HFP|STATS|
```

## 4. No partial-success response

`HFP STATS` obtains both the regular statistics snapshot and the FD-13 diagnostic snapshot before emitting the first `INFO` line.

If either snapshot fails unexpectedly:

- the exact `esp_err_t` name is returned;
- no partial `STATS_*` lines are emitted;
- no final `OK|HFP|STATS|` record is emitted.

This avoids a dangerous mixed response in which old counter lines appear successful while the new resource diagnostics silently failed.

Expected unavailable conditions remain a successful diagnostic snapshot and are represented with explicit state fields plus `NA` values.

## 5. Heap diagnostics

Production heap values use the ESP-IDF v5.5.1 APIs required by the FD-13 plan:

```c
heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
esp_get_minimum_free_heap_size()
heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
```

The fields mean:

- `FREE_INTERNAL_BYTES`: current total free internal-capable heap;
- `MIN_FREE_HEAP_BYTES_LIFETIME`: ESP-IDF's process-lifetime minimum free heap;
- `LARGEST_INTERNAL_BLOCK_BYTES`: current largest single allocatable internal block.

The three calls are made sequentially during one command operation. They are not presented as a scheduler-frozen transactional heap snapshot. A concurrent allocation may therefore occur between calls, which is normal for live heap telemetry.

## 6. Task stack high-water marks

ESP-IDF's `uxTaskGetStackHighWaterMark()` reports the minimum amount of free stack that has remained since task creation. In this ESP-IDF configuration, the value is already expressed in bytes.

FD-13 therefore reports minimum free stack bytes, not:

- current free stack;
- stack bytes used;
- words requiring multiplication by `sizeof(StackType_t)`.

The task fields are named `MIN_FREE_STACK_BYTES_LIFETIME` to make this meaning explicit.

### 6.1 `BtAppTask`

`BtAppTask` samples its own high-water mark:

- when the task starts;
- after each event-loop iteration.

The minimum observed value is retained in process-lifetime atomic storage. It survives task shutdown/restart and is not reset by `HFP RESETSTATS`.

`BtAppTask` is the shared Bluetooth application-dispatch task. Its high-water mark therefore reflects all work dispatched through that task, including HFP work; it is not falsely labeled as an HFP-callback-only stack measurement.

### 6.2 HFP I2S writer task

The `hfp_i2s_tx` writer samples its own high-water mark:

- when the task starts;
- after the start gate is released;
- after every writer iteration;
- immediately before task exit.

The minimum observed value is retained outside the transient writer-task context. It survives I2S writer stop/start cycles and is not reset by `HFP RESETSTATS`.

### 6.3 Safe task ownership

Each task samples itself rather than allowing an unrelated command task to query a handle that may be concurrently deleted.

This avoids a task-handle lifetime race. Until a task has executed its first sample, the corresponding metric is reported as:

```text
STATE=UNAVAILABLE
..._MIN_FREE_STACK_BYTES_LIFETIME=NA
```

It is not reported as zero.

## 7. Callback timing diagnostics

The incoming HFP audio callback retains the established budget:

```text
BUDGET_US=2000
```

FD-13 exposes:

- `LAST_US`: duration of the most recently measured callback in the current HFP audio-component lifecycle;
- `MAX_US_LIFETIME`: greatest measured duration during the process lifetime;
- `OVER_BUDGET_LIFETIME`: number of measured callbacks longer than the configured budget during the process lifetime.

The final audit found that the pre-FD-13 fields named as lifetime values were stored inside the transient HFP audio context and were erased by verified profile teardown. FD-13 moves the maximum and over-budget count into process-lifetime storage.

HFP profile teardown and reinitialization may reset current/session fields such as `LAST_US`, but it cannot reduce `MAX_US_LIFETIME` or clear `OVER_BUDGET_LIFETIME`.

No allocation was added to the callback path. The existing focused test runner continues to reject reachable `malloc`, `calloc`, `realloc`, `heap_caps_malloc`, or `heap_caps_calloc` symbols from `bt_hfp_audio.o`.

## 8. Reset semantics

`HFP RESETSTATS` remains a non-destructive reporting-baseline operation.

It does not reset:

- ESP-IDF minimum-ever free heap;
- current free heap or largest-block gauges;
- retained `BtAppTask` minimum free stack;
- retained I2S writer minimum free stack;
- callback maximum duration lifetime value;
- callback over-budget lifetime count;
- ring peak-used lifetime value;
- authoritative process-lifetime health-transition count.

The existing reset-baseline-relative counters remain relative to the most recent accepted reset. The FD-13 wire fields explicitly containing `LIFETIME` are raw historical values and are not baseline-subtracted.

No implicit reset occurs during `HFP STATS`, profile stop, I2S writer stop, or ordinary HFP profile reinitialization.

## 9. Availability and error policy

Expected unavailable results are limited to:

- `ESP_ERR_NOT_FOUND`;
- `ESP_ERR_INVALID_STATE`;
- `ESP_ERR_NOT_SUPPORTED`.

For those cases, the corresponding availability flag is false and the command record uses `NA`.

Other errors, including timeout or generic failure, are propagated exactly. FD-13 does not convert an unexpected diagnostic failure into an unavailable metric because that would hide a real malfunction.

Host builds without ESP platform support intentionally report live heap and task metrics as unavailable unless the focused test fixture injects platform values.

## 10. Tests

### 10.1 FD-13 diagnostic sanitizer suite

The new focused ASan/UBSan suite contains 7 cases covering:

1. null output rejection;
2. exact full diagnostic snapshot;
3. explicit supported-unavailable sources;
4. authoritative manager-status failure with no output commit;
5. exact HFP task-stack failure with no output commit;
6. exact I2S task-stack failure with no output commit;
7. exact callback-snapshot failure with no output commit.

### 10.2 HFP command sanitizer suite

The focused HFP command suite now contains 20 cases. FD-13 additions verify:

1. exact available heap, stack, and callback records;
2. unavailable values use `NA` and never fake zero;
3. an unexpected diagnostic error produces the exact error and no partial `STATS` response.

### 10.3 Callback lifetime regression

The focused incoming-audio suite now contains 14 cases.

The added teardown regression records an over-budget callback, destroys the HFP audio component, recreates it, records a shorter callback, and verifies that:

- `LAST_US` reflects the new callback;
- `MAX_US_LIFETIME` retains the prior maximum;
- `OVER_BUDGET_LIFETIME` retains the prior count.

### 10.4 Regression integration

The strict host workflow runs:

- the generic host CMake build;
- all prior full-duplex focused ASan/UBSan suites;
- the HFP incoming-audio sanitizer suite with allocation-symbol gate;
- the FD-13 diagnostic sanitizer suite;
- the expanded HFP command suite;
- changed-Python lint;
- Python unit tests;
- the complete CTest suite.

## 11. Validation

Validation for implementation head `5d656a55bb2e09c7924996958a60757a31783113`:

- Strict host CI run **1063**: PASS.
  - all focused ASan/UBSan suites passed;
  - FD-13 diagnostic suite: **7/7 cases passed**;
  - HFP incoming-audio suite: **14/14 cases passed**;
  - HFP command suite: **20/20 cases passed**;
  - callback allocation-symbol gate passed;
  - changed-Python lint gate passed;
  - Python unit tests passed;
  - complete CTest: **74/74 targets passed**, 0 failed.
- ESP-IDF v5.5.1 device-build run **954**: PASS.
- Application image: **1,019,520 bytes**.
- Factory app partition: **1,769,472 bytes**.
- Factory app-partition headroom: **749,952 bytes**.
- Image delta from FD-12: **+1,520 bytes**.
- `.dram0.data`: **21,856 bytes**.
- `.dram0.bss`: **52,752 bytes**.
- Static `.dram0.data + .dram0.bss`: **74,608 bytes**.
- Static DRAM delta from FD-12: **0 bytes**.
- No hardware was flashed.

The factory partition retains substantially more than the project's 256 KiB minimum headroom gate.

## 12. Explicit validation boundary

FD-13 adds and software-validates the instrumentation and wire contract. It does not claim real runtime values from the target board.

The following remain hardware-gated:

- actual free/minimum/largest heap values during A2DP/HFP operation;
- actual `BtAppTask` and I2S writer stack high-water marks;
- actual callback last/maximum durations and over-budget counts;
- ten-minute A2DP baseline measurements from FD-01;
- behavior under real HFP SLC/SCO callback cadence;
- correlation of resource diagnostics with physical I2S0 output.

No placeholder hardware numbers are recorded. A metric that has not yet been observed on the device remains unavailable rather than being synthesized.

## 13. Next phase

FD-14 validates GPIO32/GPIO33/GPIO27 I2S0 output on physical hardware.

FD-14 is a hardware gate. It requires explicit user approval before any firmware is flashed or physical acceptance work begins.
