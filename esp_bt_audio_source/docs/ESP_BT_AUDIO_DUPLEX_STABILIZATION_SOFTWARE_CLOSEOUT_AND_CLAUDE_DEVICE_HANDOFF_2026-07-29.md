# ESP32 Bluetooth Audio — Duplex Stabilization Software Closeout and Claude Device Handoff

**Repository:** `ekkus93/esp32_btaudio`  
**Branch:** `feature/esp-bt-audio-duplex`  
**Project:** `esp_bt_audio_source/`  
**Software implementation SHA validated before this documentation commit:** `77da0d122fb2673836db719cfbc5904122c259ac`  
**ESP-IDF:** v5.5.1  
**Configured target:** ESP32 / ESP32-WROOM-32  
**Software stabilization result:** Complete  
**Physical hardware validation:** Pending  
**Hardware flashed during this software closeout:** No

---

## 1. Purpose and authority

This file closes the software-only portion of:

- `esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md`
- `docs/ESP_BT_AUDIO_A2DP_BINDING_LIFECYCLE_SAFETY_FIX1_TODO.md`

The detailed TODO files remain the behavioral specifications. This closeout records what was actually implemented and validated, identifies the evidence, and gives Claude Code a bounded physical-device procedure.

The exact hardware candidate is the final branch SHA reported by ChatGPT after both maintained workflows pass on the documentation head. Claude Code must use that literal SHA, not merely whatever the branch happens to point to later.

---

## 2. Software result

The feature branch is now software-stable for the reviewed FIX1 scope:

- The complete host CMake graph clean-configures and builds.
- The retired fake pairing-adapter target no longer references deleted sources.
- The broad host CMake modularization was deliberately deferred.
- A2DP binding reset remains synchronized and fail-closed.
- Initialization rollback and teardown preserve callback-owned state when shutdown cannot be confirmed.
- Incomplete teardown quarantines the manager rather than fabricating cleanup.
- The original initialization error remains authoritative when rollback also fails.
- Generation-diagnostic counter lock failure is visible without replacing the primary generation error.
- Disconnect binding-clear lock failure is visible and cannot trigger an unlocked clear.
- Unbound `STARTED` remains an exact `ESP_ERR_INVALID_STATE` failure.
- Late unbound `STOPPED` and `REMOTE_SUSPEND` remain observable no-ops.
- Late terminal events do not invoke the audio callback, do not invoke duplex policy, do not refresh generation, and do not mutate playback state.
- Wrong-peer, stale-handle, and cross-session events cannot alter the active session.
- No unsafe force-reset, no-lock reset, best-effort binding mutation, fake success, or quiet CMake target skip was introduced.

---

## 3. Production changes

### 3.1 Manager rollback finalization

`components/bt_manager/bt_manager.c` now uses one production init-rollback finalizer for the callback-owned portion of cleanup.

The finalizer:

1. Returns the original initialization error.
2. Preserves binding, mutex, and callback-owned state when callback shutdown is unconfirmed.
3. Quarantines the manager when cleanup is incomplete.
4. Resets the binding only while the manager mutex still exists.
5. Deletes the mutex only after the guarded reset succeeds.
6. Preserves the original init error when binding reset also fails.
7. Keeps the mutex and binding intact after reset-lock failure.

The quarantine state is intentionally irreversible within the process. Tests use separate process invocations rather than adding an unsafe test-only quarantine reset.

### 3.2 Delayed lock-failure injection

The UNIT_TEST manager hook can fail a later exact `bt_ctx_lock()` boundary after a specified number of successful locks. It remains one-shot and does not add caller-specific production branches.

This permits deterministic coverage of:

- the generation-failure diagnostic update;
- the disconnect binding-clear operation;
- rollback reset after callback shutdown.

No sleeps or timing-dependent races are used.

### 3.3 Secondary A2DP failures

`components/bt_manager/bt_events_a2dp.c` no longer silently returns from either remaining helper lock failure.

Generation diagnostic behavior:

- returns the primary HFP/generation error;
- attempts the guarded diagnostic counter update;
- logs lifecycle serial, connection handle, primary error, and secondary lock error if that update fails;
- never increments the counter without the manager lock.

Binding clear behavior:

