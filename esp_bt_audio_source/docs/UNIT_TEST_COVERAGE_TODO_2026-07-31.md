# Unit Test Coverage TODO

**Created:** 2026-07-31
**Target branch:** `feature/esp-bt-audio-duplex`
**Context:** `/lint-n-test` + a follow-up coverage investigation on 2026-07-31 found that
the full-duplex HFP merge (FD-00 through FD-16) landed without wiring its host-test
suite into the build. 34 test files with real, already-written test cases exist under
`esp_bt_audio_source/test/host_test/` but are not referenced anywhere in
`test/host_test/CMakeLists.txt`, so they never compile or run, and the ~6,900 lines of
production code they cover cross zero host-test coverage measurement. Separately, the
2026-07-31 coverage run (85.0% overall line coverage, 977 host cases) surfaced several
already-tested files sitting well below that average.

**Status rules** (matching the convention used by other `docs/ESP_BT_AUDIO_FULL_DUPLEX_*`
TODOs in this directory):
- `[x]` means the task was done and verified (build succeeded / tests ran green).
- Do not check off a "verify tests pass" item without actually running `ctest`.
- Record the actual pass count next to each suite once it runs for the first time —
  do not assume the counts in this document (taken from static `RUN_TEST`/function
  counts) are exactly what executes until CI confirms it.
- No hardware flashing is required for any task in this document — everything here is
  host-only (`ctest`, no `--port`).

---

## Part A — Wire up the orphaned full-duplex/HFP host-test suites

This is the highest-leverage work in this document: the test code already exists and
appears well-formed (verified: every "runner" file's `RUN_TEST` count matches the sum of
`void test_*(void)` definitions in its paired "`_cases.c`" files, listed per-suite below).
This is registration work, not new test authoring — see Part B for that.

All mocks/stubs these suites `#include` already exist under
`test/host_test/mocks/` and `test/host_test/mocks/include/` (verified present:
`bt_duplex_policy_manager_stub.c/.h`, `bt_hfp_event_command_stub.c/.h`,
`bt_hfp_manager_command_stub.c/.h`, `bt_hfp_manager_dependencies.c/.h`, `mock_uart.c/.h`,
`mock_a2dp.c/.h`, `mock_avrc.c/.h` — the last three are already linked into other, currently
wired-in suites, so they are known-working).

### A0 — Shared prep

- [x] Read `test/host_test/CMakeLists.txt` end-to-end once to confirm the current
      `add_executable` / `target_sources` / `target_link_libraries` / `add_test` pattern
      (see e.g. the `test_commands` block) before adding new blocks, so new entries match
      house style exactly. Confirmed pattern: `add_executable(NAME file.c ...sources...)`,
      `target_link_libraries(NAME unity util_safe_host platform_shim_host [command_interface_host])`,
      `target_compile_definitions(NAME PRIVATE UNIT_TEST)`,
      `add_test(NAME NAME COMMAND $<TARGET_FILE:NAME>)`. The file also defines shared
      OBJECT libraries (`util_safe_host`, `platform_shim_host`, `command_interface_host`)
      that most suites link against instead of recompiling those sources per-executable.
- [x] Confirm `bt_ctx_lock.c` (split out of `bt_manager.c` on 2026-07-31) is included
      wherever a new suite pulls in any `bt_manager.c`-family file that calls
      `bt_ctx_lock()`/`bt_ctx_unlock()` — every suite below that lists `bt_manager.c`,
      `bt_manager_ops.c`, `bt_pairing_store.c`, `bt_connection.c`, `bt_scan.c`,
      `bt_events_gap.c`, or `bt_events_a2dp.c` needs `bt_ctx_lock.c` alongside it.
      Confirmed via the existing `test_bt_lock_cb_reentry` block (end of file), which
      already lists `bt_ctx_lock.c` next to `bt_manager.c`.
- [x] Decide file layout: append new suites to the end of `CMakeLists.txt` in the same
      style as existing blocks, grouped by feature area (duplex state → duplex policy →
      HFP AG → HFP audio → HFP audio control → HFP commands → HFP connection → HFP
      diagnostics/event-contract → HFP manager → audio_processor HFP helpers).

### A1 — `test_bt_duplex_state` (state machine core) — DONE

