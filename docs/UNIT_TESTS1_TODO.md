# Unit Test Coverage TODO (Batch 1)

**Created:** 2026-07-12
**Scope:** `esp_bt_audio_source/` host-testable unit tests (CTest, no hardware).
**Baseline:** host line coverage **68.0%** across 4750 instrumented lines, all 66 host
suites passing (measured via `--coverage` lcov build in `test/host_test/build_cov/`).

This list targets the **host-runnable** gaps only. Device-only paths (real I2S DMA,
real BT stack, GPIO) cannot be exercised on host and are called out explicitly under
"Out of scope" so nobody burns time trying to test them without hardware.

## ✅ COMPLETE — all 10 tasks landed (Ralph loop, 2026-07-12)

**Overall host coverage: 68.0% → 78.1% line, 70.4% → 84.7% function.** All 70 CTest
suites pass. One commit per task (`test:` prefix). Final per-file line coverage:

| File | Before → After | Task |
|---|---|---|
| `platform_storage_host.c` | 0.0% → **93.3%** | UT-1 |
| `synth_manager.c` | 46.4% → **67.3%** | UT-2 |
| `cmd_handlers_debug.c` | 67.8% → **98.9%** | UT-3 |
| `nvs_storage.c` | 47.7% → **90.0%** | UT-4 |
| `cmd_handlers_system.c` | 72.9% → **98.6%** | UT-5 |
| `bt_app_core.c` | 67.3% → **76.4%** | UT-6 |
| `cmd_handlers_audio.c` | 69.9% → **91.4%** | UT-7 |
| `beep_manager.c` | 78.2% → **93.2%** | UT-8 |
| `command_interface.c` | 60.0% → **100%** | UT-9 |
| `audio_processor.c` | 19.5% → **23.5%** | UT-10 |

Ceilings below the original goals (synth_manager, bt_app_core, audio_processor) are due
to genuinely host-unreachable code (dead fade envelope, FreeRTOS task loops, device
I/O) — documented per task below. `audio_processor.c` needs the #1 split before its
lifecycle code becomes host-testable.

---

## How coverage was measured (reproduce)

```bash
cd esp_bt_audio_source/test/host_test
rm -rf build_cov && mkdir build_cov && cd build_cov
cmake -DENABLE_COVERAGE=ON .. && cmake --build . -- -j"$(nproc)"
ctest                                   # populates .gcda
lcov --capture --directory . --output-file cov.info --quiet
lcov --remove cov.info '/usr/*' '*/build*/*' '*/mocks/*' '*/test/*' '*/_deps/*' \
     '*/unity/*' --output-file cov_filtered.info --quiet
lcov --list cov_filtered.info
```

Or via the harness: `conda run -n python310 python tools/run_all_tests.py --no-device --no-standalone --coverage`.

## Coverage baseline (files worth acting on)

| File | Line cov | Func cov | Instrumented ln |
|---|---|---|---|
| `audio_processor/audio_processor.c` | 19.5% | 35.7% | 523 |
| `nvs_storage/nvs_storage.c` | 47.7% | 38.2% | 350 |
| `audio_processor/synth_manager.c` | 46.4% | 60.0% | 110 |
| `platform_shim/platform_storage_host.c` | **0.0%** | 0.0% | 178 |
| `command_interface/cmd_handlers_debug.c` | 67.8% | **38.5%** | 87 |
| `command_interface/cmd_handlers_system.c` | 72.9% | **45.5%** | 70 |
| `command_interface/command_interface.c` | 60.0% | 14.3% | 15 |
| `bt_manager/bt_app_core.c` | 67.3% | 81.8% | 110 |
| `audio_processor/cmd_handlers_audio.c` | 69.9% | 91.7% | 186 |
| `audio_processor/beep_manager.c` | 78.2% | 78.6% | 147 |

