# ESP32 Bluetooth Audio — Duplex Stabilization and Device Handoff TODO

**Repository:** `ekkus93/esp32_btaudio`  
**Working branch:** `feature/esp-bt-audio-duplex`  
**Primary project:** `esp_bt_audio_source/`  
**Starting branch head:** `5edbd8386c9c5ff5e21fd01a4b728abf6dbe175a`  
**Created:** 2026-07-29  
**Status:** Open  
**Primary goal:** Finish every software-only stabilization, regression, sanitizer, complete-host, and compile-only validation task before handing the repository to Claude Code for the smallest possible hardware-only session.

# Software closeout status

The software-only stabilization work was closed out in:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_SOFTWARE_CLOSEOUT_AND_CLAUDE_DEVICE_HANDOFF_2026-07-29.md`

A later review identified additional software follow-up tasks in:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_REVIEW_FIXES_TODO_2026-07-30.md`

Do not treat unchecked software boxes below as the sole source of current
status without reading those closeout and follow-up files. Physical hardware
tasks remain open until real ESP32-WROOM-32 evidence is captured.

---

---

## 1. Why this TODO exists

The repository owner is close to the current Claude Code token quota and does not receive a quota reset until Thursday night. Claude Code should therefore not spend its remaining quota discovering host-build problems, understanding the A2DP binding history, writing ordinary unit tests, repairing CMake bookkeeping, or diagnosing GitHub Actions failures that do not require a physical ESP32.

The intended division of work is:

1. Complete all work that can be performed through source inspection, host tests, sanitizers, Python tests, ESP-IDF compile-only validation, and GitHub Actions.
2. Produce one exact final commit SHA with a clean software evidence package.
3. Hand that exact commit to Claude Code.
4. Use Claude Code only for the work that genuinely requires the connected ESP32, local serial port, Bluetooth peers, I2S wiring, and physical runtime observation.

This TODO is self-contained enough for a new agent to resume the work without relying on the current ChatGPT conversation.

---

## 2. Authoritative existing files

Every referenced file in this section exists in the repository at the exact path shown.

- `docs/ESP_BT_AUDIO_A2DP_BINDING_LIFECYCLE_SAFETY_FIX1_TODO.md`
- `docs/ESP_BT_AUDIO_HFP_SDKCONFIG_AUDIT_2026-07-29.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_REVIEW_FIX_TODO_2026-07-28.md`
- `esp_bt_audio_source/docs/ESP_BT_AUDIO_HFP_CALLBACK_RESOURCE_BUDGET_2026-07-28.md`
- `.github/workflows/ci-host-tests.yml`
- `.github/workflows/ci-device-build.yml`
- `CLAUDE.md`

The first file remains the detailed behavioral specification for A2DP binding lifecycle FIX1. This document is the authoritative remaining-work and handoff plan.

Do not reference a new review report, response file, template, or closeout document unless that file is also committed at the exact path named.

---

## 3. Current state at the starting head

The following production behavior is present at the starting head, but it is not considered complete until the remaining tests and same-head CI gates pass:

- `bt_events_a2dp_reset_binding()` returns `esp_err_t`.
- Binding reset acquires `bt_ctx` synchronization and returns the exact lock error without an unlocked fallback.
- `bt_manager_init()` creates the mutex before resetting guarded A2DP binding state.
- Initialization reset failure deletes the newly created mutex and returns the exact error.
- Confirmed teardown resets binding before deleting the manager mutex.
- Unconfirmed callback shutdown preserves callback-owned state and quarantines the manager.
- Teardown reset failure preserves manager state and mutex and quarantines the manager.
- Unbound A2DP `STARTED` fails closed.
- Late unbound `STOPPED` and `REMOTE_SUSPEND` events are ignored as observable no-ops.
- Wrong-peer and stale-connection-handle events are rejected.
- Exact A2DP diagnostic counter tests exist.
- Cross-session delayed terminal-event tests have been added and wired into the focused sanitizer runner.
- A dedicated focused CMake project builds the real production manager and A2DP event sources with ASan and UBSan.
- An incompatible host-mock struct copy found by UBSan was corrected.
- HFP-related `sdkconfig` changes have an audit document.

Known remaining gaps include:

- The complete host CMake graph still contains a retired pairing-adapter test target whose source files were deliberately deleted.
- Direct initialization rollback-finalization regression tests are incomplete.
- Two helper paths in `bt_events_a2dp.c` still quietly discard lock failures.
- Current-head focused tests, full CTest, Python gates, host CI, and compile-only device CI have not all been proven green on one final SHA.
- Physical board tests have not been run for the current head.

---

## 4. Non-negotiable execution rules

- [ ] Work directly on `feature/esp-bt-audio-duplex`.
- [ ] Do not create a new branch or pull request unless the repository owner explicitly changes this instruction.
- [ ] Do not force-push or rewrite branch history.
- [ ] Do not merge into `master` until the software stabilization gates in this file pass.
- [ ] Do not perform the broad 1,700-line host CMake modularization during this stabilization pass.
- [ ] The only permitted broad-host-CMake change in this pass is removal or repair of objectively stale targets that prevent a clean configure.
- [ ] Do not restore retired pairing adapter sources with placeholders, fake implementations, no-op tests, or fabricated events.
- [ ] Do not weaken assertions, convert exact checks to `>=`, suppress sanitizer output, add arbitrary sleeps, or change failures into warnings merely to obtain green CI.
- [ ] Do not add quiet fallback behavior, best-effort state mutation, fake success, generic error replacement, or silent error swallowing.
- [ ] Preserve the primary error when a secondary cleanup or diagnostic operation also fails.
- [ ] Host success does not prove device compilation.
- [ ] Device compilation does not prove runtime behavior, pin correctness, SCO behavior, PCM quality, or resource margins.
- [ ] ChatGPT must not flash the ESP32 during the software phase.
- [ ] Claude Code must obtain explicit confirmation in its live hardware session before executing `idf.py flash`; a checked-in file alone is not authorization.
- [ ] Do not change the configured ESP-IDF target merely to make a build pass. The maintained project documentation currently identifies the full-duplex target as ESP32-WROOM-32 and CI installs the `esp32` toolchain.

---

# PART A — SOFTWARE-ONLY STABILIZATION