Runner: `test_bt_duplex_state.c` (18 `RUN_TEST` calls, verified = sum of case-file test
functions below).

- [x] Add `add_executable(test_bt_duplex_state test_bt_duplex_state.c`
  - [x] `test_bt_duplex_state_cases.c` (12 tests)
  - [x] `test_bt_duplex_state_audio_cases.c` (4 tests)
  - [x] `test_bt_duplex_state_cleanup_cases.c` (1 test)
  - [x] `test_bt_duplex_state_strings.c` (1 test)
  - [x] `../../components/bt_manager/bt_duplex_state_core.c`
  - [x] `../../components/bt_manager/bt_duplex_state_audio.c`
  - [x] `../../components/bt_manager/bt_duplex_state_events.c`
  - [x] `../../components/bt_manager/bt_duplex_state_mode.c`
  - [x] `../../components/bt_manager/bt_duplex_state_profile.c`
  - [x] `../../components/bt_manager/bt_duplex_state_strings.c`
  - [x] `../../components/bt_manager/bt_duplex_state_transitions.c)`
  - [x] **Correction found at compile time**: also needed
        `../../components/bt_manager/bt_hfp_event_contract.c` (three of the
        `bt_duplex_state_*.c` files call its `bt_hfp_event_emit_*` functions) and
        `mocks/bt_hfp_event_command_stub.c` (stubs `cmd_send_response()`, the one
        `command_interface` symbol `bt_hfp_event_contract.c` needs, so the real
        `command_interface` component isn't required).
- [x] Add matching `target_link_libraries`/`target_compile_definitions(... PRIVATE UNIT_TEST)`.
- [x] Add `add_test(NAME test_bt_duplex_state COMMAND $<TARGET_FILE:test_bt_duplex_state>)`.
- [x] `cmake --build .` — built clean on the first attempt after the SRCS correction above.
- [x] `ctest -R test_bt_duplex_state --output-on-failure`; **18/18 passed**. Full suite
      re-run: 82/82 passed (81 pre-existing + this new one), 0 regressions.

### A2 — `test_bt_duplex_policy` + 2 standalone single-test executables — DONE

Three separate self-contained executables (each has its own `main()`, no split
case files):

- [x] `test_bt_duplex_policy` (10 tests): SRCS `test_bt_duplex_policy.c`,
      `bt_duplex_policy.c`, **`bt_hfp_manager_fd16.c`** (defines the
      `bt_manager_hfp_handle_a2dp_*_event`/`bt_manager_hfp_get_policy_snapshot`/
      `bt_manager_hfp_policy_note_*`/`bt_manager_hfp_policy_runtime_reset` glue the test
      drives directly — not in the original guess), the full `bt_duplex_state_*.c` set
      (A1), `bt_hfp_event_contract.c`, `mocks/bt_duplex_policy_manager_stub.c`
      (provides `bt_ctx`/`bt_ctx_lock`/`bt_ctx_unlock` mocks so the real `bt_manager.c`
      is never needed), `mocks/bt_hfp_event_command_stub.c`.
- [x] `test_bt_duplex_policy_capability` (1 test): `bt_duplex_policy.c` +
      `bt_hfp_event_contract.c` (needs `bt_hfp_event_emit_policy`) +
      `mocks/bt_duplex_policy_manager_stub.c` + `mocks/bt_hfp_event_command_stub.c`.
      Lighter than `test_bt_duplex_policy` — does not need `bt_hfp_manager_fd16.c` or
      the duplex_state family (only calls `bt_duplex_policy_evaluate` + emits directly).
- [x] `test_bt_duplex_policy_ordering` (1 test): confirmed at compile time — only needs
      `bt_duplex_policy.c` alone (calls `bt_duplex_policy_evaluate` directly, no stub or
      event contract needed).
- [x] **Bug found and fixed**: `test_bt_duplex_policy.c` used `PRIu32` without
      `#include <inttypes.h>` — a genuine pre-existing bug in the orphaned test file
      (never compiled before). Added the missing include.
- [x] Register all three with `add_executable`/`target_link_libraries`/`add_test`.
- [x] Build and fix compile errors (see corrections above).
- [x] `ctest -R test_bt_duplex_policy --output-on-failure`: **10/10, 1/1, 1/1** (12 total).
      Full suite re-run: 85/85 passed, 0 regressions.

### A3 — `test_bt_hfp_ag` (HFP Audio Gateway lifecycle/events) — DONE

Runner: `test_bt_hfp_ag.c` (15 tests = 13 + 2 below).

- [x] Add `add_executable(test_bt_hfp_ag test_bt_hfp_ag.c`
  - [x] `test_bt_hfp_ag_cases.c` (13 tests)
  - [x] `test_bt_hfp_ag_fd10_cases.c` (2 tests)
  - [x] `../../components/bt_manager/bt_hfp_ag_lifecycle.c`
  - [x] `../../components/bt_manager/bt_hfp_ag_events.c`
  - [x] the full `bt_duplex_state_*.c` set (A1))
  - [x] **Corrections found at compile time**: also needed
        `bt_hfp_manager_fd16.c` (policy glue: `bt_manager_hfp_policy_refresh`,
        `bt_manager_hfp_policy_note_hfp_profile_transition`/`_audio_transition`),
        `bt_duplex_policy.c` (`bt_duplex_policy_evaluate`), `bt_hfp_event_contract.c`,
        `mocks/bt_hfp_event_command_stub.c`, `mocks/bt_hfp_audio_lifecycle_stub.c`
        (mocks `bt_hfp_audio_*` so the real audio layer isn't needed — this is the
        AG-lifecycle-only test, audio is tested separately in A4),
        `mocks/bt_hfp_connection_untracked_stub.c` (stubs
        `bt_hfp_connection_handle_event`/`_cleanup_after_stack_shutdown`), and
        `mocks/bt_duplex_policy_manager_stub.c` (provides `bt_ctx`/`bt_ctx_lock`/
        `bt_manager_hfp_configured_mode_locked` — same stub used in A2).
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (see corrections above — no test-file bugs this time,
      only missing SRCS).
