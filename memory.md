## Current Focus
- Triage the expanding Unity failure set after the latest on-device run.
- Keep `bt_source_stubs.c` tidy after reinstating the internal reset helper.
- Maintain Unity runner output so downstream tooling captures summaries.

## Key Findings
- Unified reset now synchronises mock and stub state; connection-dependent tests progress through pairing successfully.
- Unity rerun (2025-10-27) now reports 37 tests / 37 passed / 0 failed (`test_app/build/one_run_unity.log`).
- Latest `test_app2` Unity run (2025-10-27 via runner) reports 45 tests / 42 passed / 3 failed (`test_app2/build/one_run_unity.log`).
- Remaining failing case: `test_a2dp_paired_devices`; prior connection-state failures are resolved after the disconnect fixes.
- Latest log (2025-10-27 rerun) shows `test_connection_failure_handling` now passes after wiring `bt_disconnect()` through `bt_mock_disconnect()`; both stub and authoritative states drop to disconnected before the wait helper runs.
- Latest log excerpt shows component mock connects fine for streaming tests, but `test_a2dp_paired_devices` still fails after `bt_mock_add_paired_device`, implying the stub-side paired list is not synchronised with the component helper.
- `test_app2` now emits the canonical Unity summary line (`<tests> Tests <failures> Failures <ignored> Ignored`), so `tools/run_unity.py` exits with pass/fail codes instead of falling back to per-test counting.
- 2024-XX-XX: add sync path so component `bt_mock_add_paired_device` notifies test-app mock; `bt_is_device_paired` now sees authoritative paired entry via `bt_source_mock_cache_paired_device` helper.
- Strong definitions in `test_app/main/bt_source_mock.c` override the weak stubs, so changes in `bt_source_stubs.c` (e.g., deferred disconnect visibility) never execute during Unity runs.
- Added `s_defer_disconnect_visibility` and a strong `bt_source_stub_release_disconnect_visibility()` inside `bt_source_mock.c` so deferred disconnect logic now lives in the active mock path.
- `bt_source_stub_reset_state_internal` restored (calls `bt_mock_reset` and clears locals); rebuild now succeeds cleanly with no new warnings.
- Controller enable issue from prior attempts no longer appears after reset fixes.
- Unity harness now runs from a dedicated FreeRTOS task (16 KB stack); stack overflow resolved and high-water mark logged (~12 KB used).
- Latest `test_app` Unity run completes without crashes; device drops into idle loop after summary.

## Assumptions & Constraints
- No sdkconfig, partition, or target changes without explicit approval per repo policy.
- Preserve existing log markers (Unity summary lines) for downstream tooling.
- ESP32 DUT is connected unless noted otherwise; default serial port is `/dev/ttyUSB0`.
- Serial comms stay at 115200 baud, 8N1; avoid tweaking monitor baud unless asked.
- When reporting unit test results, always include counts for tests passed and tests failed by default.

## Sticky Reference Notes
- **Hardware target:** ESP32-WROOM32 dev kit; UART over `/dev/ttyUSB0` via 3.3V adapter.
- **Helper scripts:** `tools/run_unity.py` manages flash/monitor/summary; logs land in `<project>/build/one_run_unity.log`.
- If you don't remember how to run the on-device Unity tests, check `esp_bt_audio_source/README.md` ("Developer tools / Diagnostics" section) for runner invocation and log locations.
- **Unity suites:** `test_app`, `test_app_audio`, `test_app2`; latest `test_app` log shows 37 tests, 36 pass, 1 fail.
- **Policy reminders:** Do not touch `sdkconfig`/partitions/targets or introduce new components without explicit approval; keep log markers and component boundaries intact.
- **Documentation split:** `README.md` is user-facing; keep procedural notes here.
- **Mock config:** `CONFIG_BT_MOCK_TESTING=y`, compiled with `BT_USE_MOCKS` define.
- **Unity runner reminder:** Either `cd esp_bt_audio_source/test_app` before running the helper or pass `--project-root test_app`; running from the production root without the flag flashes the wrong image and produces no Unity output.

## Open Questions
- Diagnose why `test_a2dp_paired_devices` still fails under the mock stack even after connection fixes.
- Assess whether `bt_mock_setup` should seed paired-device state differently for the paired-devices assertion.
- Interpret latest user note "Try Again"—confirm whether to rerun the prior Unity test sequence or revisit recent code edits.

## Next Steps Snapshot
- Confirm rebuild remains warning-free after subsequent edits.
- Deep-dive the remaining Unity failure (`test_a2dp_paired_devices`) and capture repro notes.
- Compare mock vs stub state transitions during paired-device workflows.
- Re-run `tools/run_unity.py` for `test_app` after each iteration to refresh the canonical log.
- 2025-10-27: Chat session restarted; reloaded README.md and memory.md context for continuity.

