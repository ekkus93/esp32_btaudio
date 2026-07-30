# ESP32 Bluetooth Audio — Duplex Stabilization Review Fixes TODO

**Repository:** `ekkus93/esp32_btaudio`  
**Branch:** `feature/esp-bt-audio-duplex`  
**Project:** `esp_bt_audio_source/`  
**Created:** 2026-07-30  
**Purpose:** Implement the fixes identified by the ChatGPT review of `esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md` and the current feature-branch code.

---

## 0. Non-goals and safety boundaries

This TODO is a software/docs follow-up for review findings only. It must not turn into a broad refactor.

- [ ] Do **not** flash ESP32 hardware as part of this TODO.
- [ ] Do **not** perform broad host CMake modularization.
- [ ] Do **not** clean up the full Python lint backlog here.
- [ ] Do **not** implement FD-18 or FD-19 HFP speaker downlink work here.
- [ ] Do **not** add fallback behavior to make hardware appear functional.
- [ ] Do **not** suppress logs, counters, or CI failures to obtain green checks.
- [ ] Do **not** mark physical hardware gates as passed without physical evidence.

Expected final state:

- HFP command regression runner is enforced by host CI.
- Original stabilization TODO points clearly to the closeout/fix evidence.
- Remaining diagnostic-recording failures are visible.
- A2DP data callback audio-processor failures are observable without log spam.
- Disconnect-clear precedence semantics are explicitly coded and documented.
- Host and device CI pass on one exact final SHA.

---

# PART A — BASELINE AND BRANCH DISCIPLINE

## Task FIX-00 — Reconfirm branch and starting state

- [ ] Run:

```bash
git fetch origin
git checkout feature/esp-bt-audio-duplex
git pull --ff-only
git status --short
git rev-parse HEAD
```

- [ ] Record the starting SHA.
- [ ] Confirm the working tree is clean before changes.
- [ ] Confirm no one else advanced the branch between read and write operations.
- [ ] Read these files before editing:
  - [ ] `CLAUDE.md`
  - [ ] `esp_bt_audio_source/CLAUDE.md`
  - [ ] `esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md`
  - [ ] `esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_SOFTWARE_CLOSEOUT_AND_CLAUDE_DEVICE_HANDOFF_2026-07-29.md`
  - [ ] this TODO file

**Acceptance condition:** The exact starting SHA and clean worktree state are recorded before implementation.

---

# PART B — ENFORCE THE MISSING HFP COMMAND REGRESSION GATE

## Task FIX-01 — Add `run_bt_hfp_commands_test.sh` to maintained host CI

Review finding: `STAB-05` explicitly required `bash esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh`, but `.github/workflows/ci-host-tests.yml` did not run it.

### FIX-01.1 — Inspect the runner before wiring it into CI

- [ ] Open:

```text
esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh
```

- [ ] Confirm it uses `set -euo pipefail`.
- [ ] Confirm it compiles with ASan/UBSan flags.
- [ ] Confirm it runs the resulting binary without `|| true`.
- [ ] Confirm it writes logs under a disposable build subdirectory.
- [ ] Confirm it does not delete unrelated user files.
- [ ] Confirm it covers the FD-11/FD-16/integrity command cases currently linked by the runner.

Expected runner characteristics:

```bash
set -euo pipefail
...
-fsanitize=address,undefined
...
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${binary}"
```

### FIX-01.2 — Add an enforced host workflow step

Edit:

```text
.github/workflows/ci-host-tests.yml
```

Add this step after the existing FD-16 duplex-policy sanitizer step and before the A2DP lifecycle sanitizer step:

```yaml
      - name: Run HFP command sanitizer tests
        run: bash esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh
```

Requirements:

- [ ] The step must be an enforced CI step, not `if: always()` and not informational.
- [ ] Do not wrap it in `|| true`.
- [ ] Do not redirect failures away from GitHub Actions.
- [ ] Do not weaken any existing sanitizer step.
- [ ] Keep the existing artifact upload behavior so its `compile.log` and `test.log` are included by the `*_compile.log` / `*_test.log` artifact globs when applicable.

### FIX-01.3 — Verify command-runner logs are captured

The runner currently writes logs under:

```text
esp_bt_audio_source/test/host_test/build_host_tests/bt_hfp_commands/
```

The host artifact glob currently includes:

```yaml
esp_bt_audio_source/test/host_test/build_host_tests/*_compile.log
esp_bt_audio_source/test/host_test/build_host_tests/*_test.log
```

This does **not** necessarily catch nested logs named `compile.log` and `test.log` under `bt_hfp_commands/`.

Choose one of these fixes:

Option A, update artifact globs:

```yaml
esp_bt_audio_source/test/host_test/build_host_tests/**/*.log
```

Option B, update the runner to also tee or copy logs to top-level names:

```bash
cp "${compile_log}" "${build_root}/bt_hfp_commands_compile.log"
cp "${test_log}" "${build_root}/bt_hfp_commands_test.log"
```

Requirements:

- [ ] The command runner's compile and test logs must be uploaded on both success and failure.
- [ ] Do not remove existing artifacts.
- [ ] Do not make artifact upload failure hide a test failure.

**Acceptance condition:** Host CI enforces the HFP command sanitizer runner, and the runner's logs are available in the host evidence artifact.

---

# PART C — MAKE REMAINING DIAGNOSTIC RECORDING FAILURES VISIBLE

## Task FIX-02 — Surface stale-operation telemetry recording failures

Review finding: `record_rejected_bound_event()` and `record_rejected_unbound_event()` currently discard the return from `bt_duplex_record_stale_operation_event()`. The state mutation path is fail-closed, but telemetry failure is silent.

### FIX-02.1 — Change `record_rejected_bound_event()` to log/count recorder failure

Edit:

```text
esp_bt_audio_source/components/bt_manager/bt_events_a2dp.c
```

Current pattern to fix:

```c
static void record_rejected_bound_event(uint32_t generation,
                                        const char *event_peer)
{
    if (generation != 0U && event_peer != NULL) {
        (void)bt_duplex_record_stale_operation_event(generation, event_peer);
    }
}
```

Replace with behavior equivalent to:

```c
static esp_err_t record_rejected_bound_event(uint32_t generation,
                                             const char *event_peer,
                                             const char *reason)
{
    if (generation == 0U || event_peer == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = bt_duplex_record_stale_operation_event(
        generation, event_peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "A2DP stale-operation telemetry failed: peer=%s generation=%u reason=%s error=%s",
                 event_peer,
                 (unsigned)generation,
                 reason != NULL ? reason : "UNKNOWN",
                 esp_err_to_name(err));
    }
    return err;
}
```

Adjust the exact signature if needed, but preserve these requirements:

- [ ] The production state rejection remains authoritative even when telemetry recording fails.
- [ ] The exact telemetry error is logged.
- [ ] The log includes peer, generation, and reason.
- [ ] No fallback state mutation is attempted.
- [ ] The function should return `esp_err_t` so tests can assert behavior through a UNIT_TEST observation, or it should update a test-visible error field under `UNIT_TEST`.

### FIX-02.2 — Change `record_rejected_unbound_event()` similarly

Current pattern to fix:

```c
static void record_rejected_unbound_event(const char *event_peer)
{
    bt_hfp_manager_status_t status;
    if (event_peer != NULL &&
        bt_manager_hfp_get_status(&status) == ESP_OK &&
        status.duplex.peer_valid &&
        status.duplex.session_generation != 0U) {
        (void)bt_duplex_record_stale_operation_event(
            status.duplex.session_generation, event_peer);
    }
}
```

Requirements:

- [ ] If `bt_manager_hfp_get_status()` fails, log or test-capture the exact status error unless this path is deliberately non-reportable and documented.
- [ ] If no authoritative generation exists, return/report `ESP_ERR_NOT_FOUND` without logging as an error.
- [ ] If `bt_duplex_record_stale_operation_event()` fails, log peer, generation, reason, and exact error.
- [ ] Do not change the A2DP event handler's public `void` callback signatures.
- [ ] Do not let telemetry failure mutate `bt_ctx.audio_playing` or the active binding.

### FIX-02.3 — Add UNIT_TEST observability

Add test-only observations in `bt_events_a2dp.c` and declarations in:

```text
esp_bt_audio_source/components/bt_manager/include/bt_events_a2dp.h
```

Suggested API:

```c
#ifdef UNIT_TEST
void bt_events_a2dp_test_reset_telemetry_errors(void);
esp_err_t bt_events_a2dp_test_get_last_stale_record_error(void);
#endif
```

Requirements:

- [ ] Reset helper clears only test-visible observation state.
- [ ] Getter returns the exact last telemetry-recorder error.
- [ ] No production behavior depends on the UNIT_TEST field.

### FIX-02.4 — Add focused tests

Extend or add a focused test file, preferably:

```text
esp_bt_audio_source/test/host_test/test_a2dp_secondary_failures_exact.c
```

Add cases for:

- [ ] Wrong-peer event where `bt_duplex_record_stale_operation_event()` is forced to return a distinctive error.
- [ ] Same-peer stale-handle event where the recorder fails.
- [ ] Unbound rejection path where HFP status lookup succeeds but stale-operation recording fails.
- [ ] HFP status lookup failure in `record_rejected_unbound_event()` if the implementation reports it.

Required assertions:

- [ ] Active binding remains byte-for-byte unchanged.
- [ ] `bt_ctx.audio_playing` remains correct.
- [ ] Policy callback count does not increase for rejected stale events.
- [ ] The exact recorder/status error is visible.
- [ ] The primary event rejection remains authoritative.

### FIX-02.5 — Add mock support for recorder failure

Edit:

```text
esp_bt_audio_source/test/host_test/mocks/mock_audio_and_btstate.c
```

Add helpers like:

```c
void bt_manager_test_set_stale_operation_record_result(esp_err_t result);
esp_err_t bt_manager_test_get_stale_operation_record_result(void);
```

Requirements:

- [ ] Default recorder result remains `ESP_OK`.
- [ ] `bt_manager_test_reset_btstate_mock()` resets the forced result.
- [ ] The weak `bt_duplex_record_stale_operation_event()` returns the configured result after recording call count inputs.
- [ ] Existing tests continue to pass without changes unless they need to reset new state.

**Acceptance condition:** Stale-operation telemetry failures are visible in logs/tests and cannot be mistaken for fully recorded diagnostics.

---

# PART D — MAKE A2DP DATA CALLBACK AUDIO-PROCESSOR FAILURES OBSERVABLE

## Task FIX-03 — Add bounded observability for `audio_processor_read()` failure

Review finding: `bt_events_a2dp_data_callback()` returns zero bytes when `audio_processor_read()` fails, but the error is not logged or counted.

Current code:

```c
size_t bytes_read = 0;
esp_err_t ret = audio_processor_read(buf, req, &bytes_read);
if (ret != ESP_OK) {
    return 0;
}

return (int32_t)bytes_read;
```

Returning `0` may be the right callback behavior, but the failure must not be invisible.

### FIX-03.1 — Add a small diagnostic state object

In `bt_events_a2dp.c`, under ESP-platform-compatible code, add static counters guarded by the existing manager lock **only if safe from callback timing**. If locking from the A2DP data callback is inappropriate, use atomics or a minimal callback-safe counter strategy and document it.

Recommended low-risk design:

```c
typedef struct {
    uint64_t audio_read_failures;
    esp_err_t last_audio_read_error;
    uint32_t suppressed_audio_read_error_logs;
} a2dp_data_diagnostics_t;

static a2dp_data_diagnostics_t s_a2dp_data_diag;
```

Requirements:

- [ ] Count every `audio_processor_read()` failure.
- [ ] Preserve the exact last `esp_err_t`.
- [ ] Do not allocate memory from the audio callback.
- [ ] Do not block indefinitely from the audio callback.
- [ ] Do not log every callback failure unconditionally.

### FIX-03.2 — Add rate-limited logging or suppression counter

Implement a callback-safe, bounded diagnostic pattern. Example:

```c
if (ret != ESP_OK) {
    a2dp_data_record_audio_read_failure(ret);
    return 0;
}
```

The recorder should:

- [ ] Log the first failure after a reset.
- [ ] Log periodic failures, for example every 64th or 256th failure.
- [ ] Count suppressed logs.
- [ ] Include requested byte count and exact error in the log.
- [ ] Avoid unbounded log spam from the Bluetooth audio callback.

Example log message:

```c
ESP_LOGE(TAG,
         "A2DP data callback audio_processor_read failed: requested=%u error=%s failures=%llu suppressed_logs=%u",
         (unsigned)requested,
         esp_err_to_name(err),
         (unsigned long long)s_a2dp_data_diag.audio_read_failures,
         (unsigned)s_a2dp_data_diag.suppressed_audio_read_error_logs);
```

### FIX-03.3 — Expose diagnostics through tests