## Task STAB-00 — Reconfirm the exact baseline

- [ ] Run `git status --short` and record whether the working tree is clean.
- [ ] Run `git branch --show-current` and require exactly:

```text
feature/esp-bt-audio-duplex
```

- [ ] Run `git rev-parse HEAD`.
- [ ] Confirm the starting point is `5edbd8386c9c5ff5e21fd01a4b728abf6dbe175a` or a direct descendant.
- [ ] Read `CLAUDE.md` and `esp_bt_audio_source/CLAUDE.md` before executing repository commands.
- [ ] Record any pre-existing uncommitted files. Do not delete user logs, local configuration, or generated data without determining ownership.
- [ ] Confirm no other agent has advanced the branch before each write.

**Acceptance condition:** The active branch, exact head, and pre-existing worktree state are recorded before modifications.

---

## Task STAB-01 — Repair the complete host CMake configure blocker

### STAB-01.1 — Remove only the retired adapter target

The complete host CMake file is:

```text
esp_bt_audio_source/test/host_test/CMakeLists.txt
```

It contains a block beginning with the comment:

```cmake
## Adapter runner: compile test_bluetooth pairing test and the adapter code so we can
```

The block defines and uses:

```cmake
TEST_PAIRING_ADAPTER_SRC

test_pairing_adapter_runner

../../../test_bluetooth/main/test_pairing_adapters.c
../../../test_bluetooth/main/test_pairing_commands.c
```

Those source files were intentionally retired because the old harness fabricated pairing behavior that no longer matched the production notification contract.

- [ ] Confirm both referenced files are absent from the branch.
- [ ] Confirm repository history shows their removal was intentional rather than accidental corruption.
- [ ] Delete the complete obsolete target block, including:
  - [ ] the `set(TEST_PAIRING_ADAPTER_SRC ...)` declaration;
  - [ ] `add_executable(test_pairing_adapter_runner ...)`;
  - [ ] every `target_sources`, `target_link_libraries`, `target_compile_definitions`, and `add_test` statement for that target.
- [ ] Do not recreate either source file.
- [ ] Do not create an empty test with the same target name.
- [ ] Do not add `if(EXISTS ...)` around the broken target. Quietly skipping a supposedly maintained test would hide graph drift.
- [ ] Add a concise CMake comment only if necessary to explain that production pairing-notification tests replaced the retired adapter harness.

### STAB-01.2 — Prove clean configure rather than cached success

Run from repository root:

```bash
rm -rf esp_bt_audio_source/test/host_test/build_host_tests
cmake \
  -S esp_bt_audio_source/test/host_test \
  -B esp_bt_audio_source/test/host_test/build_host_tests
cmake \
  --build esp_bt_audio_source/test/host_test/build_host_tests \
  --parallel "$(nproc)"
```

- [ ] The configure must begin with no prior `CMakeCache.txt`.
- [ ] Do not rely on GitHub Actions cache restoration to prove the graph is correct.
- [ ] If a second missing-source target appears, inspect its history and current replacement coverage before changing it.
- [ ] Remove a second stale target only when its sources were intentionally retired and maintained replacement coverage exists.
- [ ] Do not perform generalized CMake deduplication during this task.

### STAB-01.3 — Capture the test inventory

After successful configure:

```bash
cmake \
  --build esp_bt_audio_source/test/host_test/build_host_tests \
  --target help \
  > esp_bt_audio_source/test/host_test/build_host_tests/target-help.txt

ctest \
  --test-dir esp_bt_audio_source/test/host_test/build_host_tests \
  --show-only=json-v1 \
  > esp_bt_audio_source/test/host_test/build_host_tests/ctest-inventory.json
```

- [ ] Record the number of configured CTest tests.
- [ ] Confirm no maintained executable silently lost its `add_test()` registration.
- [ ] Keep generated inventory files out of the commit unless explicitly documenting them is necessary.

**Acceptance condition:** A clean full host configure and full build complete without referencing deleted sources, and no placeholder test code is introduced.

---

## Task STAB-02 — Eliminate remaining quiet A2DP lock failures

The current starting implementation contains two helpers that return silently when `bt_ctx_lock()` fails:

```c
static void increment_generation_sync_failure(...)
{
    if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) return;
    ...
}
```

```c
static void clear_binding_if_identity(...)
{
    if (bt_ctx_lock(PLATFORM_WAIT_FOREVER) != ESP_OK) return;
    ...
}
```

This violates the FIX1 requirement that lock and reset failures must not disappear silently.

### STAB-02.1 — Make generation-diagnostic update failure visible

- [ ] Change `increment_generation_sync_failure()` to return `esp_err_t`.
- [ ] Return the exact lock error when the counter cannot be updated.
- [ ] Preserve the original generation/status error as the primary operation error.
- [ ] Log the secondary diagnostic-update error with enough context to diagnose it:
  - [ ] lifecycle serial;
  - [ ] connection handle;
  - [ ] primary error;
  - [ ] counter-update lock error.
- [ ] Do not replace the original failure with the secondary counter failure.
- [ ] Do not perform an atomic or unlocked counter increment as a fallback unless the entire counter ownership model is deliberately redesigned and reviewed.

Preferred pattern:

```c
static esp_err_t increment_generation_sync_failure(
    uint32_t lifecycle_serial,
    esp_a2d_conn_hdl_t conn_handle)
{
    esp_err_t lock_err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (lock_err != ESP_OK) return lock_err;

    if (s_policy_binding.valid &&
        s_policy_binding.lifecycle_serial == lifecycle_serial &&
        s_policy_binding.conn_handle == conn_handle) {
        increment_u64_saturating(
            &s_policy_binding.generation_sync_failures);
    }

    bt_ctx_unlock();
    return ESP_OK;
}
```

At each call site, retain the primary error and report a secondary error explicitly.

### STAB-02.2 — Make binding-clear failure visible

- [ ] Change `clear_binding_if_identity()` to return `esp_err_t`.
- [ ] Return the exact lock failure.
- [ ] Distinguish these outcomes:
  - [ ] binding matched and was cleared;
  - [ ] binding did not match because the session changed;
  - [ ] lock acquisition failed.