> **Why some numbers are misleadingly low:** several suites link mocks
> (`mocks/nvs_storage_mock.c`, `mocks/audio_processor_host_stub.c`) that override the
> real (weak) symbols, so the real domain logic never runs *in those suites*. The 0% on
> `platform_storage_host.c` and 47.7% on `nvs_storage.c` are real, addressable gaps —
> they need **dedicated** suites that link the real implementation, not the mock.

---

## Conventions — how to add a host test (grounded in the current tree)

Each new suite is a standalone Unity executable registered in
`test/host_test/CMakeLists.txt`. Pattern (copied from `test_cmd_handlers_system`):

```cmake
add_executable(test_<name> test_<name>.c
    ../../components/<component>/<real_source>.c   # link the REAL unit under test
    mocks/<needed_mock>.c)                          # only the collaborators you must fake
target_link_libraries(test_<name> unity util_safe_host command_interface_host platform_shim_host)
add_test(NAME test_<name> COMMAND $<TARGET_FILE:test_<name>>)
```

Rules learned from the existing suites:
- **Link the real file you're testing**; only mock its *collaborators*. To test
  `nvs_storage.c` domain logic, do NOT link `mocks/nvs_storage_mock.c` — that stubs out
  the very code you want to measure.
- Test-only accessor hooks already exist for the hard-to-reach state, e.g.
  `audio_processor_test_*` in `audio_processor.c` (get active source, produce chunk,
  reset core-logic state, compute-engine-paused). Prefer these over adding new ones.
- Unity file shape: `void setUp(void)`, `void tearDown(void)`, `test_*` functions, and a
  `main()` with `UNITY_BEGIN(); RUN_TEST(...); return UNITY_END();`.
- Keep each test file under 700 lines (repo split policy). Split by scenario if it grows.

### Per-task acceptance / verification
For every task below:
1. New/extended suite **builds clean** and **passes** under `ctest`.
2. Re-run the coverage build; the target file's **line coverage meets the stated goal**.
3. `flake8` unaffected (C-only change); no new clang-tidy findings on touched files.
4. Full host sweep still 100%: `run_all_tests.py --no-device --no-standalone`.
5. One commit per task, `test:` prefix, no `Co-Authored-By` line.

---

## P0 — highest value (clean, pure logic; big uncovered surface)

