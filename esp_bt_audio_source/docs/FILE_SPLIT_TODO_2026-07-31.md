# File Size Reduction TODO — 2026-07-31

## Goal

Split the 5 largest project-authored (non-vendor, non-third-party) files down to
under 800 lines each, by extracting cohesive sub-modules into sibling files —
the same pattern this codebase already uses repeatedly:

- `bt_hfp_manager_fd11.c` / `bt_hfp_manager_fd13.c` / `bt_hfp_manager_fd16.c` (phase split)
- `hfp_i2s_output_data.c` / `_lifecycle.c` / `_platform.c`
- `bt_duplex_state_core.c` / `_audio.c` / `_events.c` / `_mode.c` / `_profile.c` / `_strings.c` / `_transitions.c`
- `bt_ctx_lock.c` (extracted from `bt_manager.c` earlier in this project's history)

Each split introduces a new `_internal.h` header (or extends an existing one)
to share static state/types/function declarations between the sibling `.c`
files, exactly as `bt_manager_internal.h` already does for `s_bt_ctx_mutex`.

Target files (current line counts, vendor/third_party excluded):

| # | File | Lines | Target after split |
|---|------|------:|---------------------|
| A | `components/bt_manager/bt_hfp_audio_control.c` | 1241 | ~430 core + 3 siblings |
| B | `components/command_interface/cmd_handlers_hfp_fd11_v2.c` | 802 | ~480 core + 2 siblings |
| C | `test/host_test/test_audio_processor_diag.c` | 846 | ~260 core + 2 siblings |
| D | `components/bt_manager/bt_manager.c` | 811 | ~680 core + 1 sibling |
| E | `components/bt_manager/bt_events_a2dp.c` | 820 | ~300 core + 2 siblings |

## Critical risk note — read before starting D or E

`bt_manager.c` and `bt_events_a2dp.c` are each compiled directly (not through
a library) into **~28 separate host-test executables** in
`test/host_test/CMakeLists.txt` (`target_sources`/`add_executable` blocks —
confirmed via `awk` scan, list captured in Part D below). Splitting either
file means **every one of those ~28 blocks** must be updated to add the new
sibling file(s), or that test executable fails to link with an undefined
reference the moment a function moves out of the original file.

By contrast, `bt_hfp_audio_control.c` is referenced by exactly **1** test
target (`test_bt_hfp_audio_control`) and `cmd_handlers_hfp_fd11_v2.c` by
exactly **2** (`test_bt_hfp_commands`, `test_bt_hfp_event_uart`). `test_audio_processor_diag.c`
*is* a test target itself, so its split only requires adding brand-new
`add_executable` blocks, not touching existing ones.

**Do Parts A, B, C first** (low blast radius) to bank progress before
tackling the CMake-heavy Parts D and E. When you do reach D/E, treat
"grep every target block that references the file and update it" as its own
checklist item — do not assume a partial sed/grep pass caught them all;
re-grep and diff-check after editing.

## Ground rules for every part

- Follow the codebase's existing `_internal.h` pattern; do not invent a new sharing mechanism.
- Preserve every existing public API signature exactly — this is a pure file-layout refactor, not a behavior change.
- `#ifdef UNIT_TEST` test-hook functions move to whichever new file owns the state they inspect/reset.
- After each part: run `idf.py build` (CLAUDE.md mandates this for any change touching `#ifdef ESP_PLATFORM` code — a host-test-only pass will not catch device-build breakage), then the full host suite (`ctest --output-on-failure` from `test/host_test/build_host_tests/`), with **zero regressions** before moving on.
- Verify the new file's line count with `wc -l` after each split — don't just eyeball it.
- One commit per part (matches this repo's established one-task-one-commit convention).

---

## Part A: `bt_manager/bt_hfp_audio_control.c` (1241 → ~430 lines)

### A0. Create `bt_hfp_audio_control_internal.h`
- [x] New header in `components/bt_manager/include/` (or alongside the `.c` files, matching wherever `bt_duplex_state_internal.h`-style headers already live for this component).
- [x] Move the private types `bt_hfp_audio_work_request_t`, `bt_hfp_audio_remote_cleanup_t`, `bt_hfp_audio_control_context_t` into it.
- [x] Declare `extern bt_hfp_audio_control_context_t s_control;` (was `static`).
- [x] Under `#ifdef UNIT_TEST`, declare `extern bt_hfp_audio_control_platform_ops_t s_test_ops;` and `extern bool s_test_ops_set;`.
- [x] Declare (non-static) prototypes for every function that moves to a different file than its caller: `control_lock`, `control_unlock`, `drain_sem`, `context_ensure`, `parse_mac`, `same_peer`, `copy_peer`, `platform_audio_connect`, `platform_audio_disconnect`, `stop_i2s_for_session`, `set_audio_state_if_needed`, `set_health`, `reserve_operation`, `update_operation_generation`, `finish_operation`, `queue_lower_request`, `issue_cleanup_disconnect`, `lower_request_may_be_live`, `rollback_started_i2s`.
- [x] Add the new header to whichever internal-headers install/include path the component already uses (check how `bt_duplex_state_internal.h` is wired into CMake includes, if at all — likely nothing extra needed since it's in the same source dir).

### A1. Create `bt_hfp_audio_control_i2s.c` (~215 lines: current lines 173–384)
- [x] Move: `map_i2s_state`, `set_i2s_state_if_needed`, `set_audio_state_if_needed`, `set_health`, `local_i2s_snapshot`, `sync_i2s_state`, `ensure_i2s_initialized`, `start_i2s_for_session`, `record_i2s_stop_failure`, `stop_i2s_for_session`.
- [x] `#include "bt_hfp_audio_control_internal.h"` plus whatever of `bt_app_core.h` / `hfp_i2s_output.h` / `platform_sync.h` this subset actually needs.
- [x] Drop the now-dead forward declaration of `stop_i2s_for_session` from the top of the original file (it becomes a header-declared cross-file function).

### A2. Create `bt_hfp_audio_control_work.c` (~330 lines: current lines 47–171, 386–429, 431–487, 567–732)
- [x] Move MAC helpers: `hex_value`, `parse_mac`, `same_peer`, `copy_peer`.
- [x] Move `platform_audio_connect`, `platform_audio_disconnect`.
- [x] Move `remote_cleanup_handler`, `dispatch_remote_cleanup`.
- [x] Move `work_handler`.
- [x] Move `queue_lower_request`, `issue_cleanup_disconnect`, `lower_request_may_be_live`, `rollback_started_i2s`.
- [x] This file owns the `bt_app_work_dispatch` interaction and the BT_HFP_AUDIO_WORK_EVENT / BT_HFP_AUDIO_REMOTE_CLEANUP_EVENT constants — move those `#define`s here too.

### A3. Create `bt_hfp_audio_control_events.c` (~210 lines: current lines 962–1168)
- [x] Move `record_wrong_peer_event`, `reject_unexpected_connected_event`, `bt_hfp_audio_control_handle_event` (the public event-ingest entry point).

### A4. Trim `bt_hfp_audio_control.c` down to the orchestration core (~430 lines)
- [x] Keep: includes/defines/types header block reduced to what core still needs, `context_ensure`/`bt_hfp_audio_control_init`, `reserve_operation`/`update_operation_generation`/`finish_operation`, the two big public entry points `bt_hfp_audio_start` and `bt_hfp_audio_stop`, `bt_hfp_audio_control_get_snapshot`, `bt_hfp_audio_control_profile_stopping`, `bt_hfp_audio_control_cleanup_after_stack_shutdown`, and the `#ifdef UNIT_TEST` hooks `bt_hfp_audio_control_test_set_platform_ops` / `bt_hfp_audio_control_test_reset`.
- [x] `#include "bt_hfp_audio_control_internal.h"`.
- [x] Define `s_control` (non-static now) and `#ifdef UNIT_TEST` `s_test_ops`/`s_test_ops_set` here — this file is the single definition site; the others only `extern`-reference via the internal header.

### A5. Wire up the build
- [x] `components/bt_manager/CMakeLists.txt`: add `bt_hfp_audio_control_i2s.c`, `bt_hfp_audio_control_work.c`, `bt_hfp_audio_control_events.c` next to the existing `bt_hfp_audio_control.c` entry (line 40).
- [x] `test/host_test/CMakeLists.txt`: add the 3 new files to the `test_bt_hfp_audio_control` target (the only one referencing this file, ~line 1601).

### A6. Verify and commit
- [x] `idf.py build` from `esp_bt_audio_source/` — confirm clean device build.
- [x] Full host `ctest --output-on-failure` — zero regressions.
- [x] `wc -l` all 4 files, confirm each under 800 (core should land well under).
- [x] Update this TODO's checkboxes, commit, push.

---

## Part B: `command_interface/cmd_handlers_hfp_fd11_v2.c` (802 → ~480 lines)

### B0. Create `cmd_handlers_hfp_internal.h`
- [x] Declare prototypes for the `wire_*` string-conversion functions (11 of them — see B1), `sanitize_field`, `format_checked`, and the stats-formatting entry points `send_stats_lines`, `send_diagnostics_lines` (see B2).

### B1. Create `cmd_handlers_hfp_wire.c` (~140 lines: current lines 14–148)
- [x] Move all 11 `wire_*` enum-to-string helpers: `wire_mode`, `wire_a2dp_profile`, `wire_a2dp_audio`, `wire_hfp_profile`, `wire_hfp_audio`, `wire_codec`, `wire_i2s`, `wire_health`, `wire_policy_state`, `wire_policy_reason`, `wire_downlink_owner`.
- [x] This mirrors the `bt_duplex_state_strings.c` precedent already in the codebase — same idea, applied to the command layer's wire-format strings.

### B2. Create `cmd_handlers_hfp_stats.c` (~200 lines: current lines 459–723)
- [x] Move `send_stats_line`, `send_stats_lines` (the `SEND(...)`-macro-driven STATS_* line emitter), `format_size_or_na`, `send_diagnostics_lines`.
- [x] Needs `sanitize_field`/`format_checked` from the internal header (still defined in core, per B3) and the `wire_*` helpers are NOT needed here — this block only formats raw counters, not enums. Double check `send_diagnostics_lines`/`send_stats_lines` don't call any `wire_*` function before finalizing the split (a quick grep of the moved block confirms they don't).

### B3. Trim `cmd_handlers_hfp_fd11_v2.c` down to the core (~480 lines)
- [x] Keep: `effective_mode`, `parse_mode`, `sanitize_field`, `format_checked`, `send_esp_error`, `invalid_count`, `same_peer`, `send_policy_status`, `handle_status`, `handle_connect`, `handle_disconnect`, `send_audio_status_unavailable`, `handle_audio_start`, `handle_audio_stop`, `handle_mode`, `handle_codec`, `handle_stats`, `handle_reset_stats`, `cmd_handle_hfp` (the public dispatcher).
- [x] `#include "cmd_handlers_hfp_internal.h"`.

### B4. Wire up the build
- [x] `components/command_interface/CMakeLists.txt`: add `cmd_handlers_hfp_wire.c`, `cmd_handlers_hfp_stats.c` next to the existing entry (line 29).
- [x] `test/host_test/CMakeLists.txt`: add both new files to **both** targets that reference the original — `test_bt_hfp_commands` (~line 1622) and `test_bt_hfp_event_uart` (~line 1673).

### B5. Verify and commit
- [x] `idf.py build`.
- [x] Full host `ctest --output-on-failure` — zero regressions.
- [x] `wc -l` all 3 files, confirm each under 800.
- [x] Update checkboxes, commit, push.

---

## Part C: `test/host_test/test_audio_processor_diag.c` (846 → ~260 lines)

This is host **test** code, not shipped production logic, so the split
follows the tested-module boundary rather than an internal-header pattern —
each new file is its own independent Unity executable with its own
`setUp`/`tearDown`/`main`, mirroring how `test_bt_hfp_manager_cases.c` etc.
are already split from their `main()`-holding runner file.

### C0. Confirm current test groupings (already identified by section comments in the file)
- Lines 41–165: diag/probe/status/stats core (targets `audio_processor_diag.c` + `audio_processor.c` accessors: `is_diag_enabled`, `arm_probe`, `emit_probe`, `get_status`, `get_stats`, `emit_diag_summary`).
- Lines 166–290 (`TEST-5`): `apply_volume` edge cases + the `TEST-1b` volume integration test.
- Lines 292–587 (`BT-1`): `audio_processor_config.c` setter/getter tests (mute, channels, bit depth, sample rate, i2s pins, `configure_i2s`, `get_config`).
- Lines 589–639 (`BT-2`): `audio_processor_sync_diag.c` tests.
- Lines 641–746 (`P2`): `audio_processor.c` accessors (`get_work_buffer_bytes`, `is_synth_mode_enabled`, `set_dram_only`, `set_synth_mode`, `drain_ring`).
- Lines 748–767 (`P2`): `audio_processor_diag.c` dump-helper (`diag_dump_bytes`) tests.

### C1. Create `test_audio_processor_config.c` (~300 lines)
- [ ] Move the entire BT-1 section (lines 292–587): all `test_set_sample_rate_*`, `test_set_mute_*`, `test_set_channels_*`, `test_set_bit_depth_*`, `test_set_i2s_pins_*`, `test_configure_i2s_null_config_rejected`, `test_get_config_*`.
- [ ] Give it its own `setUp`/`tearDown` (copy from the original — same reset logic applies) and its own `main()` with the corresponding `RUN_TEST` calls.

### C2. Create `test_audio_processor_runtime.c` (~330 lines)
- [ ] Move `TEST-5`/`TEST-1b` (`apply_volume` + integration, lines 166–290), `BT-2` (`sync_diag`, lines 589–639), and `P2` accessors + `drain_ring` (lines 641–746).
- [ ] Own `setUp`/`tearDown`/`main`. Note the `s_sync_diag_proc_buf`/`s_sync_diag_proc_buf2` static backing arrays (declared right before the BT-2 tests) move with those tests.

### C3. Trim `test_audio_processor_diag.c` down to the core (~260 lines)
- [ ] Keep: the diag/probe/status/stats tests (lines 41–165) and the dump-helper tests (lines 748–767).
- [ ] Keep `setUp`/`tearDown` and reduce `main()`'s `RUN_TEST` list to match.

### C4. Wire up the build
- [ ] `test/host_test/CMakeLists.txt`: duplicate the existing `test_audio_processor_diag` `add_executable` block (~line 653) twice, once per new file, keeping the identical production/mock source list (`audio_processor.c`, `audio_processor_engine.c`, `audio_processor_config.c`, `audio_processor_test_hooks.c`, `audio_processor_diag.c`, `audio_processor_sync_diag.c`, `audio_processor_state.c`, `mocks/audio_processor_core_logic_stubs.c`, `mocks/fake_log.c`, `mocks/fake_esp_err.c`, plus its `target_include_directories`/`target_compile_definitions`/`target_link_libraries`/`add_test` lines) — swap only the executable name and the first source file.
- [ ] Register both new executables with `add_test`.

### C5. Verify and commit
- [ ] Full host `ctest --output-on-failure`, confirm the same total test count as before the split (no test silently dropped from a `RUN_TEST` list during the copy/move).
- [ ] `wc -l` all 3 files, confirm each under 800.
- [ ] Update checkboxes, commit, push.

---

## Part D: `bt_manager/bt_manager.c` (811 → ~680 lines)

811 lines is only marginally over the 800 target, so this part extracts a
single cohesive unit rather than doing a 3-4-way split like Part A.

### D0. Identify the extraction: BT profile lifecycle (~130 lines total)
- Lines 653–729: `bt_manager_init_profiles`, `bt_manager_deinit_profiles` (both `#ifdef ESP_PLATFORM`).
- Lines 732–785: `#ifdef UNIT_TEST` `bt_manager_test_init_profiles` (the host-side mirror, including its `BT_MANAGER_TEST_HFP_PROFILES`-gated branch).

### D1. Update `bt_manager_internal.h`
- [ ] Move the forward declarations `static esp_err_t bt_manager_init_profiles(void);` / `static esp_err_t bt_manager_deinit_profiles(void);` (currently at lines 106–107 of `bt_manager.c`, `#ifdef ESP_PLATFORM`) into `bt_manager_internal.h` as non-static prototypes, `#ifdef ESP_PLATFORM`-guarded.
- [ ] Add a non-static prototype for `bt_manager_test_init_profiles` under `#ifdef UNIT_TEST` if not already exposed via a public test header.

### D2. Create `bt_manager_profiles.c` (~130 lines)
- [ ] Move `bt_manager_init_profiles`, `bt_manager_deinit_profiles` (`#ifdef ESP_PLATFORM`) and `bt_manager_test_init_profiles` (`#ifdef UNIT_TEST`, including the `BT_MANAGER_TEST_HFP_PROFILES` branch and its `esp_avrc_ct_init`/`esp_a2d_source_init`/`bt_hfp_ag_profile_init` host-mock sequence).
- [ ] Include whatever headers this subset needs (`bt_events_avrc.h`, `bt_events_a2dp.h`, `bt_hfp_ag.h` at minimum — check the original file's include list for what these functions actually touch).

### D3. Trim `bt_manager.c` (~680 lines)
- [ ] Everything else stays as-is: `bt_manager_get_status`, `bt_manager_finalize_teardown`/`bt_manager_finalize_init_rollback`, `bt_manager_init`/`bt_manager_deinit` (which now call the profile functions via the header declaration instead of a same-file static), all the thin wrapper accessors, and the remaining `#ifdef UNIT_TEST` hooks (`bt_manager_test_is_quarantined`, `bt_manager_test_finalize_teardown`, `bt_manager_test_finalize_init_rollback`).

### D4. Wire up the build (the high-blast-radius step — do not rush this)
- [ ] `components/bt_manager/CMakeLists.txt`: add `bt_manager_profiles.c` next to `bt_manager.c` (line 14).
- [ ] `test/host_test/CMakeLists.txt`: re-run the discovery grep fresh (do not rely on the list below going stale) —
  ```bash
  awk '
  /add_executable\(/ { match($0, /add_executable\(([A-Za-z0-9_]+)/, a); cur=a[1] }
  /target_sources\(([A-Za-z0-9_]+)/ { match($0, /target_sources\(([A-Za-z0-9_]+)/, b); cur=b[1] }
  /bt_manager\/bt_manager\.c/ { print cur }
  ' test/host_test/CMakeLists.txt | sort -u
  ```
  As of this writing that yields 28 targets: `dump_event_stress_output`, `test_autoconnect`, `test_bluetooth`, `test_bt_ctx_lock`, `test_bt_lock_cb_reentry`, `test_bt_manager_connection_pairing_events`, `test_bt_manager_edge_cases`, `test_bt_manager_hfp_profiles`, `test_bt_manager_profiles`, `test_bt_pairing_store`, `test_bt_scan`, `test_cmd_dual_uart`, `test_cmd_handlers_audio`, `test_cmd_handlers_bt`, `test_cmd_handlers_system`, `test_commands`, `test_concurrency`, `test_connect_name`, `test_event_stress`, `test_integration_flows`, `test_mock_connection_helpers`, `test_pairing_confirm`, `test_pairing_edge_cases`, `test_pairing_enter_pin`, `test_pairing_event_notifications`, `test_pairing_pending`, `test_pairing_seq_hardening`, `test_uart_audio_cmd`.
  - [ ] Add `../../components/bt_manager/bt_manager_profiles.c` immediately after every `../../components/bt_manager/bt_manager.c` line found by that grep (whether it's inside an `add_executable(...)` argument list or a separate `target_sources(...)` call for the same target — match the existing style at each site).
  - [ ] Note: only targets that actually exercise `bt_manager_init`/`bt_manager_deinit` on `ESP_PLATFORM`-style paths strictly *need* the new file for linking (host builds don't compile the `#ifdef ESP_PLATFORM` half), but `bt_manager_test_init_profiles` under `UNIT_TEST` is unconditionally compiled, so **every** target that links `bt_manager.c` needs `bt_manager_profiles.c` too, or it will fail to link on an undefined `bt_manager_test_init_profiles` reference wherever a test calls it.
  - [ ] After editing, re-run the grep for `bt_manager_profiles.c` and confirm its hit count equals the `bt_manager.c` hit count (30 lines including the 2 `.c` filename occurrences unrelated to targets, i.e. confirm 28 target-block additions).

### D5. Verify and commit
- [ ] `idf.py build`.
- [ ] Full host `ctest --output-on-failure` — this is the step most likely to surface a missed CMake site (undefined-reference link errors), so treat any linker failure here as "go back to D4," not as a code bug.
- [ ] `wc -l` both files, confirm `bt_manager.c` is now under 800.
- [ ] Update checkboxes, commit, push.

---

## Part E: `bt_manager/bt_events_a2dp.c` (820 → ~300 lines)

Same CMake blast-radius warning as Part D applies here — re-grep for
`bt_manager/bt_events_a2dp.c` in `test/host_test/CMakeLists.txt` before
starting (expect a very similar ~28-target list to Part D, since these two
files are referenced together at nearly every site observed during
investigation — verify, don't assume identical).

### E0. Create `bt_events_a2dp_internal.h`
- [ ] Move the private types `a2dp_policy_binding_t`, `a2dp_bound_profile_event_t`, `a2dp_bound_audio_event_t` into it.
- [ ] Declare `extern a2dp_policy_binding_t s_policy_binding;` (was `static`).
- [ ] Declare non-static prototypes for: `report_policy_result`, `record_rejected_bound_event`, `record_rejected_unbound_event`, `refresh_bound_generation`, `clear_binding_if_identity`, `capture_audio_binding`, `prepare_connection_event`, `prepare_audio_event`, `bda_to_string`, `increment_u64_saturating`.
- [ ] Under `#ifdef UNIT_TEST`, declare the 5 secondary-error statics as `extern`: `s_test_last_generation_diag_update_error`, `s_test_last_binding_clear_error`, `s_test_last_stale_record_error`, `s_test_last_unbound_status_error`, `s_test_last_connection_policy_error`.

### E1. Create `bt_events_a2dp_binding.c` (~350 lines: current lines 112–250, 252–341, 343–376, 378–429, 431–475, plus their UNIT_TEST hooks)
- [ ] Move `apply_base_profile_state_locked`, `create_or_capture_profile_binding`, `record_rejected_bound_event`, `record_rejected_unbound_event`, `increment_generation_sync_failure`, `preserve_primary_generation_error`, `refresh_bound_generation`, `clear_binding_if_identity`, `capture_audio_binding`, `prepare_connection_event`, `prepare_audio_event`.
- [ ] Move the `#ifdef UNIT_TEST` hooks that inspect/reset this state: `bt_events_a2dp_test_get_binding`, `bt_events_a2dp_test_reset_binding`, `bt_events_a2dp_test_reset_secondary_errors`, `bt_events_a2dp_test_reset_telemetry_errors`, `bt_events_a2dp_test_get_last_generation_diag_update_error`, `bt_events_a2dp_test_get_last_binding_clear_error`, `bt_events_a2dp_test_get_last_stale_record_error`, `bt_events_a2dp_test_get_last_unbound_status_error`, `bt_events_a2dp_test_get_last_connection_policy_error`, `bt_events_a2dp_test_prepare_audio_event`, `bt_events_a2dp_test_refresh_bound_generation`, `bt_events_a2dp_test_clear_binding_if_identity`.
- [ ] Define `s_policy_binding` here (single definition site — core file `extern`-references it via the internal header).

### E2. Create `bt_events_a2dp_data.c` (~90 lines: current lines 52–56, 62, 629–686, plus its UNIT_TEST hooks)
- [ ] Move the `a2dp_data_diagnostics_t` type, `s_a2dp_data_diag` static, `a2dp_data_record_audio_read_failure`, `bt_events_a2dp_data_callback` (the `esp_a2d_source_data_cb_t` implementation).
- [ ] Move `#ifdef UNIT_TEST` `bt_events_a2dp_test_reset_data_diagnostics`, `bt_events_a2dp_test_get_data_diagnostics`.
- [ ] This is the A2DP PCM-pull data-path callback — functionally distinct from the connection/audio-state policy machinery, worth keeping separate for readability even though it's small.

### E3. Trim `bt_events_a2dp.c` down to the core (~300 lines)
- [ ] Keep: `bda_to_string`, `increment_u64_saturating`, `next_lifecycle_serial`, `report_policy_result`, `apply_connection_policy`, `apply_audio_policy`, `bt_events_handle_a2dp_connection`, `bt_events_handle_a2dp_audio`, `bt_events_a2dp_callback` (the public GAP-registered callback), `bt_events_a2dp_reset_binding`.
- [ ] `#include "bt_events_a2dp_internal.h"`.

### E4. Wire up the build (high blast radius — same discipline as D4)
- [ ] `components/bt_manager/CMakeLists.txt`: add `bt_events_a2dp_binding.c`, `bt_events_a2dp_data.c` next to `bt_events_a2dp.c` (line 22).
- [ ] `test/host_test/CMakeLists.txt`: re-grep fresh for every site referencing `bt_manager/bt_events_a2dp.c` and add both new files at each one — do not reuse the Part D target list without re-verifying, since the two files' reference sites, while overlapping heavily, were not confirmed identical.
- [ ] After editing, re-grep to confirm the new files' hit counts match `bt_events_a2dp.c`'s.

### E5. Verify and commit
- [ ] `idf.py build`.
- [ ] Full host `ctest --output-on-failure` — zero regressions.
- [ ] `wc -l` all 3 files, confirm each under 800.
- [ ] Update checkboxes, commit, push.

---

## Final check

- [ ] Re-run the original vendor-excluded top-10-largest-files scan; confirm all 5 files from this TODO are now under 800 lines and note the new top 5 (informational only, not a new commitment).
- [ ] Confirm no file was accidentally left orphaned from every CMakeLists.txt that used to reference it (a stale reference to a function that no longer exists in that file is a build break, not a silent no-op).