- [ ] Decide and document whether an identity mismatch should return `ESP_ERR_NOT_FOUND` or `ESP_ERR_INVALID_STATE`; do not report it as `ESP_OK` unless a deliberate idempotent contract is documented.
- [ ] Make `apply_connection_policy()` consume the return value.
- [ ] Do not silently continue after a disconnect clear lock failure.
- [ ] Do not replace a prior policy error with the clear error. Preserve the first error and report the secondary cleanup error.
- [ ] Ensure the failure cannot look like a fully clean disconnect in logs or diagnostics.

### STAB-02.3 — Add exact tests

Add focused tests that force the next `bt_ctx_lock()` failure at each helper boundary.

- [ ] Generation diagnostic case:
  - [ ] force an HFP status/generation failure;
  - [ ] force the diagnostic counter lock to fail;
  - [ ] verify the primary operation error remains authoritative;
  - [ ] verify the secondary lock error is visible;
  - [ ] verify no unlocked counter write occurs.
- [ ] Binding clear case:
  - [ ] establish a valid binding;
  - [ ] drive a matching disconnect far enough to exercise the clear helper;
  - [ ] force the clear lock to fail;
  - [ ] verify the binding is preserved;
  - [ ] verify the exact error is visible;
  - [ ] verify no unlocked clear occurs.
- [ ] Use one-shot failure injection. Do not add timing sleeps.

**Acceptance condition:** A repository search finds no `bt_ctx_lock(...) != ESP_OK) return;` pattern in A2DP binding lifecycle helpers where the failure would be silently discarded.

---

## Task STAB-03 — Add the missing initialization rollback-finalization tests

The device-only `bt_manager_init()` failure path runs under `#ifdef ESP_PLATFORM`, while ordinary host tests compile with `UNIT_TEST` and explicitly undefine `ESP_PLATFORM`. Do not write a test that claims to cover device rollback while actually taking the trivial host `bt_manager_init()` branch.

### STAB-03.1 — Extract one production rollback finalizer

Preferred minimal refactor:

- [ ] Extract the final callback-shutdown/binding-reset/quarantine portion of the existing `fail:` block into one production helper used by the real `bt_manager_init()` failure path.
- [ ] Keep the actual controller, Bluedroid, profile, and duplex cleanup sequence in the existing reverse-order rollback.
- [ ] The helper must receive the original init error and the cleanup facts determined by the real rollback.
- [ ] Expose only a `UNIT_TEST` wrapper around the same production helper.
- [ ] Do not duplicate rollback semantics in a test-only implementation.

Suggested contract:

```c
static esp_err_t bt_manager_finalize_init_rollback(
    esp_err_t init_error,
    bool callbacks_stopped,
    bool cleanup_complete,
    bool duplex_state_initialized);
```

Possible test wrapper:

```c
#ifdef UNIT_TEST
esp_err_t bt_manager_test_finalize_init_rollback(
    esp_err_t init_error,
    bool callbacks_stopped,
    bool cleanup_complete,
    bool duplex_state_initialized)
{
    return bt_manager_finalize_init_rollback(
        init_error,
        callbacks_stopped,
        cleanup_complete,
        duplex_state_initialized);
}
#endif
```

Adjust the signature if necessary, but preserve these requirements:

- the original init error is always returned;
- callback-owned binding state is cleared only when callbacks are confirmed stopped;
- reset occurs while the mutex still exists;
- reset failure preserves mutex and binding;
- incomplete cleanup quarantines the manager;
- unconfirmed callback shutdown preserves binding and mutex;
- successful complete cleanup deletes the mutex only after successful reset.

### STAB-03.2 — Case A: callbacks stopped and reset succeeds

- [ ] Create the manager mutex through the checked test helper.
- [ ] Establish a non-empty A2DP binding through the production event path.
- [ ] Call the production rollback finalizer wrapper with:
  - [ ] a distinctive original init error;
  - [ ] `callbacks_stopped = true`;
  - [ ] otherwise complete cleanup.
- [ ] Assert the exact original init error is returned.
- [ ] Assert binding reset occurred.
- [ ] Assert reset happened before mutex deletion by proving reset succeeded through the real lock.
- [ ] Assert the mutex is deleted only after reset.
- [ ] Assert the manager is not quarantined solely because the original init operation failed when rollback was complete.
- [ ] Assert no callback-owned state survives.

### STAB-03.3 — Case B: callback shutdown unconfirmed

- [ ] Establish a non-empty binding.
- [ ] Call the same production finalizer with `callbacks_stopped = false`.
- [ ] Assert the exact original init error is returned.
- [ ] Assert binding identity and all counters remain unchanged.
- [ ] Assert the mutex remains usable for potentially live callbacks.
- [ ] Assert the manager is quarantined.
- [ ] Assert reinitialization is rejected with `ESP_ERR_INVALID_STATE`.
- [ ] Assert no reset attempt is made.

### STAB-03.4 — Case C: callbacks stopped but binding reset lock fails

- [ ] Establish a non-empty binding.
- [ ] Force the next context-lock result to a distinctive error.
- [ ] Call the same production finalizer with `callbacks_stopped = true`.
- [ ] Assert the exact original init error remains the return value.
- [ ] Assert the reset error is visible as a secondary cleanup failure.
- [ ] Assert binding remains byte-for-byte unchanged.
- [ ] Assert mutex remains present.
- [ ] Assert the manager is quarantined.
- [ ] Assert reinitialization is rejected.

### STAB-03.5 — Case D: other rollback stage already failed

- [ ] Call the finalizer with callbacks stopped but `cleanup_complete = false` before binding finalization.
- [ ] Assert a successful binding reset may still occur when safe.
- [ ] Assert the manager remains quarantined because rollback was incomplete.
- [ ] Assert the original init error remains the return value.
- [ ] Assert secondary cleanup failure is visible and not converted into fake success.

**Acceptance condition:** Tests exercise the exact production rollback finalizer used by the ESP-platform `fail:` path and prove all four outcomes without compiling a fake host-only replacement.

---

## Task STAB-04 — Finish A2DP event-regression coverage

### STAB-04.1 — Verify the new cross-session test builds and runs

The starting branch contains:

```text
esp_bt_audio_source/test/host_test/test_a2dp_cross_session_exact.c
```

It is wired into:

```text
esp_bt_audio_source/test/host_test/a2dp_binding_lifecycle/CMakeLists.txt
esp_bt_audio_source/tools/run_bt_a2dp_binding_lifecycle_test.sh
```