- [x] `ctest -R test_bt_hfp_ag --output-on-failure`; **15/15 passed**. Full suite
      re-run: 86/86 passed, 0 regressions.

### A4 — `test_bt_hfp_audio` (HFP audio path / SCO lifecycle) — DONE

Runner: `test_bt_hfp_audio.c` (15 tests = 13 + 1 + 1 below).

- [x] Add `add_executable(test_bt_hfp_audio test_bt_hfp_audio.c`
  - [x] `test_bt_hfp_audio_cases.c` (13 tests)
  - [x] `test_bt_hfp_audio_concurrency.c` (1 test)
  - [x] `test_bt_hfp_audio_lifetime.c` (1 test)
  - [x] `../../components/bt_manager/bt_hfp_audio.c`
  - [x] **Correction found at compile time**: not the real `hfp_i2s_output.c` — the test
        needs `mocks/bt_hfp_audio_i2s_stub.c`, which replaces
        `hfp_i2s_output_push_cvsd()` with an instrumented mock (call count, last
        generation/samples/PCM, accept/reject control). Also learned `hfp_i2s_output.c`
        is itself split across 4 files (`hfp_i2s_output.c`,
        `hfp_i2s_output_data.c`, `hfp_i2s_output_lifecycle.c`,
        `hfp_i2s_output_platform.c`) — relevant for A8/A12, not this suite.
      No duplex_state family needed — `bt_hfp_audio.c` only references
      `bt_duplex_snapshot_t` as a type, never calls duplex functions.
      `esp_hf_ag_api.h` usage in `bt_hfp_audio.c` is fully `#ifdef ESP_PLATFORM`-guarded,
      so no host-side HFP AG stub header was needed.
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (see correction above).
- [x] `ctest -R test_bt_hfp_audio --output-on-failure`; **15/15 passed**. Full suite
      re-run: 87/87 passed, 0 regressions.

### A5 — `test_bt_hfp_audio_control` — DONE

Runner: `test_bt_hfp_audio_control.c` (21 tests = 14 + 6 + 1 below). Note: the runner
file already `#include`s `test_bt_hfp_audio_control_health_cases.c` directly as text, so
that file is **not** a separate `add_executable` SRCS entry — it compiles as part of the
runner's own translation unit. Only `test_bt_hfp_audio_control_cases.c` and
`test_bt_hfp_audio_control_lifecycle_cases.c` need separate SRCS entries.

