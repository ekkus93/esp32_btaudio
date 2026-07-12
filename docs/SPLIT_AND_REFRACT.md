# Split & Refactor — get the 10 largest code files under 700 lines

**Goal:** every source file below **700 lines**. These are **behavior-preserving**
refactors: split by responsibility, no logic changes. Do each file as its own
branch/PR, verify green, then move on.

## Progress (Ralph loop)

**Done (8/10), each behavior-preserving, verified, committed & pushed:**
#1 audio_processor.c (1692→586 + engine.c 482 + config.c 363 + sync_diag.c 171 +
test_hooks.c 153; host 70/70 + idf.py build clean; commit dcacefcd). State was already
externalized in audio_processor_state.c, so it was a clean function-relocation (pure
deletions); also moved the audio_source_t enum + a few now-cross-TU fn decls into the
internal header (behavior-neutral static→extern).

#3 web_ui.c (1025→253), #4 cmd_handlers_bt.c (838→544 + new cmd_handlers_debug.c),
#7 test_commands.c (1129→250, host 66/66 unchanged), #8 bt_a2dp_test.c (929→166,
test-app build clean), #9 run_all_tests.py (1343→595, 725/725 host run unchanged),
#10 run_test_scenarios.py (833→247, import/dispatch smoke),
#2 bt_manager.c (1315→695 + new bt_manager_ops.c 430 + bt_manager_mocks.c 338;
host CTest 70/70 + idf.py build clean; commit b1d20893). Verified per project:
WROOM32 host CTest + `idf.py build`; S3 `idf.py build`; test_bluetooth app build;
flake8 parity (no new findings).

**bt_manager.c gotcha (worth remembering for #1):** the state was easy (clean
globals — bt_ctx/s_autostart_enabled/s_autostart_attempts, defs kept in bt_manager.c);
the real cost was **27 host-test executables** that compile bt_manager.c directly, all
needing the new .c files added (uniform path → one sed). `bt_start_audio` was ~50 lines
(not ~330 as guessed), so ops.c also took volume/pairing to reach ≤700.

**Remaining (2/10) — the compile-only-verifiable tier:**
- **#5 bt_source_mock.c / #6 bt_source_stubs.c** — ~30 interspersed static vars
  incl. locally-defined struct typedefs + multi-line initializers; 5 state names
  collide with the mirror file; **only compile/link-verifiable here** (Unity suites
  need hardware). Extern-the-state approach works but needs typedef relocation and
  collision-safe naming.

## Ground rules (apply to every task)

- [ ] **Structural-only commits.** No behavior change in a split commit. If you
      must also fix behavior, do it in a *separate* commit (per
      `esp_bt_audio_source/CLAUDE.md`).
- [ ] **Verify after each split** with that project's tests before committing:
  - `esp_bt_audio_source` (WROOM32): host suite —
    `cd esp_bt_audio_source/test/host_test && cmake -S . -B build_host_tests && cmake --build build_host_tests -j && ctest --test-dir build_host_tests`
    then `idf.py build` (device compile). On-device Unity suites only if the
    change touches them and hardware is available (confirm before flashing).
  - `esp_i2s_source` (S3): `cd web && npx tsc --noEmit`, host CTest under
    `test/host_test`, `idf.py build`, and Playwright (`DEVICE_URL=… npx playwright test`).
  - Python: `conda run -n python310 python -m flake8 <files> --max-line-length=120`.
- [ ] **Keep public headers stable.** Callers/`#include`s shouldn't change. Split
      *implementation* across `.c` files that share one internal header; the public
      API header stays put.
- [ ] **File-static state is the hard part.** A `static` var can't be shared across
      `.c` files. When functions that touch the same `s_*` state land in different
      files, either (a) move that state into one `.c` and expose **accessors** in an
      internal header, or (b) declare it `extern` in an internal header and define it
      once. Prefer (a). Note this per-file below where it bites.
- [ ] **Update the build** (`CMakeLists.txt` SRCS / host-test `CMakeLists.txt` /
      Python imports) in the same commit as the split.
- [ ] Re-run `git ls-files … | xargs wc -l | sort -rn` at the end; confirm all ≤ 700.

---

## 1. `esp_bt_audio_source/components/audio_processor/audio_processor.c` — 1692 → 586 ✅ DONE