- [ ] Build the test from a clean focused build directory.
- [ ] Run under both ASan and UBSan.
- [ ] Confirm delayed `STOPPED` from old peer/session A cannot mutate session B.
- [ ] Confirm delayed `REMOTE_SUSPEND` from old peer/session A cannot mutate session B.
- [ ] Confirm a same-peer, old-connection-handle event cannot mutate the newer session.
- [ ] Assert exact wrong-peer or stale-handle counter deltas as appropriate.
- [ ] Assert `audio_playing`, peer identity, handle, lifecycle serial, and generation remain unchanged for session B.

### STAB-04.2 — Prove late unbound terminals do not request generation refresh

The FIX1 TODO requires no generation refresh for late unbound terminal events.

- [ ] Add or use a mock call counter around `bt_manager_hfp_get_status()` or the narrowest generation-refresh dependency.
- [ ] Snapshot the call count before the late event.
- [ ] Deliver late unbound `STOPPED`.
- [ ] Assert call-count delta is exactly zero.
- [ ] Repeat for late unbound `REMOTE_SUSPEND`.
- [ ] Do not infer this solely from unchanged state; assert the dependency was not called.

### STAB-04.3 — Make unbound START error evidence exact

The public event handler is `void`, so the test cannot pretend to directly receive an `esp_err_t` from it.

- [ ] Assert the exact observable contract through supported hooks:
  - [ ] `missing_binding_rejections` increases by exactly one;
  - [ ] `late_terminal_events_ignored` does not change;
  - [ ] no audio callback runs;
  - [ ] no policy call runs;
  - [ ] `bt_ctx.audio_playing` remains unchanged;
  - [ ] the expected error-level diagnostic path is invoked when log capture is available.
- [ ] If an internal `prepare_audio_event()` test hook is added, it must call the real production function and exist only under `UNIT_TEST`.
- [ ] Do not change the public callback signature merely to simplify a test.

### STAB-04.4 — Verify exact diagnostic counter isolation

- [ ] Run `test_a2dp_binding_diagnostics_exact`.
- [ ] Confirm exactly one event is driven for each diagnostic category:
  - [ ] wrong peer;
  - [ ] stale handle;
  - [ ] generation-sync failure;
  - [ ] missing binding;
  - [ ] late terminal ignored.
- [ ] Assert each intended counter changes by exactly `+1`.
- [ ] Assert unrelated counters remain unchanged.
- [ ] Assert tests use baseline-relative snapshots rather than assuming process-lifetime counters begin at zero.

**Acceptance condition:** All FIX1 event-identity cases have direct deterministic host coverage and pass under sanitizers.

---

## Task STAB-05 — Run every focused sanitizer gate

Start by running the new lifecycle suite:

```bash
bash esp_bt_audio_source/tools/run_bt_a2dp_binding_lifecycle_test.sh
```

The runner must execute at least:

```text
test_bt_ctx_lock
test_bt_manager_connection_pairing_events
test_a2dp_binding_diagnostics_exact
test_a2dp_cross_session_exact
```

Add the initialization rollback-finalization binary to the same runner or another clearly named maintained sanitizer runner.

Then run the same maintained focused gates used by host CI:

```bash
bash esp_bt_audio_source/tools/run_bt_duplex_state_test.sh
bash esp_bt_audio_source/tools/run_bt_hfp_ag_test.sh
bash esp_bt_audio_source/tools/run_bt_hfp_connection_test.sh
bash esp_bt_audio_source/tools/run_bt_manager_hfp_profiles_test.sh
bash esp_bt_audio_source/tools/run_hfp_pcm_ring_test.sh
bash esp_bt_audio_source/tools/run_hfp_voice_convert_test.sh
bash esp_bt_audio_source/tools/run_hfp_i2s_output_test.sh
bash esp_bt_audio_source/tools/run_bt_hfp_audio_test.sh
bash esp_bt_audio_source/tools/run_bt_hfp_audio_control_test.sh
bash esp_bt_audio_source/tools/run_bt_hfp_diagnostics_test.sh
bash esp_bt_audio_source/tools/run_bt_duplex_policy_test.sh
```

Also run the specific command regression gate required by the FIX1 TODO:

```bash
bash esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh
```

For each runner:

- [ ] Record command.
- [ ] Record executable/test count.
- [ ] Record Unity pass/failure count.
- [ ] Confirm ASan reports no memory error or leak.
- [ ] Confirm UBSan reports no undefined behavior.
- [ ] Confirm no runner contains `|| true` around an actual test command.
- [ ] Confirm no runner discards a failing pipeline status.
- [ ] Confirm the runner deletes only its dedicated disposable build directory, not unrelated user files.

**Acceptance condition:** Every focused gate completes with zero Unity, ASan, UBSan, linker, or leak failures.

---

## Task STAB-06 — Run the complete host and Python validation

### STAB-06.1 — Complete CTest

From a clean build:

```bash
rm -rf esp_bt_audio_source/test/host_test/build_host_tests
cmake \
  -S esp_bt_audio_source/test/host_test \
  -B esp_bt_audio_source/test/host_test/build_host_tests
cmake \
  --build esp_bt_audio_source/test/host_test/build_host_tests \
  --parallel "$(nproc)"
ctest \
  --test-dir esp_bt_audio_source/test/host_test/build_host_tests \
  --output-on-failure \
  -j1
```

- [ ] Record total CTest count.
- [ ] Record passed, failed, skipped, and disabled counts.
- [ ] Investigate every failure from its complete output.
- [ ] Do not rerun until green without preserving the first failure evidence.
- [ ] Do not exclude a failing test from CTest merely to unblock the branch.
- [ ] Treat any intentionally failing diagnostic executable separately and ensure it is not accidentally registered as an ordinary success test.

### STAB-06.2 — Python tests

```bash
python3 -m pip install --upgrade pip
python3 -m pip install pytest pyserial flake8
python3 -m pytest -q esp_bt_audio_source/tools/tests
```

- [ ] Record pytest count and result.
- [ ] A missing test directory is a failure, not a reason to print “skipping.”

### STAB-06.3 — Python lint