- [x] Add `add_executable(test_bt_hfp_audio_control test_bt_hfp_audio_control.c`
  - [x] `test_bt_hfp_audio_control_cases.c` (14 tests)
  - [x] `test_bt_hfp_audio_control_lifecycle_cases.c` (1 test)
  - [x] (`test_bt_hfp_audio_control_health_cases.c` — 6 tests — confirmed not listed
        separately; compiles via the runner's text `#include`)
  - [x] `../../components/bt_manager/bt_hfp_audio_control.c`
  - [x] the full `bt_duplex_state_*.c` set (A1) + `bt_hfp_event_contract.c` +
        `mocks/bt_hfp_event_command_stub.c`
  - [x] **Correction found at compile time**: NOT `bt_hfp_audio.c` — the real dependency
        is `mocks/bt_hfp_audio_control_dependencies.c`, a full test double that mocks
        the entire audio layer (`bt_hfp_audio_get_snapshot`,
        `bt_hfp_audio_profile_stopping`, `bt_hfp_audio_apply_duplex_state`), the i2s
        layer (`hfp_i2s_output_init/start/stop/get_snapshot/default_config/
        get_runtime_pin_owners`), and `bt_app_core`'s `bt_app_work_dispatch` — this
        suite tests the control layer in full isolation from all three, so none of
        `bt_hfp_audio.c`, `hfp_i2s_output*.c`, or `bt_app_core.c` should be linked here.)
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (see correction above; no double-definition issue —
      `bt_hfp_audio_control_health_cases.c` was correctly never listed separately).
- [x] `ctest -R test_bt_hfp_audio_control --output-on-failure`; **21/21 passed**. Full
      suite re-run: 88/88 passed, 0 regressions.

### A6 — `test_bt_hfp_commands` (command_interface HFP command handlers) — DONE

Runner: `test_bt_hfp_commands.c` (24 tests = 16 + 2 + 6 below).

- [x] Add `add_executable(test_bt_hfp_commands test_bt_hfp_commands.c`
  - [x] `test_bt_hfp_commands_cases.c` (16 tests)
  - [x] `test_bt_hfp_commands_fd16_cases.c` (2 tests)
  - [x] `test_bt_hfp_commands_integrity_cases.c` (6 tests)
  - [x] `../../components/command_interface/commands.c` (needed for the real
        `cmd_parse`/`cmd_execute`/`cmd_send_response` — the tests drive the full
        command pipeline, not `cmd_handle_hfp` directly)
  - [x] `../../components/command_interface/cmd_handlers_hfp_fd11_v2.c`
  - [x] **Major correction found at compile time, after 3 iterations**: NOT the real
        `bt_hfp_manager_fd11/13/16.c` (the tests call `mock_bt_hfp_manager_*` control
        functions that only exist in `mocks/bt_hfp_manager_command_stub.c`, which
        mocks the entire `bt_manager_hfp_*` wrapper API `cmd_handlers_hfp_fd11_v2.c`
        calls — verified all 9 of its calls match the stub exactly). Also NOT the real
        `bt_manager.c` family or `command_interface_host` (which would need the full
        real BT stack for `cmd_execute`'s other dispatch entries and collide: the
        shared `mocks/mock_audio_and_btstate.c` already defines a placeholder
        `cmd_handle_hfp` for other suites that predates this real handler, so linking
        both is a multiple-definition error). The correct, purpose-built dependency
        set is: `mocks/bt_hfp_manager_command_stub.c` +
        `mocks/bt_hfp_command_dependencies.c` (a lightweight `commands.c`-only
        dependency set: `audio_processor_init/deinit`, `uart_audio_*` stubs,
        `cmd_safe_copy`/`cmd_memcpy_safe`/`cmd_memset_safe`/`cmd_snprintf_safe`/etc.,
        `bt_manager_test_reset_btstate_mock`) + `mocks/fake_esp_err.c`
        (`esp_err_to_name`) + `mocks/mock_uart.c`. This is a much smaller, fully
        isolated build — no real `bt_manager`/`command_interface_host` needed at all.
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (see the correction above — took 3 iterations to find
      the right isolation boundary).
- [x] `ctest -R test_bt_hfp_commands --output-on-failure`; **24/24 passed**. Full suite
      re-run: 89/89 passed, 0 regressions.

### A7 — `test_bt_hfp_connection` (SLC connect/disconnect) — DONE