- returns `ESP_OK` when the matching identity is cleared;
- returns `ESP_ERR_NOT_FOUND` when the expected session no longer owns the binding;
- returns the exact lock error when the binding cannot be inspected or cleared;
- preserves the binding on lock failure;
- logs the clear outcome from the disconnect policy path.

### 3.4 Late terminal contract

A legacy Bluetooth integration test still expected a post-disconnect STOP event to reach `bt_audio_state_cb()`. That expectation contradicted FIX1.

The registered integration case now asserts the actual safety contract:

- authoritative disconnect clears playback and binding;
- the later STOP does not reach the audio callback;
- playback remains stopped;
- `late_terminal_events_ignored` increases by exactly one;
- hard rejection and generation-failure counters do not change;
- no autostart occurs.

---

## 4. Host CMake repair

The complete host registry is:

```text
esp_bt_audio_source/test/host_test/CMakeLists.txt
```

The stale `test_pairing_adapter_runner` block was removed because it referenced deliberately retired files:

```text
esp_bt_audio_source/test_bluetooth/main/test_pairing_commands.c
esp_bt_audio_source/test_bluetooth/main/test_pairing_adapters.c
```

The repair did **not**:

- restore fake implementations;
- create placeholder files;
- add `if(EXISTS)` target omission;
- create an empty test under the old name;
- perform broad CMake modularization.

The complete graph now registers the exact A2DP diagnostics, cross-session, secondary-failure, and four process-isolated rollback cases with CTest.

---

## 5. New and extended regression coverage

### 5.1 `test_a2dp_secondary_failures_exact.c`

Five exact cases cover:

1. Generation diagnostic lock failure preserves the primary operation error.
2. The secondary lock error is separately observable.
3. Binding clear returns the exact lock error and leaves every binding field unchanged.
4. The full disconnect path exposes binding-clear failure and preserves the binding.
5. Late terminal events perform zero HFP generation/status refresh and zero downstream audio-policy delivery.
6. The test helper exposes exact unbound START `ESP_ERR_INVALID_STATE` behavior.

The file contains five Unity test functions; the generation and unbound-start assertions include multiple contract points within those cases.

### 5.2 `test_bt_manager_init_rollback.c`

Four process-isolated cases cover:

```text
complete
callbacks-live
reset-fails
cleanup-incomplete
```

They prove:

- complete rollback resets the binding before mutex deletion;
- unconfirmed callback shutdown preserves the binding and mutex;
- reinit is rejected after quarantine;
- reset-lock failure preserves binding and primary init error;
- prior lower cleanup failure may safely clear guarded binding but still quarantines the manager.

### 5.3 Focused sanitizer runner

`tools/run_bt_a2dp_binding_lifecycle_test.sh` now clean-configures a disposable focused build and runs:

```text
test_bt_ctx_lock
test_bt_manager_connection_pairing_events
test_a2dp_binding_diagnostics_exact
test_a2dp_cross_session_exact
test_a2dp_secondary_failures_exact
test_bt_manager_init_rollback complete
test_bt_manager_init_rollback callbacks-live
test_bt_manager_init_rollback reset-fails
test_bt_manager_init_rollback cleanup-incomplete
```

All run with ASan and UBSan, leak detection, halt-on-error, and no undefined-behavior recovery.

---

## 6. Same-SHA software evidence

The following two maintained workflows passed on exactly:

```text
77da0d122fb2673836db719cfbc5904122c259ac
```

### 6.1 Host workflow

```text
Workflow: CI — host tests (optimized)
Run ID:   30451703563
Job ID:   90575084169
Result:   SUCCESS
CTest:    80/80 passed, 0 failed
CTest time: 40.10 seconds
```

Successful enforced stages:

- clean complete-host CMake configure;
- complete host build;
- CMake target inventory generation;
- CTest JSON inventory generation;
- full-duplex state sanitizer suite;
- HFP Audio Gateway sanitizer suite;
- HFP SLC operation sanitizer suite;
- manager HFP profile rollback sanitizer suite;
- HFP PCM ring sanitizer suite;
- HFP voice conversion sanitizer suite;
- HFP I2S output sanitizer suite;
- HFP incoming-audio sanitizer suite;
- HFP audio-control sanitizer suite;
- HFP FD-13 diagnostics sanitizer suite;
- FD-16 duplex-policy sanitizer suite;
- A2DP binding-lifecycle sanitizer suite;
- changed-Python flake8 gate;
- Python unit tests;
- complete CTest.