- [ ] Run strict flake8 on Python files changed by this stabilization work.
- [ ] Keep the existing full-tree flake8 backlog informational unless this work changes a backlogged file.
- [ ] Do not broaden `.flake8` ignores to hide a new error.
- [ ] Record changed-file lint result and full-tree backlog count separately.

### STAB-06.4 — Generated artifact hygiene

Before committing:

```bash
git status --short
git diff --check
```

- [ ] Do not commit build directories.
- [ ] Do not commit CMake caches.
- [ ] Do not commit sanitizer binaries.
- [ ] Do not commit serial logs unless intentionally placed under a documented evidence path.
- [ ] Do not commit generated audio captures unless explicitly required and reviewed.

**Acceptance condition:** Clean full host build, complete CTest, Python tests, and changed-file lint all pass locally.

---

## Task STAB-07 — Run deterministic ESP-IDF v5.5.1 compile-only validation

Use the repository's maintained ESP-IDF installation when available:

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
cd esp_bt_audio_source
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py size
```

CI uses ESP-IDF v5.5.1 and installs the `esp32` toolchain.

- [ ] Do not add `|| true` to `idf.py fullclean`, `reconfigure`, or `build` in the local proof command.
- [ ] Confirm the build target matches the maintained project target.
- [ ] Confirm a clean reconfigure does not create unexplained `sdkconfig` churn.
- [ ] Diff `sdkconfig` before and after reconfigure.
- [ ] Classify any churn before committing it.
- [ ] Confirm required configuration remains present:
  - [ ] HFP AG enabled;
  - [ ] HFP client disabled if that remains the reviewed design;
  - [ ] HCI SCO data path retained;
  - [ ] one synchronous BR/EDR connection enabled;
  - [ ] reviewed HFP I2S values retained;
  - [ ] no accidental PCM data-path switch.
- [ ] Record application binary size.
- [ ] Record smallest applicable partition size.
- [ ] Record exact free headroom in bytes and percentage.
- [ ] Require more than 256 KiB of partition headroom or document an explicit reviewed exception.
- [ ] Do not flash hardware in this task.
- [ ] Do not claim physical pin or audio validation.

**Acceptance condition:** A clean ESP-IDF v5.5.1 compile succeeds with deterministic configuration and recorded image metrics.

---

## Task STAB-08 — Obtain same-head GitHub Actions evidence

The required workflows are:

```text
CI — host tests (optimized)
CI — device build (compile only)
```

They are defined at:

```text
.github/workflows/ci-host-tests.yml
.github/workflows/ci-device-build.yml
```

- [ ] Push all stabilization commits to `feature/esp-bt-audio-duplex`.
- [ ] Record the exact pushed SHA.
- [ ] Confirm both workflows run for that exact SHA.
- [ ] Do not treat a previous commit's green run as current-head evidence.
- [ ] Inspect individual jobs and steps, not only the overall badge.
- [ ] On failure, capture the exact failing step and complete logs.
- [ ] Patch the root cause directly on the same branch.
- [ ] Repeat until both workflows pass on one identical final SHA.

Record:

- [ ] Final SHA.
- [ ] Host workflow run ID.
- [ ] Host job ID and conclusion.
- [ ] CTest count and conclusion.
- [ ] Focused sanitizer counts and conclusion.
- [ ] Python test count and conclusion.
- [ ] Changed-file lint conclusion.
- [ ] Device workflow run ID.
- [ ] Device job ID and conclusion.
- [ ] ESP-IDF version.
- [ ] Firmware size.
- [ ] Partition size.
- [ ] Headroom bytes and percentage.
- [ ] Explicit `no flash` statement for CI.

**Acceptance condition:** Both workflows pass on the same final commit and the actual job logs support every recorded result.

---

## Task STAB-09 — Reconcile the FIX1 and full-duplex TODOs

After same-head CI is green:

- [ ] Review every checkbox in `docs/ESP_BT_AUDIO_A2DP_BINDING_LIFECYCLE_SAFETY_FIX1_TODO.md` against source and evidence.
- [ ] Mark a checkbox complete only when its exact behavior is implemented and tested.
- [ ] Leave physical hardware items open.
- [ ] Do not mark an item complete solely because compilation succeeded.
- [ ] Update `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md` only where new same-head evidence changes the status.
- [ ] Preserve the explicit FD-18/FD-19 limitation: operational HFP playback downlink is not implemented.
- [ ] Preserve all untested hardware boundaries.
- [ ] Add a final software closeout section to this document containing:
  - [ ] exact final SHA;
  - [ ] files changed;
  - [ ] behavioral changes;
  - [ ] test commands;
  - [ ] test counts;
  - [ ] sanitizer results;
  - [ ] workflow run IDs;
  - [ ] image and partition metrics;
  - [ ] remaining hardware tasks;
  - [ ] known limitations;
  - [ ] confirmation that no hardware was flashed during software stabilization.
- [ ] Append a factual, concise entry to `memory.md` using an actual timestamp obtained from the environment. Do not guess a timestamp.

**Acceptance condition:** Repository documentation accurately distinguishes implemented software, verified software, compile-only evidence, and hardware-pending work.

---

# PART B — CLAUDE CODE HARDWARE HANDOFF PREPARATION

## Task HANDOFF-00 — Freeze one exact hardware candidate

Do not hand Claude Code a moving branch name alone.

- [ ] Record the final validated SHA from STAB-08.
- [ ] Require Claude Code to run:

```bash
git fetch origin
git checkout feature/esp-bt-audio-duplex
git pull --ff-only
git rev-parse HEAD
```

- [ ] Require the printed SHA to match the recorded candidate exactly.
- [ ] If it does not match, stop before build or flash.
- [ ] Do not allow opportunistic source changes before the first hardware run.
- [ ] Create a new commit only if hardware evidence identifies a real defect.

---

## Task HANDOFF-01 — Give Claude Code a minimal reading list

Claude Code should read only the files needed for the hardware run:

1. `CLAUDE.md`
2. `esp_bt_audio_source/CLAUDE.md`
3. This TODO
4. `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
5. `docs/ESP_BT_AUDIO_A2DP_BINDING_LIFECYCLE_SAFETY_FIX1_TODO.md`
6. `docs/ESP_BT_AUDIO_HFP_SDKCONFIG_AUDIT_2026-07-29.md`