Add UNIT_TEST helper declarations in `bt_events_a2dp.h`, for example:

```c
#ifdef UNIT_TEST
typedef struct {
    uint64_t audio_read_failures;
    esp_err_t last_audio_read_error;
    uint32_t suppressed_audio_read_error_logs;
} bt_events_a2dp_data_diag_snapshot_t;

void bt_events_a2dp_test_reset_data_diagnostics(void);
esp_err_t bt_events_a2dp_test_get_data_diagnostics(
    bt_events_a2dp_data_diag_snapshot_t *out);
#endif
```

Requirements:

- [ ] Test helper must not exist in production ABI.
- [ ] Snapshot must be safe and deterministic in host tests.

### FIX-03.4 — Add host tests for callback failure observability

Add a focused host test, or extend an existing A2DP data callback test if present.

Test cases:

- [ ] `audio_processor_read()` returns `ESP_OK` and nonzero bytes: callback returns the byte count, no failure count increments.
- [ ] `audio_processor_read()` returns a distinctive error: callback returns `0`, failure counter increments exactly once, last error matches.
- [ ] Repeated failures increment the counter without unbounded logging.
- [ ] NULL buffer and negative length behavior remain unchanged.
- [ ] Zero-length request behavior remains unchanged.

Mock requirements:

- [ ] Add an `audio_processor_read()` test stub that can return a configured error and byte count.
- [ ] Reset the stub state between tests.
- [ ] Do not use sleeps or timing assumptions.

**Acceptance condition:** An `audio_processor_read()` failure in the A2DP data callback is visible through deterministic diagnostics while the callback still returns a safe byte count to the Bluetooth stack.

---

# PART E — CLARIFY DISCONNECT CLEAR ERROR PRECEDENCE

## Task FIX-04 — Resolve `ESP_ERR_NOT_FOUND` vs clear-error precedence explicitly

Review finding: The disconnect policy path currently replaces `err` with `clear_err` when `err` is `ESP_OK` or `ESP_ERR_NOT_FOUND` and `clear_err != ESP_OK`. This is probably reasonable because `ESP_ERR_NOT_FOUND` is treated as an idempotent non-primary result, but the TODO wording said not to replace prior policy errors.

Current code pattern:

```c
if ((err == ESP_OK || err == ESP_ERR_NOT_FOUND) &&
    clear_err != ESP_OK) {
    err = clear_err;
}
```

Choose and implement exactly one option.

### Option A — Document `ESP_ERR_NOT_FOUND` as non-primary/idempotent

Use this if the current behavior is intentional.

- [ ] Add a concise code comment above the condition explaining that `ESP_ERR_NOT_FOUND` from disconnect policy means no active duplex session, not a primary failure.
- [ ] Keep replacing `ESP_ERR_NOT_FOUND` with a real clear failure so a failed clear cannot look clean.
- [ ] Add/adjust a test that proves:
  - [ ] prior `ESP_ERR_NOT_FOUND` plus `clear_err == ESP_OK` reports no error;
  - [ ] prior `ESP_ERR_NOT_FOUND` plus `clear_err == ESP_ERR_TIMEOUT` reports/exposes `ESP_ERR_TIMEOUT`;
  - [ ] prior hard policy failure, for example `ESP_ERR_INVALID_STATE`, is not replaced by clear failure.

Suggested comment:

```c
/* ESP_ERR_NOT_FOUND here is an idempotent "no duplex session" result from the
 * disconnect policy path, not a primary failure. A real clear failure is more
 * actionable and must remain visible. Hard policy errors remain authoritative. */
```

### Option B — Preserve every non-OK prior result literally

Use this if the TODO wording should be enforced strictly.

- [ ] Change the condition to:

```c
if (err == ESP_OK && clear_err != ESP_OK) {
    err = clear_err;
}
```

- [ ] Keep logging `clear_err` even when it does not become the returned/reportable error.
- [ ] Add/adjust a test proving clear failure is visible through `s_test_last_binding_clear_error` and logs while the hard prior error remains authoritative.

### FIX-04.1 — Update the test names to encode the chosen contract

Test names should make the semantics obvious. Examples:

```c
test_disconnect_not_found_policy_allows_clear_error_to_surface(void)
test_disconnect_hard_policy_error_is_not_replaced_by_clear_error(void)
```

or:

```c
test_disconnect_any_prior_policy_error_remains_authoritative(void)
```

**Acceptance condition:** The code and tests make the disconnect-clear precedence rule unambiguous, and no future reviewer has to infer intent from the condition alone.

---

# PART F — RECONCILE THE ORIGINAL TODO AND CLOSEOUT DOCUMENTATION

## Task FIX-05 — Update the original stabilization TODO with a closeout pointer

Review finding: `STAB-09` asked for a closeout section in `ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md`, but the actual closeout was placed in a separate file.

Edit:

```text
esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_AND_DEVICE_HANDOFF_TODO_2026-07-29.md
```

Add a short section near the top, after the title or before Part A:

```markdown
# Software closeout status

The software-only stabilization work was closed out in:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_SOFTWARE_CLOSEOUT_AND_CLAUDE_DEVICE_HANDOFF_2026-07-29.md`

A later review identified additional software follow-up tasks in:

`esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_REVIEW_FIXES_TODO_2026-07-30.md`

Do not treat unchecked software boxes below as the sole source of current status without reading those closeout/follow-up files. Physical hardware tasks remain open until real ESP32-WROOM-32 evidence is captured.
```

Requirements:

- [ ] Do not mechanically mark checkboxes complete unless each exact item is verified and cited in the closeout/fix evidence.
- [ ] Do not mark hardware tasks complete.
- [ ] Do not obscure the fact that this review TODO exists.
- [ ] Preserve all original hardware instructions.

## Task FIX-06 — Update the software closeout after fixes are complete

Edit:

```text
esp_bt_audio_source/docs/ESP_BT_AUDIO_DUPLEX_STABILIZATION_SOFTWARE_CLOSEOUT_AND_CLAUDE_DEVICE_HANDOFF_2026-07-29.md
```

Add a new section:

```markdown
## Review follow-up closeout — 2026-07-30

The review follow-up TODO was completed in commit `<FINAL_SHA>`.

Resolved items:

- HFP command sanitizer runner is enforced by host CI.
- Stale-operation telemetry recorder failures are visible.
- A2DP data callback audio-processor read failures are counted and rate-limited.
- Disconnect-clear precedence semantics are documented and tested.
- Original stabilization TODO points to the closeout and follow-up evidence.

Validation:

- Host workflow run: `<RUN_ID>` / job `<JOB_ID>` / result `<RESULT>`
- Device workflow run: `<RUN_ID>` / job `<JOB_ID>` / result `<RESULT>`
- Final SHA: `<FINAL_SHA>`
- Flash executed: No
```

Requirements:

- [ ] Fill in real values only after CI completes.
- [ ] Do not claim hardware validation.
- [ ] Preserve the FD-18/FD-19 limitation.
- [ ] Preserve the full-tree Python lint backlog note unless it has separately been fixed and verified.

**Acceptance condition:** Future agents can read the original TODO and immediately find the actual software closeout and this follow-up fix status.

---

# PART G — TEST AND CI VALIDATION

## Task FIX-07 — Run focused local/CI-equivalent gates

Run from repository root after implementation:

```bash
bash esp_bt_audio_source/tools/run_bt_hfp_commands_test.sh
bash esp_bt_audio_source/tools/run_bt_a2dp_binding_lifecycle_test.sh
```

Also run any new focused runner added for A2DP data callback diagnostics.

For each runner, record:

- [ ] command;
- [ ] binary name;
- [ ] Unity test count;
- [ ] pass/fail count;
- [ ] ASan result;
- [ ] UBSan result;
- [ ] log path.

Failure handling:

- [ ] Preserve the first complete failure log.
- [ ] Fix root cause, not the test expectation, unless the test is wrong and the replacement contract is documented.
- [ ] Do not skip a failing test to keep the branch moving.

## Task FIX-08 — Run complete host validation

Run:

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

Record:

- [ ] configured CTest count;
- [ ] passed count;
- [ ] failed count;
- [ ] skipped/disabled count;
- [ ] total time;
- [ ] exact failing output if any.

Requirements:

- [ ] No generated build directory, cache, sanitizer binary, or log file should be committed unless explicitly documented as evidence.
- [ ] Do not remove a CTest registration merely to get green CI.

## Task FIX-09 — Run Python validation only where relevant

This TODO should not touch Python tooling unless artifact/glob or workflow changes require it.

- [ ] If no Python files changed, record `No changed Python files`.
- [ ] If Python files changed, run strict flake8 on changed Python files.
- [ ] Run:

```bash
python3 -m pytest -q esp_bt_audio_source/tools/tests
```

- [ ] Keep full-tree flake8 backlog informational unless this work edits a backlogged file.
- [ ] Do not broaden `.flake8` ignores.

## Task FIX-10 — Run device compile-only validation

Run locally when ESP-IDF v5.5.1 is available, and require GitHub Actions validation either way:

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
cd esp_bt_audio_source
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py size
```