The A2DP focused runner completed all nine listed invocations. The five secondary-failure cases and all four rollback process cases passed with zero failures and zero ignored tests.

### 6.2 Device compile-only workflow

```text
Workflow: CI — device build (compile only)
Run ID:   30451703498
Job ID:   90575050929
Result:   SUCCESS
ESP-IDF:  v5.5.1
Target:   esp32
Flash:    NOT EXECUTED
```

The workflow enforced:

- HFP enabled;
- HFP Audio Gateway enabled;
- HFP client disabled;
- HFP audio data path set to HCI;
- PCM data path disabled;
- one BR/EDR synchronous connection;
- controller SCO data path set to HCI;
- controller PCM SCO path disabled;
- clean build directory;
- deterministic `idf.py reconfigure` with no tracked `sdkconfig` churn;
- successful `idf.py build`;
- generated `.bin` and `.elf` artifacts;
- explicit compile-only/no-flash boundary.

Firmware evidence:

```text
Application binary:       0xFACD0 = 1,027,280 bytes
Smallest app partition:   0x1B0000 = 1,769,472 bytes
App-partition headroom:   0xB5330 = 742,192 bytes
Reported free percentage: 42%
idf.py size image total:  1,027,169 bytes
```

The `idf.py size` total may be smaller than the padded application binary. The partition check uses the generated `.bin` size.

The 742,192-byte headroom substantially exceeds the maintained 256 KiB minimum compile-time gate.

---

## 7. CI workflow hardening completed

### Host workflow

The host workflow now:

- deletes the full host build directory before configure;
- does not cache `CMakeCache.txt` or compiled host outputs;
- captures target and CTest inventories;
- runs the A2DP lifecycle sanitizer runner explicitly;
- treats build, sanitizer, Python, and CTest failures as failures;
- publishes commit status context `ci/host-tests-optimized` with the run URL.

### Device workflow

The device workflow now:

- removes the build directory rather than ignoring `fullclean` failure;
- validates exact HFP/HCI configuration before build;
- fails on unexplained tracked `sdkconfig` churn;
- builds with ESP-IDF v5.5.1;
- records size evidence;
- verifies the expected firmware artifacts;
- states and enforces that no flash command ran;
- publishes commit status context `ci/device-build-compile-only` with the run URL.

---

## 8. Changed-file integrity sweep

Relative to the stabilization TODO creation commit, the durable changed-file set is limited to:

```text
.github/workflows/ci-device-build.yml
.github/workflows/ci-host-tests.yml
esp_bt_audio_source/components/bt_manager/bt_events_a2dp.c
esp_bt_audio_source/components/bt_manager/bt_manager.c
esp_bt_audio_source/components/bt_manager/include/bt_events_a2dp.h
esp_bt_audio_source/components/bt_manager/include/bt_manager_internal.h
esp_bt_audio_source/test/host_test/CMakeLists.txt
esp_bt_audio_source/test/host_test/a2dp_binding_lifecycle/CMakeLists.txt
esp_bt_audio_source/test/host_test/mocks/mock_audio_and_btstate.c
esp_bt_audio_source/test/host_test/test_a2dp_secondary_failures_exact.c
esp_bt_audio_source/test/host_test/test_bluetooth.c
esp_bt_audio_source/test/host_test/test_bt_a2dp_late_terminal_contract.c
esp_bt_audio_source/test/host_test/test_bt_manager_init_rollback.c
esp_bt_audio_source/tools/run_bt_a2dp_binding_lifecycle_test.sh
```

No temporary one-shot patch script or workflow remains. No generated build directory, firmware image, map file, test log, sanitizer log, CMake cache, or inventory artifact was committed.

---

## 9. Known non-hardware debt and explicit boundaries

### 9.1 Legacy Python lint backlog

The changed-Python flake8 gate passed. The full-tree informational audit still reports **252 legacy findings**. This is visible workflow debt, not a suppressed runtime failure and not part of the Bluetooth lifecycle stabilization scope.