- [ ] Do not ask Claude Code to read all of `memory.md`.
- [ ] Use `grep` for recent relevant entries only.
- [ ] Do not ask Claude Code to repeat the completed code review.
- [ ] Do not ask Claude Code to refactor CMake.
- [ ] Do not ask Claude Code to rewrite tests unless hardware exposes a defect requiring a regression test.

---

## Task HANDOFF-02 — Prepare exact command and evidence template

Give Claude Code this required response structure before it starts:

```text
FINAL_SHA=
ESP_IDF_VERSION=
TARGET=
SERIAL_PORT=
BOARD_OR_MODULE=
BUILD_RESULT=
FLASH_RESULT=
BOOT_RESULT=
A2DP_RESULT=
HFP_SLC_RESULT=
HFP_AUDIO_RESULT=
I2S0_RESULT=
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
FILES_CHANGED_AFTER_HARDWARE_TEST=
REMAINING_BLOCKERS=
```

- [ ] Require full serial logs for every failure.
- [ ] Require exact command output rather than “it worked.”
- [ ] Require timestamps or sequence markers around test transitions.
- [ ] Require explicit reporting of every skipped case and why it was skipped.
- [ ] Do not allow absent hardware to be represented as a pass.

---

# PART C — CLAUDE CODE PHYSICAL DEVICE PHASE

Claude Code should perform this part only after the repository owner explicitly confirms flashing in the live Claude Code session.

## Task HW-00 — Inspect the physical setup before writing

- [ ] Confirm the intended board/module matches the project target.
- [ ] Confirm the correct USB device is attached.
- [ ] Discover the stable serial path:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null || true
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
```

- [ ] Prefer `/dev/serial/by-id/...` over an unstable `/dev/ttyUSB0` path when available.
- [ ] Confirm no other serial monitor owns the port.
- [ ] Record board/module, USB adapter, serial path, and power source.
- [ ] Confirm common ground and all intended I2S wires before any I2S acceptance test.
- [ ] Confirm the receiver or analyzer can tolerate the configured voltage and format.
- [ ] Stop if target identity or wiring is uncertain.

---

## Task HW-01 — Rebuild the exact candidate locally

From repository root:

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
cd esp_bt_audio_source
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py size
```

- [ ] Record ESP-IDF version from the environment.
- [ ] Record the exact Git SHA.
- [ ] Confirm local image size is consistent with compile-only CI.
- [ ] Confirm no unexplained `sdkconfig` diff.
- [ ] Stop and report if the local build differs materially from CI.
- [ ] Do not run `idf.py set-target` unless the maintained configuration is demonstrably incorrect and the repository owner approves the change.

---

## Task HW-02 — Flash once and preserve complete output

After live confirmation:

```bash
idf.py -p "$PORT" flash
```

- [ ] Record the exact command.
- [ ] Save the complete esptool output.
- [ ] Record chip identification, flash size, baud, addresses, and verification result.
- [ ] Do not erase all flash unless required and separately justified.
- [ ] Do not repeatedly flash after a runtime failure without first capturing the failure.
- [ ] If flash verification fails, stop and diagnose power, cable, port, permissions, or target mismatch.

---

## Task HW-03 — Capture cold-boot and warm-reset behavior

Start the monitor without discarding startup output:

```bash
idf.py -p "$PORT" monitor
```

- [ ] Capture a full cold boot from reset vector through application readiness.
- [ ] Capture one warm reset.
- [ ] Confirm no panic, watchdog, brownout, abort, illegal instruction, stack canary, heap corruption, or reset loop.
- [ ] Confirm reported project name, version, chip target, and configuration are expected.
- [ ] Confirm Bluetooth manager initialization completes.
- [ ] Confirm the manager is not unexpectedly quarantined at boot.
- [ ] Confirm HFP AG/profile initialization reports the expected supported state.
- [ ] Confirm no unsupported capability is represented as operational.
- [ ] Record initial free heap, largest block, and available stack diagnostics when exposed.

---

## Task HW-04 — Basic command and transport validation

- [ ] Confirm the primary command UART responds.
- [ ] Confirm UART2 only when physically connected and configured.
- [ ] Run status/version/diagnostic commands documented by the project.
- [ ] Run HFP status and statistics commands.
- [ ] Confirm command responses distinguish:
  - [ ] success;
  - [ ] accepted but not completed;
  - [ ] unsupported;
  - [ ] unavailable status;
  - [ ] exact lower-layer failure;
  - [ ] transport delivery failure.
- [ ] Confirm no command reports fake zero for unavailable metrics.
- [ ] Confirm no fallback mode change occurs without an explicit event/reason.

---

## Task HW-05 — A2DP lifecycle runtime tests

Use a known Bluetooth peer and record its address/name.

- [ ] Pair or reuse a known pairing according to the maintained command flow.
- [ ] Connect A2DP.
- [ ] Start known continuous audio playback.
- [ ] Verify `audio_playing` and connection identity become correct.
- [ ] Stop playback.
- [ ] Remote-suspend playback.
- [ ] Disconnect.
- [ ] Reconnect.
- [ ] Repeat at least 20 start/stop cycles unless a failure occurs earlier.
- [ ] Confirm no stale terminal event clears a newer active session.
- [ ] Where two peers are available:
  - [ ] complete session A;
  - [ ] establish session B;
  - [ ] observe any delayed session-A events;
  - [ ] verify session B remains correct.
- [ ] Where reconnect reuses the same peer:
  - [ ] confirm a new connection handle/session is established;
  - [ ] verify old-handle events are rejected visibly.
- [ ] Record exact A2DP event ordering and diagnostic counters.
- [ ] Confirm wrong-peer/stale-handle/missing-binding/late-terminal counters change only for the intended event.

---

## Task HW-06 — HFP SLC and SCO/CVSD tests

Use the actual supported peer arrangement documented for the project.

- [ ] Establish HFP AG profile readiness.
- [ ] Establish an HFP SLC with the intended peer.
- [ ] Test connect, disconnect, reject, timeout, reconnect, and peer power-cycle behavior.
- [ ] Start CVSD SCO/eSCO microphone audio.
- [ ] Confirm the matching completion event is required before success is reported.
- [ ] Stop audio and confirm bounded cleanup.
- [ ] Repeat at least 20 start/stop cycles.
- [ ] Verify a failed or incomplete stop becomes fault/quarantine state rather than fake success.
- [ ] Record actual SCO start/stop timing.
- [ ] Record codec negotiation.
- [ ] Confirm mSBC remains visibly unsupported unless separately implemented.
- [ ] Confirm HFP downlink playback remains explicitly unimplemented until FD-18/FD-19.