### test_app2 TODO
- [x] Align `test_app2` Unity output with `test_app` format so `run_unity.py` parses cleanly.
- [ ] Adjust `run_unity.py` logic if needed after output alignment (handle idle loop/markers).
- [ ] Investigate and fix current `test_app2` Unity failures (`test_bt_connect_failure`, `test_bt_connection_info`, `test_bt_connect_timeout`, `test_connection_status_info`).
- [x] Document optimized `test_app2` Unity run steps in `README.md` alongside existing guidance.

- [x] Remove stale references to `s_streaming`/`s_streaming_paused` and stray `reset_device_database()` lines.
- [x] Identify the three failing Unity tests and capture their failure reasons from the log.
- [x] Rebuild `test_app` after cleanup to ensure no compile errors remain.
- [x] Move Unity harness onto dedicated FreeRTOS task with larger stack and confirm overflow disappears.
- [ ] Investigate `test_a2dp_paired_devices` failure (Expected TRUE Was FALSE).
- [x] Inspect `test_app2` Unity entrypoint and runner script to understand missing summary counts.
- [x] Patch `test_app2` to emit the canonical Unity summary line and verify the runner reports status codes.
-    - `test_bluetooth_connection`: ✅ passes after disconnect delegation; retained note for historical context.
-    - Deferred disconnect visibility path in `bt_source_stubs.c` remains available; connection helpers now release it automatically via wait helpers.
-    - `test_connection_failure_handling`: ✅ fixed by delegating disconnect to the component mock; diagnostic log confirms stub/mock both report disconnected prior to the wait helper.
- Latest `tools/run_unity.py --project-root test_app --port /dev/ttyUSB0` (2025-10-27 evening rerun) reports 37 tests / 37 pass / 0 fail after paired-device cache fix (`one_run_unity.log`).
- Unity rerun (2025-10-27) rebuilt successfully after adding `bt_mock_release_disconnect_visibility()` prototype/extern; hardware run now shows 37 tests / 36 pass / 1 fail. Log located at `test_app/build/one_run_unity.log`.
- Active fix focus per user: resolve `test_a2dp_paired_devices`; streaming tests remain green after prior fixes.
- [ ] Cross-check paired-device seeding during setup to ensure mock and stub stay aligned.
- [ ] Confirm no regressions in streaming tests after the connection fixes.

### 2025-11-02 Updates
- [ ] Capture pointer and size diagnostics whenever `bt_mock_get_ssp_passkey` rejects arguments so we can reproduce the `ESP_ERR_INVALID_ARG` return seen on-device.
- [ ] Verify Unity `test_ssp_confirmation_request` still expects `ESP_OK` and confirm buffer size is 16 bytes; adjust implementation if the component mock propagates errors for otherwise valid inputs.
- [ ] Once diagnostics land, rerun `tools/run_unity.py --project-root test_app --port /dev/ttyUSB0` and scrape the log for new `bt_get_ssp_passkey` traces before iterating on pairing test fixes.

## Recent Changes
- Adjusted `bt_reset_for_test()` in `test_app/main/bt_source_mock.c` to reset the stub state before invoking `bt_source_mock_reset_impl()` so scan devices stay seeded after resets.
- 2025-10-27: Verified working tree clean when preparing to commit/push per user request; no staged or unstaged diffs found.
- 2025-10-27: Reran `test_app` via `tools/run_unity.py`; log shows 37/37/0 PASS despite runner exiting with code 3 (monitor error), results captured in `test_app/build/one_run_unity.log`.
- 2025-10-27: Full regression sweep — main app build successful (3% flash headroom warning noted), host_test `ctest` 15/15 pass, `test_app` Unity 37/37/0 pass (monitor exit code 3), `test_app_audio` Unity 26/26/0 pass with runner exit 0.
- 2025-10-27: `test_app2` main updated to call `UNITY_BEGIN/UNITY_END`, emit standard summary, and idle instead of restarting; rebuilt + rerun runner now prints footer but reports real failures (`test_bt_connect_failure`, `test_bt_connection_info`, `test_bt_connect_timeout`, `test_connection_status_info`).
- 2025-10-27: Pending: prepare commit summarizing README doc update, then push once current fixes land and failing Unity tests addressed.

## Latest captured device Unity run (manual copy)
Timestamp: 2025-10-27 (captured via `tools/run_unity.py`)

Captured summary (trimmed to the important summary lines):

37 Tests 1 Failures 0 Ignored 
FAIL
--- SUMMARY ---
----- UNITY TEST COMPLETE: FAIL -----
-------- BLUETOOTH TEST SUMMARY --------
Tests run    : 37
Tests passed : 36
Tests failed : 1
--------------------------------------

Actionable data:
- Regression scope trimmed to paired-device handling (connection and failure-handling cases pass).
- Helper script exits non-zero (code 1) and stores logs at `test_app/build/one_run_unity.log`.
- Latest failing-case lines cluster around the paired-device assertions near `test_main.c:456`.
