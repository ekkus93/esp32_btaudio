## Current Focus
- Triage the expanding Unity failure set after the latest on-device run.
- Keep `bt_source_stubs.c` tidy after reinstating the internal reset helper.
- Maintain Unity runner output so downstream tooling captures summaries.

## Key Findings
- Unified reset now synchronises mock and stub state; connection-dependent tests progress through pairing successfully.
- Unity rerun (2025-10-27) now reports 37 tests / 34 passed / 3 failed (`test_app/build/one_run_unity.log`).
- Remaining failing cases: `test_bluetooth_connection`, `test_connection_failure_handling`, `test_a2dp_paired_devices`.
- `bt_source_stub_reset_state_internal` restored (calls `bt_mock_reset` and clears locals); rebuild now succeeds cleanly with no new warnings.
- Controller enable issue from prior attempts no longer appears after reset fixes.
- Unity harness now runs from a dedicated FreeRTOS task (16 KB stack); stack overflow resolved and high-water mark logged (~12 KB used).
- Latest `test_app` Unity run completes without crashes; device drops into idle loop after summary.

## Assumptions & Constraints
- No sdkconfig, partition, or target changes without explicit approval per repo policy.
- Preserve existing log markers (Unity summary lines) for downstream tooling.
- ESP32 DUT is connected unless noted otherwise; default serial port is `/dev/ttyUSB0`.
- Serial comms stay at 115200 baud, 8N1; avoid tweaking monitor baud unless asked.

## Sticky Reference Notes
- **Hardware target:** ESP32-WROOM32 dev kit; UART over `/dev/ttyUSB0` via 3.3V adapter.
- **Helper scripts:** `tools/run_unity.py` manages flash/monitor/summary; logs land in `<project>/build/one_run_unity.log`.
- If you don't remember how to run the on-device Unity tests, check `esp_bt_audio_source/README.md` ("Developer tools / Diagnostics" section) for runner invocation and log locations.
- **Unity suites:** `test_app`, `test_app_audio`, `test_app2`; latest `test_app` log shows 37 tests, 34 pass, 3 fail.
- **Policy reminders:** Do not touch `sdkconfig`/partitions/targets or introduce new components without explicit approval; keep log markers and component boundaries intact.
- **Documentation split:** `README.md` is user-facing; keep procedural notes here.
- **Mock config:** `CONFIG_BT_MOCK_TESTING=y`, compiled with `BT_USE_MOCKS` define.
- **Unity runner reminder:** Either `cd esp_bt_audio_source/test_app` before running the helper or pass `--project-root test_app`; running from the production root without the flag flashes the wrong image and produces no Unity output.

## Open Questions
- Determine why connection establishment and failure-handling tests still fail under the mock stack.
- Assess whether `bt_mock_setup` should seed paired-device state differently for the paired-devices assertion.

## Next Steps Snapshot
- Confirm rebuild remains warning-free after subsequent edits.
- Deep-dive the three remaining Unity failures; capture repro notes per test.
- Compare mock vs stub state transitions during connection workflows.
- Re-run `tools/run_unity.py` for `test_app` (prior attempt canceled) to refresh the canonical log.

- [x] Remove stale references to `s_streaming`/`s_streaming_paused` and stray `reset_device_database()` lines.
- [x] Identify the three failing Unity tests and capture their failure reasons from the log.
- [x] Rebuild `test_app` after cleanup to ensure no compile errors remain.
- [x] Move Unity harness onto dedicated FreeRTOS task with larger stack and confirm overflow disappears.
- [ ] Investigate `test_bluetooth_connection`, `test_connection_failure_handling`, `test_a2dp_paired_devices` failures (Expected TRUE Was FALSE).
- [ ] Cross-check paired-device seeding during setup to ensure mock and stub stay aligned.
- [ ] Confirm no regressions in streaming tests after the connection fixes.

## Recent Changes
- Adjusted `bt_reset_for_test()` in `test_app/main/bt_source_mock.c` to reset the stub state before invoking `bt_source_mock_reset_impl()` so scan devices stay seeded after resets.

## Latest captured device Unity run (manual copy)
Timestamp: 2025-10-27 (captured via `tools/run_unity.py`)

Captured summary (trimmed to the important summary lines):

37 Tests 3 Failures 0 Ignored 
FAIL
--- SUMMARY ---
----- UNITY TEST COMPLETE: FAIL -----
-------- BLUETOOTH TEST SUMMARY --------
Tests run    : 37
Tests passed : 34
Tests failed : 3
--------------------------------------

Actionable data:
- Regression scope trimmed to connection-focused cases (connection state, failure handling, paired devices).
- Helper script exits non-zero (code 1) and stores logs at `test_app/build/one_run_unity.log`.
- Latest failing-case lines cluster around ~354390–354600 in the log.