---

## Task HW-07 — I2S0 microphone output acceptance

Current reviewed wire-format intent:

```text
BCLK: GPIO32
WS/LRCLK: GPIO33
DOUT: GPIO27
Sample rate: 16 kHz
Samples: signed 16-bit mono
Framing: Philips I2S
```

- [ ] Verify the actual board supports those pins safely.
- [ ] Connect common ground.
- [ ] Verify BCLK frequency and stability.
- [ ] Verify WS/LRCLK is 16 kHz.
- [ ] Verify data framing and bit alignment.
- [ ] Capture PCM from the receiver, logic analyzer, or audio interface.
- [ ] Confirm microphone speech is intelligible.
- [ ] Confirm CVSD 8 kHz to 16 kHz duplication behavior in captured samples.
- [ ] Confirm receiver mono-slot interpretation.
- [ ] Confirm I2S0 activity does not disturb I2S1 playback capture.
- [ ] Confirm I2S0 activity does not disturb UART2.
- [ ] Test receiver absent or stalled behavior.
- [ ] Confirm bounded timeouts, counted loss/silence, and fault thresholds.
- [ ] Do not mark this task complete from serial logs alone; electrical or captured-PCM evidence is required.

---

## Task HW-08 — Simultaneous A2DP plus HFP microphone gate

- [ ] Start known continuous A2DP playback.
- [ ] Start HFP microphone audio.
- [ ] Record whether the target peer keeps, suspends, or stops A2DP.
- [ ] Record exact A2DP, HFP, mode, and policy events.
- [ ] Verify microphone PCM continues through I2S0 when the selected mode claims it should.
- [ ] Verify I2S1 and playback counters remain stable.
- [ ] Confirm strict `A2DP_MIC` behavior either works or reports explicit incompatibility.
- [ ] Confirm `AUTO` transitions are explicit.
- [ ] Confirm no silent fallback.
- [ ] Do not claim operational HFP speaker downlink; FD-18 and FD-19 remain future work.

---

## Task HW-09 — Failure and recovery sampling

At minimum, sample these practical recovery cases:

- [ ] Remote Bluetooth peer power-off during A2DP.
- [ ] Remote Bluetooth peer power-off during HFP SLC.
- [ ] Remote Bluetooth peer power-off during SCO.
- [ ] Reconnect after remote power cycle.
- [ ] Repeated start while already starting/running.
- [ ] Repeated stop while already stopping/stopped.
- [ ] Wrong-peer command attempt when a different peer owns the session.
- [ ] Receiver absent or stalled on I2S0.
- [ ] Serial transport interruption where safe to test.
- [ ] Warm reset after a clean stop.

For every failure:

- [ ] Record exact error.
- [ ] Record state before and after.
- [ ] Record quarantine/fault status.
- [ ] Confirm no hidden retry loop.
- [ ] Confirm no fake success.
- [ ] Confirm recovery either succeeds explicitly or requires an explicit reboot.

---

## Task HW-10 — Runtime resource and soak evidence

Minimum resource checkpoints:

- [ ] Boot idle.
- [ ] A2DP connected and playing.
- [ ] HFP SLC connected.
- [ ] CVSD SCO active.
- [ ] Simultaneous A2DP/HFP microphone mode.
- [ ] Post-stop/post-disconnect.

Record:

- [ ] Current free internal heap.
- [ ] Lifetime minimum free heap.
- [ ] Largest free internal block.
- [ ] Minimum stack margin for every affected task.
- [ ] HFP callback p99 duration.
- [ ] HFP callback maximum duration.
- [ ] Callback overlap count.
- [ ] Ring overflow and underflow counts.
- [ ] I2S timeout and short-write counts.
- [ ] Inserted silence and lost-byte counts.
- [ ] Health-report failure count.
- [ ] Panic, watchdog, brownout, and reset count.

Acceptance thresholds from the maintained full-duplex TODO:

- [ ] Minimum internal heap at least 32 KiB or explicit reviewed exception.
- [ ] Largest internal block at least 16 KiB or explicit reviewed exception.
- [ ] Every affected task has measured stack margin.
- [ ] Callback p99 below 500 microseconds or explicit reviewed exception.
- [ ] Callback maximum below 2 milliseconds or explicit reviewed exception.
- [ ] No persistent heap loss over repeated start/stop cycles.
- [ ] Run a thirty-minute simultaneous-mode soak when the physical setup supports it.
- [ ] Zero panic, watchdog, brownout, reboot, and sustained unaccounted-loss failures.

Do not turn a failed threshold into a pass by deleting or resetting counters before recording them.

---

## Task HW-11 — Hardware-result handling

### When all required hardware tests pass

- [ ] Preserve complete logs and measurements.
- [ ] Update only the hardware-pending checkboxes supported by actual evidence.
- [ ] Record exact board, wiring, peers, codec, test duration, and measurement tools.
- [ ] Append the hardware evidence to this TODO's closeout section.
- [ ] Update `memory.md` with an actual timestamp.
- [ ] Commit documentation separately from any source fix when practical.

### When a hardware test fails

- [ ] Stop and classify the failure before changing code.
- [ ] Preserve the first complete failure log.
- [ ] Determine whether the failure is:
  - [ ] wiring/power/port;
  - [ ] configuration;
  - [ ] compile/target mismatch;
  - [ ] Bluetooth interoperability;
  - [ ] timing/resource;
  - [ ] production logic;
  - [ ] unsupported future capability.
- [ ] Do not add a fallback merely to make the device appear functional.
- [ ] Do not suppress the failing log or counter.
- [ ] For a production defect, add a deterministic host regression test where possible before changing production code.
- [ ] Re-run all software gates affected by the fix.
- [ ] Obtain new same-head host and compile-only CI evidence before reflashing.
- [ ] Minimize reflashes; each reflash must correspond to a reviewed candidate change.

---

# PART D — MERGE READINESS AND LATER CMAKE CLEANUP