Standalone (own `main()`, 10 tests).

- [x] Add `add_executable(test_bt_hfp_connection test_bt_hfp_connection.c`
  - [x] `../../components/bt_manager/bt_hfp_connection.c`
  - [x] the full `bt_duplex_state_*.c` set (A1) + `bt_hfp_event_contract.c` +
        `mocks/bt_hfp_event_command_stub.c` (confirmed needed — `bt_hfp_connection.c`
        calls `bt_duplex_session_begin`/`get_snapshot`/`set_health`/
        `set_hfp_profile_state`/`set_a2dp_profile_state`/`record_stale_operation_event`
        directly))
  - [x] **Correction found at compile time**: NOT `bt_ctx_lock.c` — the test file
        itself already defines `bt_ctx`, `bt_ctx_lock`, `bt_ctx_unlock` inline (no
        separate mock needed, and using `bt_ctx_lock.c` or
        `bt_duplex_policy_manager_stub.c` here would both collide with the test's own
        definitions). Needed instead: `mocks/bt_app_core_host_stub.c`, a small
        dedicated stub providing `bt_app_work_dispatch` (calls the callback
        synchronously — appropriate for host tests) without any `bt_ctx` involvement,
        so it composes cleanly with the test file's own mock context.
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (see correction above).
- [x] `ctest -R test_bt_hfp_connection --output-on-failure`; **10/10 passed**. Full
      suite re-run: 90/90 passed, 0 regressions.

### A8 — `test_bt_hfp_diagnostics` — DONE

Standalone (7 tests).

- [x] Add `add_executable(test_bt_hfp_diagnostics test_bt_hfp_diagnostics.c`
  - [x] `../../components/bt_manager/bt_hfp_manager_fd13.c` (diagnostics/platform-ops
        home per the FD-13 closeout doc)
- [x] **Correction found before building**: the test file defines
        `bt_manager_hfp_get_status`, `bt_hfp_audio_get_snapshot`,
        `bt_app_task_get_stack_high_water_mark`, and
        `hfp_i2s_output_get_stack_high_water_mark` itself (inline mocks), so none of
        `bt_app_core.c`, `bt_hfp_audio.c`, `hfp_i2s_output.c`, or the duplex_state
        family are needed — `bt_hfp_manager_fd13.c` alone was sufficient and built
        clean on the first attempt.
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (none needed).
- [x] `ctest -R test_bt_hfp_diagnostics --output-on-failure`; **7/7 passed**. Full
      suite re-run: 91/91 passed, 0 regressions.

### A9 — `test_bt_hfp_event_contract` + `test_bt_hfp_event_uart` — DONE

Two standalone executables sharing `bt_hfp_event_contract.c`:

- [x] `test_bt_hfp_event_contract` (17 tests): SRCS
      `test_bt_hfp_event_contract.c`,
      `../../components/bt_manager/bt_hfp_event_contract.c`,
      the full `bt_duplex_state_*.c` set (A1), `mocks/bt_hfp_event_command_stub.c`.
      Built clean as originally planned.