**Done (commit dcacefcd).** State was already externalized (audio_processor_state.c +
externs in audio_processor_internal.h), so this was a clean function-relocation like #2
(audio_processor.c diff is 0 insertions / 1106 deletions). Split into: engine.c (482,
get_active_source/produce_audio_chunk both copies + audio_engine_task + volume timer),
config.c (363, setters/getters/configure_i2s/set_i2s_pins), sync_diag.c (171,
emit_sync_worker_diag + mock_generate_i2s_audio), test_hooks.c (153, audio_processor_test_*);
slim audio_processor.c (586, lifecycle + is_* + set_synth_mode). Necessary behavior-neutral
extras: moved audio_source_t enum + a few now-cross-TU fn decls into the internal header,
externalized s_test_force_beep_overlay_fail. Only 2 host-test executables compile
audio_processor.c → both got the new sources. Verified host 70/70 + idf.py build. The
1.1–1.7 sub-steps below are superseded by this outcome.



The component already has `audio_processor_read.c` / `audio_processor_diag.c` /
`audio_processor_internal.h`; this is the remaining monolith (engine + lifecycle +
config + state queries + test hooks). Heavy shared `s_*` state — do the internal-header
extraction *first*.

- [ ] **1.1 Inventory shared state.** List every file-static (`s_*`, mutexes, ring,
      timers, config, flags) and which functions touch it. Decide accessor vs `extern`.
- [ ] **1.2 `audio_processor_state.{c,h}` (internal).** Move the shared statics into
      one `.c` with a small accessor/`extern` surface in `audio_processor_internal.h`.
- [ ] **1.3 Extract `audio_processor_engine.c`** (~400 lines): `audio_engine_task`,
      `get_active_source`, `produce_audio_chunk`, `mock_generate_i2s_audio` (both the
      device and `UNIT_TEST` copies — dedupe if trivial, else keep guarded).
- [ ] **1.4 Extract `audio_processor_config.c`** (~450 lines):
      `set_sample_rate/volume/mute/channels/bit_depth`, `get_config/get_stats/
      get_status`, `configure_i2s`, `set_i2s_pins`, `volume_commit_timer_callback`.
- [ ] **1.5 Move diag** `emit_sync_worker_diag` + diag state queries into the existing
      `audio_processor_diag.c`.
- [ ] **1.6 Extract `audio_processor_test_hooks.c`** (~150 lines): all
      `audio_processor_test_*` (guard with `UNIT_TEST`/test define).
- [ ] **1.7 Slim `audio_processor.c`** to lifecycle (`init/start/stop/deinit/
      drain`) + `is_running/is_i2s_active/…` (~500 lines).
- [ ] **1.8 CMake:** add the new `.c` files to the component `SRCS`.
- [ ] **1.9 Verify:** host CTest (`test_audio_processor*`, `test_commands`) + `idf.py build`.

---

## 2. `esp_bt_audio_source/components/bt_manager/bt_manager.c` — 1315 → 695 ✅ DONE