## Task MERGE-00 — Decide whether the feature branch is stable enough to merge

The branch is merge-ready only when:

- [ ] All required software tasks in Part A pass.
- [ ] Both required GitHub Actions workflows pass on one final SHA.
- [ ] No P0/P1 silent failure remains in the A2DP binding lifecycle.
- [ ] No stale complete-host CMake target prevents clean configure.
- [ ] The ESP-IDF v5.5.1 compile-only build passes with acceptable headroom.
- [ ] Hardware results are recorded accurately.
- [ ] Any skipped hardware gate remains explicitly open.
- [ ] No unsupported FD-18/FD-19 capability is claimed.
- [ ] Documentation distinguishes software-stable from hardware-complete.

A hardware interoperability limitation may remain open without blocking a software merge only when the repository owner explicitly accepts it and the limitation is visible rather than silently bypassed.

---

## Task CMAKE-LATER-00 — Defer broad host CMake cleanup until after stabilization

Do not execute the broad CMake cleanup as part of this TODO's stabilization commits.

After the feature branch is stable and merged into `master`, create a separate, explicit CMake modularization plan that:

- [ ] Starts from one green unified master baseline.
- [ ] Captures `cmake --build --target help` inventory.
- [ ] Captures `ctest --show-only=json-v1` inventory.
- [ ] Mechanically splits the approximately 1,700-line host CMake file by subsystem before deduplicating behavior.
- [ ] Preserves target names, source lists, mocks, definitions, libraries, and CTest registrations during the mechanical split.
- [ ] Introduces helpers and shared source sets only in a later stage.
- [ ] Compares pre/post target and CTest inventories after every stage.
- [ ] Runs complete host CI and device compile-only CI after each material stage.
- [ ] Never uses `if(EXISTS)` or quiet target omission to hide graph drift.

This later cleanup is expected to require approximately 16–33 active engineering hours. It is not required to finish the immediate feature-branch hardware handoff.

---

## 5. Estimated remaining effort

These estimates assume no newly exposed production defect:

| Work | Active engineering estimate |
|---|---:|
| Remove stale complete-host target and prove clean graph | 0.5–2 hours |
| Fix quiet A2DP lock-failure paths and add tests | 1–2 hours |
| Add initialization rollback-finalization tests | 1–3 hours |
| Run/fix focused and complete host validation | 1–3 hours |
| Run/fix compile-only and same-head CI | 1–3 hours |
| Reconcile documentation and create hardware handoff | 0.5–1 hour |
| **Software-only total** | **4–10 active hours** |
| Claude Code build/flash/basic hardware validation | 0.5–1.5 hours |
| Full I2S, Bluetooth recovery, resources, and soak | 1–4+ elapsed hours |

The software estimate is intentionally wider than earlier conversational estimates because this TODO now explicitly includes the two remaining silent lock-failure paths and complete evidence/documentation work.

---

## 6. Final definition of done for this TODO

### Software handoff definition of done

- [ ] Complete host CMake graph clean-configures from an empty build directory.
- [ ] No retired test is restored through a placeholder or fake implementation.
- [ ] All A2DP binding lifecycle lock/reset failures are visible.
- [ ] Init rollback finalization has deterministic production-path tests.
- [ ] Late terminal, unbound START, wrong-peer, stale-handle, and cross-session tests are exact.
- [ ] All focused ASan/UBSan runners pass.
- [ ] Full CTest passes.
- [ ] Python tests and changed-file lint pass.
- [ ] ESP-IDF v5.5.1 compile-only build passes.
- [ ] Host and device compile-only GitHub Actions pass on the same SHA.
- [ ] Firmware size and partition headroom are recorded.
- [ ] Existing TODOs are reconciled honestly.
- [ ] One exact SHA is frozen for Claude Code.
- [ ] Claude Code receives exact commands, expected evidence, and stop conditions.

### Hardware handoff definition of done

- [ ] Live flash authorization is confirmed.
- [ ] Exact candidate builds and flashes successfully.
- [ ] Cold boot and warm reset are clean.
- [ ] A2DP lifecycle behaves safely across repeated cycles and reconnects.
- [ ] HFP SLC and SCO/CVSD behavior is recorded.
- [ ] I2S0 electrical and PCM behavior is recorded when the required equipment is present.
- [ ] Simultaneous A2DP/HFP microphone behavior is recorded.
- [ ] Failure/recovery sampling is completed.
- [ ] Resource and soak evidence is recorded or explicitly marked pending.
- [ ] No unsupported capability or skipped test is represented as passing.
- [ ] Any hardware-discovered source fix receives regression coverage and fresh same-head CI.

---

## 7. Closeout record

Complete this section only after the work is executed.

```text
STARTING_SHA=5edbd8386c9c5ff5e21fd01a4b728abf6dbe175a
FINAL_SOFTWARE_SHA=
FINAL_HARDWARE_SHA=
HOST_WORKFLOW_RUN_ID=
HOST_JOB_ID=
HOST_RESULT=
CTEST_TOTAL=
CTEST_PASSED=
FOCUSED_SANITIZER_RESULT=
PYTEST_COUNT=
PYTEST_RESULT=
LINT_RESULT=
DEVICE_BUILD_WORKFLOW_RUN_ID=
DEVICE_BUILD_JOB_ID=
DEVICE_BUILD_RESULT=
ESP_IDF_VERSION=
TARGET=
FIRMWARE_SIZE_BYTES=
PARTITION_SIZE_BYTES=
PARTITION_HEADROOM_BYTES=
PARTITION_HEADROOM_PERCENT=
HARDWARE_FLASH_AUTHORIZED=
SERIAL_PORT=
BOARD_OR_MODULE=
FLASH_RESULT=
BOOT_RESULT=
A2DP_RESULT=
HFP_SLC_RESULT=
HFP_AUDIO_RESULT=
I2S0_RESULT=
SIMULTANEOUS_MODE_RESULT=
RECOVERY_RESULT=
SOAK_RESULT=
MIN_FREE_HEAP_BYTES=
LARGEST_FREE_BLOCK_BYTES=
CALLBACK_P99_US=
CALLBACK_MAX_US=
PANIC_OR_RESET_COUNT=
REMAINING_HARDWARE_ITEMS=
KNOWN_LIMITATIONS=
```