### UT-1 — `platform_storage_host.c` in-memory KV store (0% → 93.3% ✅ DONE)
> Achieved 93.3% line / 100% func, valgrind-clean (commit `6d020d1d`, 21 cases, CTest #67).
> Remaining ~7% is alloc-failure paths (calloc/strdup/malloc → NULL) needing malloc injection.
New suite `test_platform_storage_host.c` linking the **real**
`components/platform_shim/platform_storage_host.c`. This is a self-contained in-memory
namespace/key/value store — pure, deterministic, no collaborators to mock.

- [ ] Register `test_platform_storage_host` target + `add_test` in CMakeLists.
- [ ] **Lifecycle:** `platform_storage_init` → `open` → `close` → `erase`; double-init;
      open with `READONLY` vs `READWRITE` mode; close invalid handle.
- [ ] **Namespaces:** `find_namespace`/`create_namespace` behavior — open new ns creates
      it; reopen returns same store; multiple independent namespaces don't alias keys.
- [ ] **i32:** `set_i32`/`get_i32` round-trip; `get` on missing key → `ESP_ERR_NVS_NOT_FOUND`;
      overwrite existing key; negative and boundary (`INT32_MIN`/`MAX`) values.
- [ ] **str:** `set_str`/`get_str` round-trip; `get_str` with `length` too small →
      length-required semantics; empty string; missing key; overwrite shorter→longer.
- [ ] **blob:** `set_blob`/`get_blob` round-trip incl. binary bytes/zeros; length probe
      (NULL out buffer returns needed length); truncated buffer; missing key; zero-length blob.
- [ ] **erase/commit:** `erase_key` on present/absent key; `commit` is a no-op success;
      `erase` (whole store) clears all namespaces; get-after-erase → NOT_FOUND.
- [ ] **Type mismatch:** `get_i32` on a key written as str/blob (define + assert the
      contract the code actually implements — document if it's permissive).
- [ ] Confirm `create_or_get_entry` growth path (many keys in one namespace) has no leak
      (run under valgrind: `run_all_tests.py --valgrind` or `valgrind ./test_platform_storage_host`).

### UT-2 — `synth_manager.c` waveform generation (46.4% → 67.3% ✅ DONE)
> Achieved 67.3% line / 100% func (commit `e3343f73`). Ceiling is lower than the 90%
> goal because the fade-envelope block is **dead code** in this build (nothing starts a
> fade → `s_synth_env` stays 0), so its ~33 lines are unreachable from host without a
> fade-activation API. Value-assertion of non-zero waveform samples is likewise
> impossible while the envelope gates output to silence. All reachable branches covered.
Extend or replace `test_synth_manager.c`. `synth_manager_generate_audio` (lines 38–186)
is nearly pure and branches on bit depth × channels × waveform — ideal value-assertion tests.

- [ ] **`synth_bytes_per_sample`** for each `audio_bit_depth_t` (8/16/24/32 as supported).
- [ ] **Reset:** `synth_manager_reset_state` zeroes phase; two runs after reset produce
      identical first samples (deterministic).
- [ ] **DC / known-value:** generate N samples at a fixed freq/rate; assert exact expected
      s16 values for the first cycle (lock the waveform math, not just "runs").
- [ ] **Bit depth matrix:** 16-bit and any other supported depth — correct byte width,
      correct sample count for a given buffer size.
- [ ] **Channels:** mono vs stereo — interleaving correct, left==right for a mono source
      upmixed to stereo (or per the actual contract).
- [ ] **Buffer edge cases:** buffer not a multiple of frame size (partial trailing frame);
      zero-length buffer returns 0; very small buffer (1 sample).
- [ ] **Phase continuity:** two back-to-back `generate_audio` calls produce a continuous
      waveform (no phase discontinuity at the boundary).
- [ ] **`synth_source_fill` / `synth_source_is_active`:** active flag toggling; fill returns
      requested bytes while active; behavior when inactive.

### UT-3 — `cmd_handlers_debug.c` handlers (38.5% → 100% func ✅ DONE)
> Achieved 98.9% line / 100% func (commit `fe977536`, 17 cases, CTest #68). Note: on host,
> the ESP_PLATFORM-gated bodies (audio/beep/worker/probe real calls, mock_pair passkey) are
> compiled out — tests assert the host-visible #else UNSUPPORTED/MOCKED branches.
**No test file exists today.** New suite `test_cmd_handlers_debug.c` driving each
`handle_debug_*` through the `cmd_handle_debug` dispatcher with a `cmd_context_t` and the
mock UART, asserting the emitted `OK|...`/`ERR|...` response. Mirror `test_cmd_handlers_system`'s
harness (mock_uart + audio_processor_host_stub + nvs mock as needed).

- [ ] Register target + `add_test`; link `commands.c` + mock UART + audio stub.
- [ ] **`DEBUG MOCK ON`** (`handle_debug_mock_on`) — sets mock-enabled flag, OK response.
- [ ] **`DEBUG MOCK ADD`** (`handle_debug_mock_add`) — adds a mock device; missing/invalid
      args → ERR; response echoes added entry.
- [ ] **`DEBUG MOCK PAIR`** (`handle_debug_mock_pair`) — pairing simulation; bad MAC → ERR.
- [ ] **`handle_debug_beep_diag` / `handle_debug_worker_diag`** — emit diag lines, OK.
- [ ] **`handle_debug_audio_diag` / `_summary` / `_probe`** — the three audio-diag variants;
      assert distinct outputs; probe with/without args.
- [ ] **`handle_debug_log`** — log-level set path; invalid level → ERR.
- [ ] **`handle_debug_force_beep`** — triggers forced beep, OK.
- [ ] **`handle_debug_drain_queue`** — drain path returns a count.
- [ ] **`handle_debug_dram`** (largest handler, 225–273) — DRAM-only toggle + report;
      on/off args; malformed arg → ERR.
- [ ] **Dispatcher:** `cmd_handle_debug` with an unknown subcommand → ERR|...|UNKNOWN.

---

## P1 — real domain gaps behind existing suites

### UT-4 — `nvs_storage.c` domain functions, real impl (47.7% → 83.7% ✅ DONE)
> Achieved 83.7% line / 88.2% func from this suite alone (commit `8ca74ba8`, 20 cases,
> CTest #69), higher combined with test_nvs_storage_errors. Valgrind-clean. Note:
> get_audio_autostart returns raw PLATFORM_ERR_STORAGE_NOT_FOUND on miss (not remapped).
New suite `test_nvs_storage_domain.c` linking **real** `nvs_storage.c` +
**real** `platform_storage_host.c` (NOT `mocks/nvs_storage_mock.c`). Covers the typed
accessors and the paired-device list logic (lines 339–559), which the current mock-backed
suites skip. (UT-1 covers the storage layer; this covers the domain layer on top of it.)

- [ ] Register target; link real nvs_storage.c + platform_storage_host.c (no nvs mock).
- [ ] **Scalars round-trip + default-on-missing:** volume, audio_autostart — get before any
      set returns the documented default; set then get; out-of-range clamp/reject.
- [ ] **i2s_pins:** `set_i2s_pins`/`get_i2s_pins` round-trip all four; partial/missing keys
      → defaults; invalid pin values.
- [ ] **strings:** device_name, default_pin — round-trip; buffer-too-small; empty; overwrite.
- [ ] **last_connected_mac:** set/get/clear; get after clear → NOT_FOUND/default; malformed
      MAC on set → error.
- [ ] **`parse_mac_str` / `format_mac_str`** (static helpers — exercise via the public MAC
      APIs): valid `AA:BB:CC:DD:EE:FF`, lowercase, wrong length, non-hex, missing colons.
- [ ] **Paired-device list:**
  - [ ] `add_paired_device` then `get_paired_count` == 1; `get_paired_device_by_index(0)`
        returns the mac+name.
  - [ ] Add duplicate MAC → updates in place, count unchanged.
  - [ ] Add up to the max, then one more → overflow behavior (reject or evict per contract).
  - [ ] `remove_paired_device` present/absent; count decrements; indices compact correctly.
  - [ ] `get_paired_device_by_index` out-of-range → error.
  - [ ] `clear_paired_devices` → count 0; subsequent get_by_index → error.
- [ ] Valgrind-clean (blob-backed list, watch for leaks on grow/remove).

### UT-5 — `cmd_handlers_system.c` remaining handlers (45.5% → 100% func ✅ DONE)
> Achieved 98.6% line / 100% func (commit `4aba9d38`, +7 cases). The i2s probe/rxtest/
> clkgen handlers turned out host-testable via their #else MOCK branches (not GPIO-only).
Extend `test_cmd_handlers_system.c`. Six of eleven functions are never dispatched. Skip the
GPIO-bound ones (see Out of scope) and cover the host-testable remainder.

- [ ] **`cmd_handle_reset`** — emits the reset acknowledgement (host: assert response, the
      actual restart is stubbed).
- [ ] **`cmd_handle_parts`** — partition table listing; assert non-empty, well-formed lines.
- [ ] **`cmd_handle_audio_status`** (412–506, large) — with audio stub in known states:
      running/stopped, each source, volume/mute reflected in the response fields.
- [ ] **`cmd_handle_version`** — already partly covered; add the branch(es) currently missed
      (e.g. override path `cmd_version_host_override`).
- [ ] Broaden **`cmd_handle_status`** — the failure/edge branches (streaming-info
      unavailable already tested; add connected-MAC present vs absent, zero-callback rates).

### UT-6 — `bt_app_core.c` error/edge branches (67.3% → 76.4%, 90.9% func ✅ DONE)
> Achieved 76.4% line / 90.9% func (commit `2ab3f6f5`, +5 cases). The ≥85% goal isn't
> host-reachable: the uncovered remainder is the bt_app_task_handler FreeRTOS loop (an
> infinite task body never spun on host) + the ESP_PLATFORM-only MGR handler.
Extend `test_bt_app_core_host.c`. Happy path is covered; the uncovered third is failure
handling in the dispatch/queue machinery.

- [ ] **`bt_app_work_dispatch`** — allocation-fail path (force alloc failure via the host
      allocator hook) → returns false, no queue entry.
- [ ] **`bt_app_send_msg`** — queue-full path → false; verify no partial enqueue.
- [ ] **`bt_app_work_copy_cb`** — `len` mismatch / zero-len / NULL params branches.
- [ ] **`bt_app_param_free_cb`** — NULL param and non-NULL param both exercised.
- [ ] **`bt_app_core_drain(max_iterations)`** — drains exactly up to max; returns count;
      drain on empty queue → 0.
- [ ] **`bt_app_core_process_once`** on empty vs non-empty queue; **`bt_app_core_queue_depth`**
      tracks push/process correctly.
- [ ] **`bt_app_task_start_up` / `bt_app_task_shut_down`** — start/stop idempotency on host.

---

## P2 — long-tail line coverage on already-decent files

### UT-7 — `cmd_handlers_audio.c` branch fill (69.9% → 80.1% line ✅ DONE)
> Achieved 80.1% line / 83.3% func (commit `ad49602b`, +10 cases). Remaining uncovered is
> mostly the beep connected-path + start/stop UNIT_TEST branches needing bt-connection setup.
Extend `test_cmd_handlers_audio.c`. Every function is entered (91.7% func) but ~30% of
lines — the error/argument-validation branches — are unhit.

- [ ] For each audio command: malformed/out-of-range argument → ERR path.
- [ ] Boundary values on volume/sample-rate/channels/bit-depth setters (min, max, min-1, max+1).
- [ ] Underlying `audio_processor_*` returning error → handler surfaces ERR (drive via stub).

### UT-8 — `beep_manager.c` edge cases (78.2% → 93.2% ✅ DONE)
> Achieved 93.2% line / 100% func (commit `ca26539e`, +4 cases: state accessors, both
> stop paths, unsupported bit-depth, 32-bit mixing).
Extend `test_beep_manager_edge_cases.c`.

- [ ] Overlay-fail path (`audio_processor_test_set_force_beep_overlay_fail`) end-to-end.
- [ ] Beep while already beeping; zero-duration; very long duration clamp.
- [ ] Remaining-bytes accounting across partial fills.

### UT-9 — `command_interface.c` `cmd_status_to_name` (14.3% → 100% func ✅ DONE)
> Achieved 100% line/func (commit `7d917c9c`, CTest #70). Also pinned the weak
> link-safety fallbacks' fail-closed contract in the same isolated suite.
Tiny but trivial. New micro-suite or fold into an existing command test.

- [ ] Assert the returned string for **every** `cmd_status_t` enum value, including an
      out-of-range/unknown value → the default label.
- [ ] (Weak stubs `cmd_init`/`cmd_parse`/etc. are overridden in real builds; leave the weak
      fallbacks — do not test the stub bodies, they exist only for link safety.)

### UT-10 — `audio_processor.c` host-testable core logic (19.5% → 23.5% ✅ DONE)
> Achieved 23.5% line / 40.5% func (commit `2e25a3c1`, +7 cases via test hooks: idle-I2S
> synth fallback, engine-pause transitions/hysteresis, chunk boundary, tag-miss reset).
> The ≥40% *line* target is not reachable from pure-logic hooks: the uncovered mass is
> device init/start/stop/deinit/configure_i2s/engine_task (ESP_PLATFORM-only). Getting
> there requires isolating the arbitration logic into its own TU — i.e. the pending #1
> split — after which a lifecycle/setter harness can drive it. Func coverage did hit
> ~40% (35.7%→40.5%).
The big file, but most of the uncovered 420 lines are device-only (see Out of scope). Push
the pure decision logic further using the existing `audio_processor_test_*` hooks; don't
chase the DMA/init lines on host. **Best done alongside the pending #1 split** (isolating
the arbitration/produce logic into a testable translation unit is the point of that split).

- [ ] **`get_active_source` priority matrix** (via `audio_processor_test_get_active_source_id`):
      beep > UART > forced-synth > I2S > silence — one case per precedence edge, incl. ties.
- [ ] **`produce_audio_chunk`** (via `audio_processor_test_produce_audio_chunk`): each active
      source produces the expected chunk; silence fills zeros; short-read zero-fill.
- [ ] **`audio_processor_test_compute_engine_paused`** — pause/resume transition table
      (was_paused × used_bytes threshold) incl. the transition-out flag.
- [ ] **`audio_processor_test_should_produce_chunk`** — paused vs free-bytes threshold grid.
- [ ] **Setter validation (no hardware):** `set_volume`/`set_mute`/`set_sample_rate`/
      `set_channels`/`set_bit_depth` argument validation + `get_config`/`get_stats`/
      `get_status` output shape, using the host stub for the I2S layer.
- [ ] **Tag-miss / recover-window accounting** via `audio_processor_test_get_tag_miss_count`
      / `_reset_tag_miss_count` / `_reset_tag_recover_window`.

---

## Out of scope (device-only — do NOT host-test)

These lines depress the numbers but require real hardware; leave them to the on-device
Unity suites (`test_bluetooth`, `test_app_audio`, `test_manager`, run via `--port`):

- `audio_processor.c`: `audio_processor_init/start/stop/deinit`, `configure_i2s`,
  `audio_processor_set_i2s_pins`, `audio_engine_task`, `emit_sync_worker_diag` — real I2S
  channel + FreeRTOS task.
- `cmd_handlers_system.c`: `cmd_handle_i2s_probe`, `_i2s_rxtest`, `_i2s_clkgen`,
  `i2s_probe_one_pin` — direct GPIO/I2S register access.
- `platform_sync_host.c` (0%, 30 ln): host suites are single-threaded, so the mutex shim is
  never driven — acceptable, not a real gap.
- `platform_*_esp32.c`: don't build on host at all.

---

## Suggested execution order & rough effort

| Order | Task | New/extend | Effort | Coverage delta |
|---|---|---|---|---|
| 1 | UT-2 synth_manager | extend | S | 46→90% (pure math) |
| 2 | UT-1 platform_storage_host | new | M | 0→90% (178 ln) |
| 3 | UT-3 cmd_handlers_debug | new | M | +8 funcs |
| 4 | UT-4 nvs_storage domain | new | M | 48→85% |
| 5 | UT-5 cmd_handlers_system | extend | S | +5 funcs |
| 6 | UT-6 bt_app_core | extend | S | 67→85% |
| 7 | UT-7 cmd_handlers_audio | extend | S | 70→85% |
| 8 | UT-9 cmd_status_to_name | new/fold | XS | 14→100% func |
| 9 | UT-8 beep_manager | extend | S | 78→90% |
| 10 | UT-10 audio_processor core | extend | L | 19→40%+ |

**Projected overall host line coverage after P0+P1: ~80%+** (from 68%), with the biggest
single jump from UT-1 (a full 178-line file from 0).

## Notes
- Scratch coverage build lives at `test/host_test/build_cov/` (untracked) — safe to `rm -rf`.
- Keep the "link the real unit, mock only collaborators" discipline — several current
  low numbers are *mock shadowing*, not missing tests, and a careless new suite could
  repeat that mistake and measure nothing.