**Done (commit b1d20893).** Split into 2 new files instead of the 3 planned below
(the audio/status/control breakdown assumed a ~330-line bt_start_audio; it's ~50):
- **bt_manager_ops.c (430)** — device ops: bt_start_audio/bt_stop_audio, bt_set_volume,
  bt_pair/bt_unpair/bt_unpair_all, bt_set_pin (+ forward-decls for the moved test hooks).
- **bt_manager_mocks.c (338)** — weak failure hooks + all UNIT_TEST/CONFIG_BT_MOCK_TESTING
  bt_manager_test_*/bt_manager_mock_* functions.
- **bt_manager.c (695)** — init/deinit, status/request-handler, connect/disconnect/scan/
  pair wrappers, init_profiles + test_init_profiles; keeps the 3 shared global defs.
- Build: added both to component SRCS + all 27 host-test executables. Verified host
  CTest 70/70 + idf.py build clean. bt_manager.c diff is pure deletions (byte-identical
  remaining code). The original 2.1–2.4 sub-steps below are superseded by this outcome.

Component already split (`bt_connection.c`, `bt_scan.c`, `bt_pairing_store.c`,
`bt_events_*.c`, `bt_streaming_manager.c`, `bt_connection_manager.c`). `bt_manager.c`
is the facade + a large block of test/mock hooks.

- [ ] **2.1 Extract `bt_manager_mocks.c`** (~450 lines): all `MAYBE_WEAK` weak hooks,
      `bt_manager_mock_*` (device_found / connection_established / connection_closed /
      audio_state_changed / pairing_complete), `bt_manager_force_initialized`,
      `bt_manager_debug_print`, and `bt_manager_test_*`. Guard with
      `CONFIG_BT_MOCK_TESTING` / `UNIT_TEST` as today.
- [ ] **2.2 Extract `bt_manager_status.c`** (~350 lines): `bt_manager_get_status`,
      `bt_mgr_handle_get_status`, `bt_mgr_request_handler`, `bt_manager_is_connected`,
      `bt_get_device_list/paired_devices` + `*_snapshot`.
- [ ] **2.3 Extract `bt_manager_control.c`** (~350 lines): `bt_manager_pair/connect/
      disconnect/start_audio/stop_audio/start_scan/start_pair/set_name/
      set_autostart_enabled/is_autostart_enabled`, `bt_manager_init_profiles`.
- [ ] **2.4 Slim `bt_manager.c`** to init + shared ctx wiring (~350 lines). Note the
      `bt_ctx` access contract in `code_review/BT_STATE_ACCESS_CONTRACT.md` — don't
      change the write-via-queue / read-from-cmd_proc pattern, just relocate.
- [ ] **2.5 CMake + verify:** host CTest (`test_bt_manager*`, `test_bluetooth`,
      `test_commands`) + `idf.py build`.

---

## 3. `esp_i2s_source/components/web_ui/web_ui.c` — 1025 → ≤700

Grew a lot this session (all the REST endpoints). Split handlers by feature; keep
`web_ui_start()` as the registrar. Shared `s_bt_*` state + `s_bt_mtx` + `recv_body`
need an internal header.

- [x] **3.1 `web_ui_internal.h`.** Declares `recv_body` + every feature-handler
      prototype the registrar needs, plus `web_ui_bt_init()`. The BT state stayed
      **`static` in `web_ui_bt.c`** — moving *all* its readers/writers there meant no
      extern surface was needed (cleaner than the accessor plan). 46 lines.
- [x] **3.2 Extract `web_ui_bt.c`** (473 lines): `bt_split/bt_add/bt_remove/
      bt_status_conn_mac`, `on_bt_event`, `bt_dev_array`, `bt_get_h`, `bt_post_h`,
      `connect_volume_task`, `scan_post_h`, `console_post_h`, `ctrl_get_h/ctrl_post_h`,
      `parse_wroom_kv/parse_wroom_vol`, `btvolume_get_h/btvolume_post_h`, plus new
      `web_ui_bt_init()` (owns mutex create + subscribe + PAIRED prime).
- [x] **3.3 Extract `web_ui_radio.c`** (175 lines): `radio_post/radio_delete`,
      `radio_play_task/radio_stop_task`, stations CRUD (`stations_get/post/put/
      delete_h`, `station_id_param/station_body/station_reply`).
- [x] **3.4 Extract `web_ui_audio.c`** (100 lines): `tone_post/tone_delete`,
      `volume_post_h`, `prebuffer_post_h`.
- [x] **3.5 Extract `web_ui_wifi.c`** (106 lines): `wifi_post`, `apmode_post_h`,
      `provision_task`.
- [x] **3.6 Slim `web_ui.c`** to `root_get`, `status_get`, `refresh_wroom`,
      `recv_body`, and `web_ui_start()`. 253 lines.
- [x] **3.7 CMake + verify:** `idf.py build` (S3) clean; no host/tsc coverage of
      web_ui (device-only httpd); endpoints + registrar unchanged so Playwright
      unaffected (needs device, not run here).

---

## 4. `esp_bt_audio_source/components/command_interface/cmd_handlers_bt.c` — 838 → ≤700

Real BT command handlers + a big `handle_debug_*` mock/diag block.

- [x] **4.1 Extract `cmd_handlers_debug.c`** (~320 lines): `handle_debug_mock_on/add/
      pair`, `handle_debug_beep_diag/worker_diag/audio_diag/audio_diag_summary/
      audio_diag_probe`, `handle_debug_log/force_beep/drain_queue/dram`, and the
      `cmd_handle_debug` dispatcher (already declared in `cmd_handlers.h`). Shared
      mock statics moved to `cmd_handlers_internal.h` as `g_cmd_mock_*` (defined in
      `cmd_handlers_bt.c`, referenced from both). Result: **296 lines**.
- [x] **4.2 Keep `cmd_handlers_bt.c`** (~520 lines): `cmd_handle_scan/connect/
      connect_name/disconnect/pair/paired/confirm_pin/enter_pin/set_default_pin/
      unpair/unpair_all/set_name/last_mac`. Result: **544 lines**.
- [x] **4.3 CMake + verify:** host CTest (`test_cmd_handlers_bt`, `test_commands`) 66/66
      + `idf.py build` (see below).

---

## 5. `esp_bt_audio_source/test/test_bluetooth/main/bt_source_mock.c` — 1937 → ≤700

On-device BT API mock, guarded by `BT_MOCK_PROVIDES_PROTOTYPES` /
`CONFIG_BT_MOCK_TESTING`. Split by ESP BT API domain. Heaviest split — budget for 3–4
files.

- [ ] **5.1 `bt_source_mock_internal.h`.** Shared mock state (connection/pairing/scan
      state, TAG) + accessors; keep the conditional-prototype guards centralized.
- [ ] **5.2 Extract `bt_source_mock_a2dp.c`** — A2DP source + streaming mocks.
- [ ] **5.3 Extract `bt_source_mock_gap.c`** — GAP / pairing / SSP / auth mocks.
- [ ] **5.4 Extract `bt_source_mock_scan.c`** — inquiry/scan + reconnect controls
      (the `CONFIG_BT_MOCK_TESTING` reconnect block).
- [ ] **5.5 Keep `bt_source_mock.c`** — init/state/accessors + anything cross-cutting.
- [ ] **5.6 CMake** (`test/test_bluetooth/main/CMakeLists.txt` SRCS) **+ verify:**
      builds under the test app; run the Unity suite only with hardware (confirm first).

---

## 6. `esp_bt_audio_source/test/test_bluetooth/main/bt_source_stubs.c` — 1763 → ≤700

Same shape as #5 (stub implementations of the BT APIs for the test app). Mirror the
domain split so mock/stub stay parallel.

- [ ] **6.1 Extract `bt_source_stubs_a2dp.c`** — A2DP/streaming stubs.
- [ ] **6.2 Extract `bt_source_stubs_gap.c`** — GAP/pairing stubs.
- [ ] **6.3 Extract `bt_source_stubs_scan.c`** — scan/timeout stubs.
- [ ] **6.4 Keep `bt_source_stubs.c`** — connection/paired-device/streaming accessors
      + shared stub state.
- [ ] **6.5 CMake + verify** (test-app build; hardware run gated).

---

## 7. `esp_bt_audio_source/test/host_test/test_commands.c` — 1129 → ≤700

88 host tests, one executable (`test_commands`). **Keep one executable** — move test
*bodies* into grouped `.c` files linked into it; the runner keeps `setUp/tearDown` +
`main()` + `RUN_TEST`s (the file already uses `extern void test_*`).

- [x] **7.1 `test_commands_shared.h`** — the `#include` block + mock/test `extern`
      decls + all 63 test prototypes + `count_substring` (now non-`static`). No
      shared file-static fixture state existed, so this was purely declarations.
      103 lines.
- [x] **7.2 Extract `test_commands_parse.c`** — 13 `cmd_parse`/protocol tests. 119 lines.
- [x] **7.3 Extract `test_commands_audio.c`** — 37 beep/synth/volume/mute/status/
      start-stop/etc. tests. 593 lines.
- [x] **7.4 Extract `test_commands_bt.c`** — 13 scan/pair/connect/unpair/pin/
      disconnect tests. 278 lines.
- [x] **7.5 Keep `test_commands.c`** as the runner (`main` + `RUN_TEST` list,
      `setUp/tearDown`, `cmd_version_host_override`, `count_substring`). **250 lines.**
- [x] **7.6 host-test `CMakeLists.txt`:** added the 3 `.c` files to the
      `test_commands` executable (same target).
- [x] **7.7 Verify:** `test_commands` reports **63 Tests 0 Failures** (unchanged);
      full host suite 66/66.

---

## 8. `esp_bt_audio_source/test/test_bluetooth/main/bt_a2dp_test.c` — 929 → ≤700

62 on-device Unity tests. Same "one app, split bodies" approach as #7, but this is the
device test app (`app_main` → `UNITY_BEGIN`).

- [x] **8.1 Group the 29 tests** (file had 29 defs + 3 cross-file protos, not 62)
      by scenario: scan (8), connection (13), streaming (8).
- [x] **8.2 Move bodies** into `bt_a2dp_test_{scan,connection,streaming}.c`; keep the
      helpers (`wait_for_*`, `parse_test_addr`, now non-`static`) + `run_bt_a2dp_tests()`
      RUN_TEST list in `bt_a2dp_test.c` (166 lines). Shared decls (includes, externs,
      `#define TAG`, helper + test prototypes) in `bt_a2dp_test_shared.h` (73 lines).
- [x] **8.3 CMakeLists + verify:** added the 3 files to the test-app SRCS; `idf.py
      build` of test_bluetooth clean (on-device Unity run gated on hardware).

---

## 9. `tools/run_all_tests.py` — 1343 → ≤700

**Referenced by CI and the `lint-n-test` skill as `tools/run_all_tests.py`** — the
entry path must stay. Convert to a package `tools/run_all_tests_lib/` (or
`tools/rat/`) and keep `run_all_tests.py` as a thin CLI.

- [x] **9.1 `tools/rat/proc.py`** — `run_cmd`, `run_with_pty`, `ensure_esptool`. 93 lines.
- [x] **9.2 `tools/rat/cleanup.py`** — `cleanup_previous_artifacts`, `_unlink_artifact`. 46 lines.
- [x] **9.3 `tools/rat/host.py`** — `run_host_tests`, `run_standalone_host_tests`,
      `run_cmake_unity_suite`, `generate_coverage_report`. 387 lines.
- [x] **9.4 `tools/rat/device.py`** — `run_device_suite`. 69 lines.
- [x] **9.5 `tools/rat/report.py`** — `aggregate_summary`, `count_unity_results`,
      `_unity_counts_from_output`, `parse_flash_time_from_log`, `parse_ctest_duration`. 200 lines.
      (Plus `tools/rat/common.py` for the shared `ROOT`/`TMP_DIR`, and `__init__.py`.)
- [x] **9.6 `run_all_tests.py`** keeps `main(argv)` + argparse, importing the above.
      **595 lines** (main() alone is ~550). Entry path unchanged.
- [x] **9.7 Verify:** `flake8 tools/run_all_tests.py tools/rat --max-line-length=120`
      shows **zero new findings** vs the original (identical advisory code counts);
      `python tools/run_all_tests.py --no-device --no-standalone` green (see below).

---

## 10. `rpi_i2s_source/tools/run_test_scenarios.py` — 833 → ≤700

One giant `TestScenarioRunner` class (lines 73–739). Split the class's method groups
into modules; keep the CLI (`main`, `list_scenarios`) thin.

- [x] **10.1 Map the methods** — core/api (`__init__`, `run_scenario`,
      `run_all_scenarios`, `_api_get/_api_post/_wait_for_status`), 7 `_scenario_*`
      runners, and 3 report/summary methods.
- [x] **10.2 Extract via mixins** (method bodies use `self.*`, so mixins compose
      cleanly): `rts/types.py` (`TestResult`+`ScenarioResult`, 33 lines),
      `rts/scenarios.py` (`ScenariosMixin`, 343), `rts/reporting.py`
      (`ReportingMixin`, 224), plus `rts/__init__.py`.
- [x] **10.3 Slim `run_test_scenarios.py`** to `class TestScenarioRunner(
      ScenariosMixin, ReportingMixin)` (core/api methods) + `list_scenarios` +
      `main`. **247 lines**; entry path unchanged.
- [x] **10.4 Verify:** import + dispatch smoke (`run_scenario('system')` returns a
      `ScenarioResult`, all 7 scenario methods + reporting resolve via the mixins);
      `flake8 … --max-line-length=120` shows **no new findings** (F401 count actually
      dropped 2→0). No ESP toolchain involved.

---

## Suggested order

1. **#3 web_ui.c** and **#4 cmd_handlers_bt.c** — smallest, cleanest wins; validate the
   approach (internal header + CMake SRCS) on both projects.
2. **#9 run_all_tests.py**, **#10 run_test_scenarios.py** — pure-Python, low risk.
3. **#7 test_commands.c**, **#8 bt_a2dp_test.c** — test-body splits (one executable).
4. **#1 audio_processor.c**, **#2 bt_manager.c** — production, shared-state heavy; do
   the internal-state extraction carefully.
5. **#5 bt_source_mock.c**, **#6 bt_source_stubs.c** — largest, but mechanical
   domain splits; do them last, in parallel (keep mock/stub structure mirrored).

## Done when

- [ ] `git ls-files -- '*.c' '*.h' '*.ts' '*.tsx' '*.py' | grep -vE 'node_modules|/build/' | xargs wc -l | sort -rn | head` shows **no file > 700**.
- [ ] All host suites green (both ESP projects), both `idf.py build`s clean, S3 Playwright green, flake8 no new findings.