Do not represent the repository as globally flake8-clean. Address that backlog in a separate tooling cleanup.

### 9.2 Broad host CMake cleanup

The host registry remains large and repetitive. Broad modularization is deferred until the feature work is unified with `master` and one green baseline is established.

Do not combine the later mechanical split/deduplication with physical device debugging.

### 9.3 Hardware and capability boundaries

Software and compile-only success do not prove:

- real A2DP interoperability;
- real HFP AG SLC completion;
- SCO/eSCO timing or repeated start/stop behavior;
- microphone audio correctness;
- GPIO32/33/27 electrical suitability;
- 16 kHz Philips I2S framing;
- receiver mono-slot interpretation;
- I2S0/I2S1 coexistence;
- physical UART0/UART2 delivery;
- runtime heap, largest-block, stack, callback-latency, or soak thresholds;
- HFP speaker downlink.

FD-18 and FD-19 remain unimplemented. Do not claim operational HFP speaker downlink or complete HFP full-duplex playback.

---

# PART II — CLAUDE CODE PHYSICAL DEVICE HANDOFF

## 10. Token-saving execution rules for Claude Code

Claude Code must not repeat the code review, refactor CMake, or rewrite passing host tests before the first hardware attempt.

Read only:

1. `CLAUDE.md`
2. `esp_bt_audio_source/CLAUDE.md`
3. this closeout file
4. Part C of `esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md`
5. the hardware-pending sections of `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
6. `docs/ESP_BT_AUDIO_HFP_SDKCONFIG_AUDIT_2026-07-29.md`

Do not read all of `memory.md`. Use targeted `grep` only when historical context is necessary.

Before the first hardware run:

- do not change production code;
- do not change `sdkconfig`;
- do not run `idf.py set-target`;
- do not add fallbacks;
- do not erase all flash;
- do not repeatedly reflash after a runtime failure;
- do not mark absent equipment or skipped tests as passing.

---

## 11. Freeze the exact candidate

The repository owner or ChatGPT handoff must provide one literal final candidate SHA after the documentation-head CI cycle completes.

Claude Code must run:

```bash
git fetch origin
git checkout feature/esp-bt-audio-duplex
git pull --ff-only
git rev-parse HEAD
git status --short
```

Required conditions:

- printed SHA equals the supplied candidate SHA exactly;
- working tree is clean;
- no opportunistic source change precedes the first device run.

Stop before build or flash if any condition fails.

---

## 12. Inspect the connected board and serial path

Run:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null || true
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
```

Record:

- exact board/module;
- USB adapter;
- stable serial path;
- power source;
- Bluetooth peer model and address;
- I2S receiver/analyzer model;
- I2S wiring.

Prefer `/dev/serial/by-id/...` when available. Stop if board identity, target, power, or wiring is uncertain.

---

## 13. Rebuild locally before flashing