- [x] `test_bt_hfp_event_uart` (2 tests): **correction found at compile time** — needed
      much more than the original plan. `bt_hfp_event_contract.c`'s emit path calls
      the real `cmd_send_response` (in `commands.c`), so `mock_uart.c` alone (which
      only mocks the UART *driver*, not `cmd_send_response`) wasn't enough. Final SRCS:
      `test_bt_hfp_event_uart.c`, `bt_hfp_event_contract.c`, `commands.c` (for real
      `cmd_send_response`/`cmd_execute`), `cmd_handlers_hfp_fd11_v2.c` +
      `mocks/bt_hfp_manager_command_stub.c` (provides `cmd_handle_hfp` for
      `cmd_execute`'s dispatch table — reused from A6, since the shared
      `mock_audio_and_btstate.c` stub for `cmd_handle_hfp` conflicts with
      `bt_hfp_command_dependencies.c` on `bt_manager_test_reset_btstate_mock`),
      `mocks/bt_hfp_command_dependencies.c` (stubs every *other* `cmd_handle_*` via a
      macro, plus `commands.c`'s other needs), `mocks/fake_esp_err.c`,
      `mocks/mock_uart.c`.
- [x] Register both with `add_executable`/`target_link_libraries`/`add_test`.
- [x] Build both; fix compile errors (see correction above for `test_bt_hfp_event_uart`).
- [x] `ctest -R test_bt_hfp_event --output-on-failure`; **17/17 and 2/2** (19 total).
      Full suite re-run: 93/93 passed, 0 regressions.

### A10 — `test_bt_hfp_manager` — DONE

Runner: `test_bt_hfp_manager.c` (11 tests = `test_bt_hfp_manager_cases.c`'s 11 — the
specific file this session's investigation first flagged as orphaned).

- [x] Add `add_executable(test_bt_hfp_manager test_bt_hfp_manager.c`
  - [x] `test_bt_hfp_manager_cases.c` (11 tests)
  - [x] `../../components/bt_manager/bt_hfp_manager_fd11.c`
  - [x] Confirmed at compile time: needs **`bt_hfp_manager_fd16.c`** too (not FD-13) —
        `bt_manager_hfp_policy_refresh`/`_refresh_locked`/`_copy_locked`/
        `_runtime_reset`, same policy glue pattern as A2/A3 — plus `bt_duplex_policy.c`
        for `bt_duplex_policy_evaluate`.
  - [x] the full `bt_duplex_state_*.c` set (A1) + `bt_hfp_event_contract.c` +
        `mocks/bt_hfp_event_command_stub.c`
  - [x] `mocks/bt_hfp_manager_dependencies.c`)
- [x] **Real bug found and fixed** (not just a missing-SRCS gap): `bt_hfp_manager_dependencies.c`
      declared `static platform_mutex_t s_bt_ctx_mutex;` — a genuine naming collision
      with `extern platform_mutex_t s_bt_ctx_mutex;`, which the 2026-07-31
      `bt_ctx_lock.c` extraction (earlier this session) added to
      `bt_manager_internal.h`. This mock predates that refactor and was never compiled
      since, so the collision was latent until now. Fixed by renaming the mock's
      private static to `s_mock_bt_ctx_mutex` (purely internal, no other file
      references it). Verified via a full rebuild + full ctest run afterward that the
      rename didn't affect any other suite (94/94 passed).
- [x] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [x] Build; fix compile errors (see corrections above).
- [x] `ctest -R test_bt_hfp_manager --output-on-failure`; **11/11 passed**. Full suite
      re-run: 94/94 passed, 0 regressions.

### A11 — `test_bt_manager_hfp_profiles`

Standalone (6 tests).

- [ ] Add `add_executable(test_bt_manager_hfp_profiles test_bt_manager_hfp_profiles.c`
  - [ ] `../../components/bt_manager/bt_manager.c`
  - [ ] `../../components/bt_manager/bt_ctx_lock.c`
  - [ ] `../../components/bt_manager/bt_hfp_ag_lifecycle.c`
  - [ ] `../../components/bt_manager/bt_hfp_ag_events.c`
  - [ ] the full `bt_duplex_state_*.c` set (A1)
  - [ ] `mocks/mock_a2dp.c`
  - [ ] `mocks/mock_avrc.c`
  - [ ] likely also needs `bt_manager_mocks.c`, `bt_manager_ops.c`, `bt_pairing_store.c`,
        `bt_scan.c`, `bt_connection.c`, `bt_events_gap.c`, `bt_events_a2dp.c` — confirm
        against undefined-symbol errors at link time, matching the dependency set already
        used by the existing `test_bt_manager_profiles` suite in this file)
- [ ] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [ ] Build; fix compile errors.
- [ ] `ctest -R test_bt_manager_hfp_profiles --output-on-failure`; record pass count.

### A12 — `test_hfp_i2s_output`

Runner: `test_hfp_i2s_output.c` (14 tests = `test_hfp_i2s_output_cases.c`'s 14).

- [ ] Add `add_executable(test_hfp_i2s_output test_hfp_i2s_output.c`
  - [ ] `test_hfp_i2s_output_cases.c` (14 tests)
  - [ ] `../../components/audio_processor/hfp_i2s_output.c`)
- [ ] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [ ] Build; fix compile errors.
- [ ] `ctest -R test_hfp_i2s_output --output-on-failure`; record pass count.

### A13 — `test_hfp_pcm_ring`

