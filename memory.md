## Current Focus
- Stabilize `test_app` Unity harness so all suites auto-run once with accurate summaries.
- Capture canonical run instructions and runtime expectations in `README.md`.

## Key Findings
- Multiple Unity entry points (`test_app_main.c`, `main.c`, auto-runner in `test_app2`) currently duplicate setup and prolong runtime.
- `bt_pairing_persistence_test.c` provides a reboot-dependent flow that isn't wired into the main runner yet.
- `main.c` triggers an immediate `esp_restart()` after suites finish, complicating log capture and lengthening total runtime.
- `test_app2` already lists every `RUN_TEST` symbol; consider porting that approach into `test_app` for exhaustive coverage.

## Assumptions & Constraints
- Must keep total `test_app` execution under 15 minutes when driven by `tools/run_unity.py`.
- No sdkconfig, partition, or target changes without explicit approval per repo policy.
- Preserve existing log markers (Unity summary lines) for downstream tooling.
- ESP32 DUT is connected unless noted otherwise; default serial port is `/dev/ttyUSB0` unless explicitly overridden.
- Serial comms stay at 115200 baud, 8N1; avoid tweaking monitor baud unless the user asks.

## Sticky Reference Notes
- **Hardware target:** ESP32-WROOM32 dev kit; UART over `/dev/ttyUSB0` via 3.3V adapter.
- **Helper scripts:** `tools/run_unity.py` manages flash/monitor/summary; logs land in `<project>/build/one_run_unity.log`.
- **Unity suites:** `test_app`, `test_app_audio`, `test_app2`; only `test_app` currently failing (37 total, 26 pass, 11 fail from 2025-10-25 capture).
- **Policy reminders:** Do not touch `sdkconfig`/partitions/targets or introduce new components without explicit go-ahead; keep log markers and component boundaries intact.
- **Documentation split:** `README.md` is user-facing; keep procedural notes here in `memory.md`.
- **Host tests:** Run `ctest -N` alongside `ctest --output-on-failure` to verify every newly added binary is registered; missing registrations are a release blocker and risk hiding regressions behind false green test runs.

## Open Questions
- Decide whether to merge persistence reboot test into main harness or keep as optional suite.
- Confirm if any suites require conditional compilation to stay within time budget.

## Next Steps Snapshot
- Refactor harness to avoid nested `UNITY_BEGIN()` calls and redundant runs.
- Continue tightening documentation/examples as the harness changes.
- Re-run `tools/run_unity.py` to validate runtime and capture canonical log once harness stabilizes.

## Latest captured device Unity run (manual copy)
Timestamp: 2025-10-25  (captured from serial monitor)

Captured summary (trimmed to the important summary lines):

37 Tests 11 Failures 0 Ignored 
FAIL
--- SUMMARY ---
----- UNITY TEST COMPLETE: FAIL -----
-------- BLUETOOTH TEST SUMMARY --------
Tests run    : 37
Tests passed : 26
Tests failed : 11
--------------------------------------

Overall interpretation:
- The Unity firmware completed its test run and printed the `--- SUMMARY ---` block. That means the suites finished running.
- The firmware then printed "*** ENTERING IDLE LOOP - TESTS COMPLETE ***" and remains running in an idle loop; `idf.py monitor` does not exit automatically.
- Actionable data: 37 tests run, 26 passed, 11 failed — success rate 70.3%.

Notes / next action ideas:
- Copy this summary into the issue tracker or test report for triage of failing tests.
- We must either (A) run failing tests in host mode to debug quickly, or (B) instrument/fix the failing tests on-device.
- If you want automation to capture summaries without manual Ctrl+], I can propose a safe one-liner that listens for the `--- SUMMARY ---` marker and then stops the monitor; do you want that added as an optional helper command?