From repository root:

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
cd esp_bt_audio_source
idf.py fullclean
idf.py reconfigure
git diff --exit-code -- sdkconfig
idf.py build
idf.py size
```

Record:

```text
FINAL_SHA=
ESP_IDF_VERSION=
TARGET=
LOCAL_BINARY_SIZE=
LOCAL_PARTITION_SIZE=
LOCAL_HEADROOM=
SDKCONFIG_DIFF=
```

The local binary should be consistent with the compile-only evidence above. Stop and report any material unexplained difference.

---

## 14. Obtain live confirmation and flash once

A checked-in instruction is not live authorization. Before writing the board, obtain explicit confirmation from the repository owner in the active Claude Code session.

After confirmation:

```bash
PORT=/dev/serial/by-id/<verified-device>
idf.py -p "$PORT" flash
```

Preserve the complete esptool output. Record chip identity, baud, flash size, write addresses, verification result, and reset result.

Do not erase all flash unless a captured failure demonstrates it is necessary and the owner separately approves it.

---

## 15. Capture complete boot evidence

Run:

```bash
idf.py -p "$PORT" monitor
```

Capture one cold boot and one warm reset from the earliest available bootloader output through application readiness.

Fail the boot gate for any:

- panic;
- watchdog;
- brownout;
- abort;
- illegal instruction;
- stack canary;
- heap corruption;
- repeated reset;
- unexpected manager quarantine;
- unsupported capability represented as operational.

Record initial free internal heap, lifetime minimum heap, largest internal block, and exposed stack metrics.

---

## 16. Exact command surface

The maintained HFP command forms are:

```text
HFP STATUS
HFP CONNECT <mac>
HFP DISCONNECT
HFP AUDIO START
HFP AUDIO STOP
HFP MODE DISABLED
HFP MODE A2DP_MIC
HFP MODE HFP_FULL
HFP MODE AUTO
HFP CODEC
HFP STATS
HFP RESETSTATS
```

Validate UART0 first. Validate UART2 only when it is physically connected and configured.

Confirm command responses distinguish:

- completed success;
- accepted asynchronous request;
- already-complete/idempotent state;
- unsupported capability;
- unavailable diagnostic status;
- exact lower-layer error;
- transport delivery failure.

No unavailable metric may appear as a fabricated zero. No mode may silently fall back.

---

## 17. A2DP runtime matrix

Using a known peer:

1. Pair or reuse the maintained pairing flow.
2. Connect A2DP.
3. Start continuous audio.
4. Verify connection identity and playing state.
5. Stop.
6. Remote-suspend and resume.
7. Disconnect and reconnect.
8. Repeat at least 20 start/stop cycles unless the first failure occurs earlier.

Record exact event ordering and these counters before and after each intentional boundary:

```text
missing_binding_rejections
wrong_peer_rejections
stale_handle_rejections
generation_sync_failures
late_terminal_events_ignored
```

Acceptance:

- a delayed post-disconnect STOP/SUSPEND does not invoke a new audio transition;
- a delayed old-session event cannot alter a newer session;
- a reused peer with a new connection handle rejects old-handle events visibly;
- only the intended diagnostic counter changes for each injected or observed stale event.

---

## 18. HFP SLC and SCO/CVSD matrix

Use the intended same-peer arrangement.

Test:

1. HFP profile readiness.
2. `HFP CONNECT <mac>` request acceptance and actual completion event.
3. SLC disconnect and reconnect.
4. peer rejection/timeout where practical;
5. peer power cycle;
6. `HFP AUDIO START` with matching completion event;
7. `HFP AUDIO STOP` with bounded cleanup;
8. at least 20 audio start/stop cycles;
9. incomplete-stop behavior;
10. reconnect after peer power cycle.

Record codec and SCO/eSCO start/stop timing.

Acceptance:

- request acceptance is not represented as completion;
- failed cleanup becomes explicit error/fault/quarantine;
- no fake success;
- no hidden retry loop;
- mSBC remains visibly unsupported unless separately implemented;
- HFP speaker downlink remains explicitly unimplemented.

---

## 19. I2S0 microphone-output acceptance

Reviewed intended format:

```text
BCLK:       GPIO32
WS/LRCLK:   GPIO33
DOUT:       GPIO27
Sample rate: 16 kHz
Samples:    signed 16-bit mono
Framing:    Philips I2S
```

Before connecting, confirm those pins are safe for the exact board and the receiver/analyzer voltage is compatible. Connect common ground.

Required physical evidence:

- measured stable BCLK;
- measured 16 kHz WS/LRCLK;
- verified Philips framing and bit alignment;
- captured PCM;
- intelligible microphone speech;
- verified CVSD 8 kHz to 16 kHz duplication behavior;
- correct receiver mono-slot interpretation;
- I2S0 does not disturb I2S1 playback capture;
- I2S0 does not disturb UART2;
- receiver-absent/stalled behavior is bounded and counted.

Serial logs alone cannot pass this gate.

---

## 20. Simultaneous A2DP plus HFP microphone gate

1. Start continuous A2DP playback.
2. Start HFP microphone audio.
3. Record whether the peer keeps, suspends, or stops A2DP.
4. Record exact A2DP, HFP, mode, and policy events.
5. Verify microphone PCM continues through I2S0 when the effective mode claims it should.
6. Verify I2S1/playback counters remain stable.
7. Test strict `A2DP_MIC` behavior.
8. Test explicit `AUTO` transitions.

No silent mode substitution is acceptable. Do not claim HFP speaker downlink.

---

## 21. Recovery sampling

Capture the first complete failure before modifying or reflashing.

At minimum sample:

- peer power-off during A2DP;
- peer power-off during HFP SLC;
- peer power-off during SCO;
- reconnect after peer power cycle;
- repeated start while starting/running;
- repeated stop while stopping/stopped;
- wrong-peer command while another peer owns the session;
- I2S receiver absent/stalled;
- warm reset after clean stop.

For each case record:

- exact error;
- state before and after;
- fault/quarantine state;
- retry behavior;
- recovery result;
- whether reboot is explicitly required.

Do not add fallback behavior merely to make the device appear functional.

---

## 22. Runtime resources and soak

Record at boot idle, A2DP playing, HFP SLC, CVSD SCO, simultaneous mode, and post-stop:

```text
current_free_internal_heap
lifetime_minimum_free_heap
largest_free_internal_block
minimum_stack_margin_by_task
callback_p99_us
callback_max_us
callback_overlap_count
ring_overflow_count
ring_underflow_count
I2S_timeout_count
I2S_short_write_count
inserted_silence_count
lost_byte_count
health_report_failure_count
panic_watchdog_brownout_reset_count
```

Acceptance thresholds:

- minimum internal heap at least 32 KiB, or an explicit reviewed exception;
- largest internal block at least 16 KiB, or an explicit reviewed exception;
- measured stack margin for every affected task;
- callback p99 below 500 microseconds, or explicit reviewed exception;
- callback maximum below 2 milliseconds, or explicit reviewed exception;
- no persistent heap loss over repeated cycles;
- thirty-minute simultaneous-mode soak when the setup supports it;
- zero panic, watchdog, brownout, reset-loop, or sustained unaccounted-loss failures.

Do not reset counters before recording a failed threshold.

---

## 23. Required Claude Code result template

Claude Code must finish with this exact structure and attach complete serial logs for every failure:

```text
FINAL_SHA=
ESP_IDF_VERSION=
TARGET=
SERIAL_PORT=
BOARD_OR_MODULE=
BLUETOOTH_PEER=
I2S_RECEIVER_OR_ANALYZER=
BUILD_RESULT=
FLASH_RESULT=
BOOT_RESULT=
A2DP_RESULT=
HFP_SLC_RESULT=
HFP_AUDIO_RESULT=
I2S0_RESULT=
SIMULTANEOUS_MODE_RESULT=
RECOVERY_RESULT=
SOAK_RESULT=
FIRMWARE_SIZE=
PARTITION_HEADROOM=
MIN_FREE_HEAP=
LARGEST_FREE_BLOCK=
MIN_STACK_MARGINS=
CALLBACK_P99_US=
CALLBACK_MAX_US=
PANIC_OR_RESET_COUNT=
UNEXPECTED_FALLBACKS=
SKIPPED_CASES_AND_REASONS=
FILES_CHANGED_AFTER_HARDWARE_TEST=
REMAINING_BLOCKERS=
```

Absent equipment or skipped tests must be recorded as `NOT TESTED`, never `PASS`.

---

## 24. Hardware failure handling

When a hardware test fails:

1. Stop and preserve the first complete log.
2. Classify wiring/power/port, configuration, target mismatch, interoperability, timing/resource, production logic, or unsupported future capability.
3. Do not suppress the log or counter.
4. Do not add fallback behavior.
5. For a production defect, add a deterministic host regression test where possible before changing code.
6. Re-run all affected host/sanitizer gates.
7. Obtain same-head host and compile-only CI before reflashing.
8. Minimize reflashes; each new flash must correspond to a reviewed candidate.

---

## 25. Current merge-readiness statement

At this closeout point:

- software stabilization gates: **PASS**;
- current reviewed code same-SHA host and device compile-only workflows: **PASS**;
- P0/P1 silent A2DP binding lifecycle failure identified by this TODO: **none remaining**;
- complete-host CMake configure blocker: **removed**;
- compile-time partition headroom: **PASS**;
- hardware runtime evidence: **pending**;
- operational HFP speaker downlink: **not implemented**;
- broad host CMake cleanup: **deferred**.

The branch is ready for the bounded Claude Code hardware phase. A merge into `master` should occur only after the repository owner reviews the physical evidence and explicitly accepts any remaining hardware limitations.