Runner: `test_hfp_pcm_ring.c` (8 tests = `test_hfp_pcm_ring_cases.c`'s 8).

- [ ] Add `add_executable(test_hfp_pcm_ring test_hfp_pcm_ring.c`
  - [ ] `test_hfp_pcm_ring_cases.c` (8 tests)
  - [ ] `../../components/audio_processor/hfp_pcm_ring.c`)
- [ ] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [ ] Build; fix compile errors.
- [ ] `ctest -R test_hfp_pcm_ring --output-on-failure`; record pass count.

### A14 — `test_hfp_voice_convert`

Runner: `test_hfp_voice_convert.c` (11 tests = `test_hfp_voice_convert_cases.c`'s 11).

- [ ] Add `add_executable(test_hfp_voice_convert test_hfp_voice_convert.c`
  - [ ] `test_hfp_voice_convert_cases.c` (11 tests)
  - [ ] `../../components/audio_processor/hfp_voice_convert.c`)
- [ ] Register `target_link_libraries`/`target_compile_definitions`/`add_test`.
- [ ] Build; fix compile errors.
- [ ] `ctest -R test_hfp_voice_convert --output-on-failure`; record pass count.

### A15 — Full Part A verification

- [ ] `cmake --build . -- -j"$(nproc)"` from a clean `build_host_tests` directory (full
      reconfigure) so CMake picks up every new target with no stale cache.
- [ ] `ctest --output-on-failure` for the complete suite; confirm 0 failures across both
      the pre-existing 81 executables and the ~17 newly-registered ones.
- [ ] Update the running total test-case count this document references (17 new
      suites × their listed counts ≈ 210 additional cases) with the actual number ctest
      reports.
- [ ] `python tools/run_all_tests.py --no-device --coverage --no-standalone` and confirm
      via `tmp/coverage_filtered.info` that every file listed as "MISSING" in the
      2026-07-31 investigation (`bt_hfp_manager_fd11.c`, `bt_hfp_manager_fd13.c`,
      `bt_hfp_manager_fd16.c`, `bt_hfp_ag_lifecycle.c`, `bt_hfp_ag_events.c`,
      `bt_duplex_policy.c`, `bt_duplex_state_core.c`, `bt_duplex_state_audio.c`,
      `bt_duplex_state_events.c`, `bt_duplex_state_mode.c`, `bt_duplex_state_profile.c`,
      `bt_duplex_state_strings.c`, `bt_duplex_state_transitions.c`, `bt_hfp_audio.c`,
      `bt_hfp_audio_control.c`, `bt_hfp_connection.c`, `bt_hfp_event_contract.c`,
      `hfp_i2s_output.c`, `hfp_pcm_ring.c`, `hfp_voice_convert.c`,
      `cmd_handlers_hfp_fd11_v2.c`) now reports a real, non-zero coverage percentage.
      Record the new overall line-coverage percentage (baseline was 85.0%).
- [ ] Run `idf.py build` (production) and rebuild all three device Unity test apps
      (`test_bluetooth`, `test_app_audio`, `test_manager`) to confirm none of the
      `bt_manager`/`command_interface` changes needed to make these host tests compile
      (if any header/signature fixes are needed along the way) affect the device build.
      No behavior change is expected — these are purely CMake SRCS additions plus
      whatever compile-error fixes surface — but verify per this repo's standing rule
      that any `#ifdef ESP_PLATFORM` touch gets a device build check.
- [ ] Append a `memory.md` entry (get the real timestamp via
      `date -u +"%Y-%m-%dT%H:%M:%SZ"` immediately before writing it) summarizing what was
      wired up, the new test/coverage counts, and any production bugs the newly-running
      tests turned up (very possible — this code has never executed under a test harness
      before).

---

## Part B — Raise coverage on already-tested low scorers

These files already have host test executables that run in CI; they just score below
the 85.0% overall average per the 2026-07-31 coverage report. Unlike Part A, this is
genuinely new test-case authoring, not registration.

### B1 — `list.c` (40.6%, 71/175 lines) — lowest scorer in the codebase

- [ ] Open `tmp/coverage_html/list.c.gcov.html` (regenerate via
      `python tools/run_all_tests.py --no-device --coverage --no-standalone` if stale)
      and identify which functions/branches show 0 hits.
- [ ] Identify which test executable(s) currently exercise `list.c` and whether they
      only hit the "happy path" (e.g., insert/no error paths) while error-handling and
      edge-case branches (empty list, full list, remove-not-found, iteration boundaries)
      go untested.