Requirements:

- [ ] Do not flash hardware.
- [ ] Confirm no unexplained tracked `sdkconfig` churn.
- [ ] Record firmware binary size.
- [ ] Record smallest app partition size.
- [ ] Record headroom bytes and percentage.
- [ ] Preserve HFP AG / HCI data-path configuration.

## Task FIX-11 — Obtain same-SHA GitHub Actions evidence

Required workflows:

```text
CI — host tests (optimized)
CI — device build (compile only)
```

Required status issue for this branch:

```text
Issue #4: CI Status: Host + Device — feature/esp-bt-audio-duplex
```

Process:

- [ ] Push all fixes to `feature/esp-bt-audio-duplex`.
- [ ] Record final SHA.
- [ ] Read issue #4 after workflows start.
- [ ] Confirm issue #4 reports the same final SHA.
- [ ] Confirm host workflow run ID and device workflow run ID.
- [ ] Confirm every job is completed successfully.
- [ ] Confirm problem steps are `None`.
- [ ] Confirm host and device artifacts are present.
- [ ] Fetch failed job logs immediately if anything fails.
- [ ] Repeat until both workflows pass on one exact final SHA.

Required final evidence block:

```text
FINAL_SHA=
HOST_RUN_ID=
HOST_JOB_ID=
HOST_RESULT=
HOST_CTEST_COUNT=
HOST_ARTIFACTS=
DEVICE_RUN_ID=
DEVICE_JOB_ID=
DEVICE_RESULT=
ESP_IDF_VERSION=
FIRMWARE_SIZE=
PARTITION_SIZE=
PARTITION_HEADROOM_BYTES=
PARTITION_HEADROOM_PERCENT=
FLASH_EXECUTED=No
PYTHON_CHANGED_FILES_RESULT=
LEGACY_FULL_TREE_FLAKE8_BACKLOG=
```

**Acceptance condition:** Host and device workflows both pass on one exact final SHA, and issue #4 publishes the run/job/artifact identifiers for that SHA.

---

# PART H — FINAL REVIEW CHECKLIST

## Task FIX-12 — Review final code for quiet failures and fake success

Before declaring this TODO complete, inspect the final diff and answer each item explicitly:

- [ ] Does any new code catch an error and return success?
- [ ] Does any new code add `|| true` around a real test/build command?
- [ ] Does any new code discard an `esp_err_t` from a safety/diagnostic path without logging, counting, or documenting why?
- [ ] Does any new test replace production logic with a no-op fake in a way that invalidates the test?
- [ ] Does any new workflow step run as informational when it is supposed to be enforced?
- [ ] Does any new artifact upload hide or overwrite the real failure signal?
- [ ] Does any documentation claim hardware validation that was not performed?
- [ ] Does any documentation claim global Python lint cleanliness despite the known backlog?

## Task FIX-13 — Final closeout comment for the implementer

When complete, report:

```text
Implemented review-fix TODO:
- FIX-01 HFP command CI gate: PASS/FAIL
- FIX-02 stale telemetry visibility: PASS/FAIL
- FIX-03 A2DP data callback diagnostics: PASS/FAIL
- FIX-04 disconnect clear precedence: PASS/FAIL, option A or B
- FIX-05 original TODO pointer: PASS/FAIL
- FIX-06 closeout update: PASS/FAIL
- FIX-07 focused gates: PASS/FAIL
- FIX-08 complete host validation: PASS/FAIL
- FIX-09 Python validation: PASS/FAIL
- FIX-10 device compile-only validation: PASS/FAIL
- FIX-11 same-SHA CI evidence: PASS/FAIL

Final SHA:
Host run/job:
Device run/job:
Hardware flashed: No
Remaining blockers:
```

**Final acceptance condition:** The branch has no known unaddressed software issue from the 2026-07-30 review except explicitly deferred maintenance or physical hardware validation.