- [ ] Write additional test cases for the uncovered branches.
- [ ] Re-run coverage; confirm `list.c` moved meaningfully above 40.6%.

### B2 — `synth_manager.c` (67.3%, 74/110 lines)

- [ ] Identify uncovered lines via the HTML report.
- [ ] Check whether uncovered code is synth-voice edge cases (invalid voice IDs,
      boundary frequencies, arpeggio state transitions) or dead/unreachable code that
      should instead be removed (cross-reference against the project's existing
      dead-code-cleanup effort before adding tests to code that may not need to exist).
- [ ] Write additional test cases or file a dead-code removal note, as appropriate.
- [ ] Re-run coverage; confirm improvement.

### B3 — `allocator.c` (70.0%, 98/140 lines)

- [ ] Identify uncovered lines via the HTML report — likely allocation-failure and
      fragmentation/edge-size paths given typical allocator test gaps.
- [ ] Write additional test cases for failure-injection paths (allocation failure,
      double-free/invalid-free guards if present, boundary sizes).
- [ ] Re-run coverage; confirm improvement.

### B4 — `audio_processor_beep.c` (71.4%, 70/98 lines)

- [ ] Identify uncovered lines via the HTML report.
- [ ] Write additional test cases (likely beep-overlay priority interactions, WAV
      decode error paths, or truncated-file handling).
- [ ] Re-run coverage; confirm improvement.

### B5 — `audio_util.c` (71.9%, 115/160 lines)

- [ ] Identify uncovered lines via the HTML report.
- [ ] Write additional test cases for uncovered utility-function branches (format
      conversion edge cases, sample-rate math boundaries).
- [ ] Re-run coverage; confirm improvement.

### B6 — `bt_app_core.c` (73.3%, 77/105 lines)

- [ ] Identify uncovered lines via the HTML report — this file is shared production
      infrastructure (BtAppTask dispatch), so treat gaps here as higher priority than
      B1-B5.
- [ ] Write additional test cases for uncovered dispatch/queue-full/task-lifecycle paths.
- [ ] Re-run coverage; confirm improvement.

### B7 — `cmd_handlers_bt.c` (77.4%, 147/190 lines)

- [ ] Identify uncovered lines via the HTML report.
- [ ] Write additional test cases for uncovered command-parsing error paths or rarely-
      hit BT command branches.
- [ ] Re-run coverage; confirm improvement.

### B8 — Full Part B verification

- [ ] Re-run the full host suite (`ctest --output-on-failure`) after all Part B changes;
      confirm 0 failures.
- [ ] Re-run coverage one final time; record the final overall line-coverage percentage
      and compare against the 85.0% (pre-Part-A) and post-Part-A baselines recorded
      above.
- [ ] Update the coverage badge/percentage in the root `README.md` if it changed
      materially (current badge: 78.1% — note this already differs from the 85.0% figure
      measured on 2026-07-31, so reconcile which figure the badge should track before
      editing it).
- [ ] Append a final `memory.md` entry (real timestamp via `date -u`) summarizing the
      Part B work and the final coverage delta from this document's starting point.

---

## Known unknowns to resolve while executing this TODO

- Several suites above (`test_bt_hfp_audio`, `test_bt_hfp_diagnostics`,
  `test_bt_manager_hfp_profiles`) have SRCS lists marked "confirm at compile time" —
  this document is based on `#include` analysis and file-naming conventions, not a full
  dependency-graph trace. Expect the first `cmake --build` attempt for each new suite to
  surface missing SRCS via undefined-symbol linker errors; add the missing production
  `.c` file and rebuild rather than guessing further ahead of time.
- It is plausible (even likely, given this code has never run under a test harness) that
  wiring up Part A surfaces real production bugs the FD-00..FD-16 work shipped with. If
  that happens, treat it as a normal bug fix: file it, fix it, verify with the newly-
  running test, and note it explicitly in the Part A verification memory.md entry — do
  not silently adjust the test to match buggy behavior.
- `test_bt_duplex_policy_ordering.c`'s exact dependency set (whether it needs the mock
  stub or just `bt_duplex_policy.c` + duplex state) is a guess pending compile-time
  confirmation, noted inline in A2.
