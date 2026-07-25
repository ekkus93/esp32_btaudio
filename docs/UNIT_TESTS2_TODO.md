# Unit Test Coverage TODO (Batch 2)

**Created:** 2026-07-25
**Scope:** cross-project host-testable gaps found during the 2026-07-25 coverage
audit — both `esp_i2s_source/` and `esp_bt_audio_source/`. Continues
[`UNIT_TESTS1_TODO.md`](UNIT_TESTS1_TODO.md) (Batch 1, esp_bt_audio_source-only,
complete: 68.0% → 78.1%).
**Baseline (measured 2026-07-25, all suites passing):**

| Project | Line coverage | Instrumented lines | Suites |
|---|---|---|---|
| `esp_bt_audio_source` | **79.4%** | 5333 | 74 (891 cases) |
| `esp_i2s_source` | **55.1%** | 3571 | 25 (all pass) |

This list targets **host-runnable** gaps only. FreeRTOS task-loop bodies (`*_task`
functions with `for(;;)`/blocking reads), and thin `main.c` boot glue, are called
out under "Out of scope" — they need device/integration testing, not host unit
tests, and burning time chasing their line numbers won't move real risk.

---

## How coverage was measured (reproduce)

**esp_bt_audio_source:**
```bash
cd /home/phil/work/esp32/esp32_btaudio
. .venv/bin/activate
python tools/run_all_tests.py --no-device --coverage --no-standalone
lcov --list tmp/coverage_filtered.info
# per-function detail: open tmp/coverage_html/<component>/<file>.c.func.html
```

**esp_i2s_source:**
```bash
cd /home/phil/work/esp32/esp32_btaudio/esp_i2s_source
./tools/run_host_tests.sh --coverage
lcov --capture --directory test/host_test/build_host_tests_strict_coverage \
     --output-file /tmp/i2s_coverage.info --quiet
lcov --remove /tmp/i2s_coverage.info '/usr/*' '*/build_host_tests*/*' \
     '*/mocks/*' '*/test_*' '*/_deps/*' \
     --output-file /tmp/i2s_coverage_filtered.info --quiet
lcov --list /tmp/i2s_coverage_filtered.info
genhtml /tmp/i2s_coverage_filtered.info --output-directory /tmp/i2s_coverage_html --quiet
```

> **Why some numbers look worse than reality:** a file can be *linked* into a
> test executable (for compile completeness — e.g. `test_audio_processor_diag`
> already links `audio_processor_config.c`) without any test actually *calling*
> most of its functions. Check the per-function `.func.html` report, not just
> the file-level percentage, before concluding a file needs a whole new test
> target — it may just need new test cases added to an existing one.

---

## Conventions — how to add a host test

### esp_bt_audio_source (`test/host_test/CMakeLists.txt`)
```cmake
add_executable(test_<name> test_<name>.c
    ../../components/<component>/<real_source>.c   # link the REAL unit under test
    mocks/<needed_mock>.c)
target_link_libraries(test_<name> unity util_safe_host platform_shim_host)
add_test(NAME test_<name> COMMAND $<TARGET_FILE:test_<name>>)
```
- **Link the real file you're testing**; only mock its *collaborators*.
- Unity shape: `void setUp(void)`, `void tearDown(void)`, `test_*` functions,
  `main()` with `UNITY_BEGIN(); RUN_TEST(...); return UNITY_END();`.
- Keep test files under ~700 lines (repo split policy); split by scenario if it grows.

### esp_i2s_source (`test/host_test/CMakeLists.txt`)
```cmake
add_executable(test_<name> test_<name>.c
    ${RADIO_DIR}/<real_source>.c                    # or ${CTRL_DIR}, ${I2S_OUT_DIR}, etc.
    mocks/<needed_mock>.c)
target_include_directories(test_<name> PRIVATE ${RADIO_DIR}/include ...)
target_link_libraries(test_<name> unity)
add_test(NAME test_<name> COMMAND test_<name>)
```
- Reusable FreeRTOS-primitive mocks already exist: `mocks/freertos_event_group.c`,
  `mocks/fake_semphr.c`, `mocks/fake_queue.c`, `mocks/fake_esp_err.c`,
  `mocks/compat_strlcpy.c` — link these instead of writing new ones.
- `mocks/stubs/esp_http_client.h` declares the API but has **no mock
  implementation** — HTTP-touching tests need a new `mocks/fake_http_client.c`
  (see I2S-1 below for the exact shape needed).

### Per-task acceptance / verification
1. New/extended suite **builds clean** and **passes** under `ctest`.
2. Re-run the coverage build; the target file's line coverage **improves toward
   the stated goal** (ceilings from genuinely host-unreachable code — task
   loops, `#ifdef ESP_PLATFORM`-only branches — are fine; document them like
   Batch 1 did for `synth_manager.c`'s dead fade envelope).
3. `flake8`/clang-tidy unaffected on touched files (C-only changes).
4. Full host sweep still 100% on both projects.
5. One commit per task, `test:` prefix, no `Co-Authored-By` line.

---

## P0 — highest value (security-relevant or large clean surface)

### I2S-1 — `radio_stream.c` redirect/policy re-check (15.2% → 46.8% ✅ DONE)
> Achieved 46.8% line / 76.9% func (was 15.2%/? — commits f33b821f, 5174abd6,
> 15c9cb33, d98acd1b, 18954212, and the reconnect_delay_ms commit below). 33
> new cases in `test_radio_stream_redirects.c`, all passing; full host suite
> 26/26 throughout; `idf.py build` clean. Remaining gap to the original 80%
> goal is `stream_task`, `http_evt`, `on_audio`, `on_title` — the task-loop
> body and its ICY/header-capture callbacks, correctly out of scope per this
> doc's "Out of scope" section (would need a much larger harness simulating
> the full streaming loop, not just the redirect/policy logic this task
> targeted). The SSRF-prevention assertion is CONFIRMED PASSING.
**Why this was P0, not just low:** this is the logic that stops a malicious/compromised
radio-stream server from redirecting the device to an internal address (SSRF).
It had **zero test evidence it worked** — `redirect_target_allowed`,
`resolve_redirect_location`, and `connect_with_redirects` (the loop that calls
them) were all 0-call. Only `has_playlist_extension`/`radio_resolve_input` (24
calls each, via `test_radio_lifecycle`) and `set_radio_error` were exercised.

**Prerequisite:** `mocks/stubs/esp_http_client.h` has declarations only, no
controllable behavior. Write `mocks/fake_http_client.c`:
- `esp_http_client_init` stores the `event_handler` callback from the config and
  returns an opaque handle backed by a small static/malloc'd fake-response queue.
- Test setup pushes a sequence of "hops": `{status_code, headers[]}` where
  `headers` is a list of `{key, value}` pairs.
- `esp_http_client_open`/`fetch_headers` invoke the stored `event_handler` with
  synthetic `HTTP_EVENT_ON_HEADER` events for the current hop's headers (this is
  how `http_evt()` populates `s_hdr_location`/`s_hdr_ct`/etc. — mirror that
  exactly, e.g. push `{"location", "http://10.0.0.5/x"}` to simulate a redirect).
- `esp_http_client_get_status_code` returns the current hop's status.
- `esp_http_client_close`/`cleanup` just advance to the next queued hop / free.

- [x] Write `mocks/fake_http_client.c` + header per the shape above; add a
      `test_radio_stream_redirects` target linking `radio_stream.c`,
      `url_policy.c`, `radio_parse.c`, the fake HTTP client, and the existing
      FreeRTOS mocks (event_group/semphr/queue/esp_err). — **DONE**: also
      needed `mocks/fake_task.c` + `mocks/compat_strlcpy.c`, and link-only
      stub globals/functions (`g_radio_*`, `ring_write`, `radio_session_fault`,
      `radio_try_publish_running`, `radio_codec_str`) directly in the test
      file for `stream_task`'s (never-called) references — see the test
      file's header comment. Infra smoke test passing (1/1); full host suite
      26/26; `idf.py build` clean.
- [x] **`resolve_redirect_location`** (pure, no mocking needed beyond strings) — **DONE**:
  - [x] Absolute location (`"http://host/path"`, contains `"://"`) → copied verbatim.
  - [x] Root-relative location (`"/path"`) → host/scheme spliced from `base_url`,
        path appended. Also covers `host_end` stopping at `?`/`#`, not running
        into the query/fragment.
  - [x] Any other relative form (e.g. `"path"`, no leading `/`, no scheme) → `false`.
  - [x] NULL or empty `location` → `false`.
  - [x] `location` (or the spliced result) that doesn't fit `out_sz` → `false`,
        not truncated.
  - [x] `base_url` with no `"://"` (malformed) + root-relative `location` → `false`.
  - [x] Boundary: absolute location exactly `out_sz - 1` chars → succeeds; exactly
        `out_sz` → fails (off-by-one check, this function does raw `memcpy`).
  8/8 new cases pass; full esp_i2s_source host suite 26/26.
- [x] **`redirect_target_allowed`** (on host, reduces to `url_policy_check_literal`
      — the `ESP_PLATFORM`-gated DNS re-check block compiles out entirely, so
      this is fully host-testable without network mocking) — **DONE**:
  - [x] Public IP literal in path → allowed.
  - [x] Private/loopback/link-local IP literal → blocked (mirror
        `url_policy.c`'s own test matrix; this just confirms the *wrapper* calls
        through correctly, not the policy itself).
  - [x] Hostname (non-literal) → allowed on host build (DNS check is device-only).
  5 new cases pass; full esp_i2s_source host suite 26/26.
- [x] **`connect_with_redirects`** (needs the fake HTTP client) — **DONE**:
  - [x] Single hop, 200 status → returns an open client positioned at that
        response, `*out_permanent` stays `false`.
  - [x] One valid redirect (3xx + allowed Location) → follows to hop 2, returns
        the terminal response.
  - [x] Redirect chain of exactly `MAX_REDIRECTS` (5) hops → succeeds on the
        final terminal response.
  - [x] Redirect chain of `MAX_REDIRECTS + 1` → fails, `*out_permanent = true`,
        `set_radio_error(..., "redirect_limit")`.
  - [x] 3xx with missing/empty `Location` header → `resolve_redirect_location`
        fails → `*out_permanent = true`, error `"redirect_malformed"`.
  - [x] 3xx redirecting to a blocked target (private IP) → `*out_permanent = true`,
        error `"redirect_url_blocked"` — **this is the actual SSRF-prevention
        assertion; it's the one test in this file that matters most. CONFIRMED
        PASSING: the code correctly rejects a redirect to 192.168.1.5 and
        never opens a connection to it (hops_consumed stays at 1).**
  - [x] `esp_http_client_init` returns NULL (alloc failure) → returns NULL,
        `RADIO_ERR_HTTP_CLIENT_ALLOC`.
  - [x] `esp_http_client_open` fails (non-ESP_OK) → cleans up, returns NULL,
        `*out_permanent` stays `false` (transient — caller should retry with backoff).
  - [x] 4xx/5xx terminal response → returned as-is (not treated as a redirect;
        caller classifies it).
  10 new cases pass (incl. the SSRF-prevention assertion); full host suite 26/26.
- [x] **`codec_from_ct`** and **`ci_contains`**: pure string-matching helpers,
      test directly — known content-type strings (`audio/mpeg`, `audio/aac`,
      `application/ogg`, etc.) map to the right codec enum; unknown → default/error;
      case-insensitivity (`ci_contains`'s whole purpose). — **DONE**. Correction
      to this bullet's own wording: `application/ogg` is NOT actually a
      supported codec in the real code — `codec_from_ct` only recognizes
      `mpeg`/`mp3` (→MP3) and `aac`/`mp4` (→AAC) substrings; anything else,
      including ogg, maps to `RADIO_CODEC_UNKNOWN`. That's a source-level
      capability gap (no Ogg/Vorbis or Opus support), not a test gap — worth
      a product decision, not a coverage task. 7 new cases pass; full host
      suite 26/26.
- [x] **`reconnect_delay_ms`**: table lookup — verify the exact schedule
      `{500,1000,2000,4000,8000,15000}` and that it clamps (doesn't index past
      the array) for `attempt` values at and beyond the table length. — **DONE**.
      2 new cases pass (incl. `UINT32_MAX`); full host suite 26/26. **I2S-1
      COMPLETE** — see the section header above for final numbers.

### I2S-2 — `radio_ring.c` SPSC byte rings (0% → 100% ✅ DONE)
> Achieved 100% line / 100% func (was 0%/0%) in a dedicated `test_radio_ring.c`
> + `test_radio_ring` target (just `radio_ring.c` + the semaphore mock — no
> HTTP/event-group/task mocks needed). 20 new cases, all passing; full host
> suite 27/27; `idf.py build` clean. Confirmed `ring_write`/`pcm_write` are
> **partial-write on a full ring, not rejecting** (returns bytes actually
> accepted, e.g. 0 when completely full) — the TODO's "rejected/partial"
> open question is resolved: it's partial. Also covered the prebuffer gate
> transitions (`g_radio_prebuffered` sets true at threshold, clears only on
> full drain, stays true on partial drain) since they live in this same file.
- [x] **`ring_write`**: writes into empty ring; write that exactly fills capacity;
      write attempted against a full ring (confirmed: partial, not rejected —
      0 bytes accepted when full, N bytes when N free); wraparound (tail past
      buffer end, continues from start).
- [x] **`radio_read`** (compressed-ring consumer): read less than available;
      read exactly available; read against an empty ring (returns 0
      immediately — mutex-guarded, no blocking); NULL `dst` → 0; wraparound
      read spanning the buffer-end boundary.
- [x] **`pcm_write`** / **`radio_pcm_read`** (decoded-PCM ring): same matrix as
      above, plus frame-alignment masking (`radio_pcm_read` rounds down to
      whole 4-byte stereo-s16 frames) and the prebuffer-gate transitions.
- [x] **Concurrency-adjacent (still host-testable without real threads):**
      interleaved write-then-read-then-write sequences that exercise
      `g_radio_ring_head`/`_tail`/`_count` bookkeeping staying consistent —
      assert `count` is always `(head - tail) mod cap` after each op.
- [x] Byte-exact round-trip: write a known pattern (not all-zero — catches
      off-by-one copies), read it back, assert identical bytes in order.

## P1 — meaningful surface, some genuinely harder to fully close

### I2S-3 — `ctrl.c` boot orchestrator helpers (21.6% → 46.9% ✅ DONE, short of 55% stretch goal)
> Achieved 46.9% line / 75% func (was 21.6%/43.8%). 19 new cases added to
> `test_ctrl_init.c` (already linked `ctrl.c`+`ctrl_sm.c`+`ctrl_cfg.c` — no
> new CMake target needed, just made the existing device-stub mock
> (`mocks/stubs/ctrl_device_stubs.c`) controllable via `mock_*` setters,
> defaults unchanged so the file's original 12 tests are unaffected). De-static'd
> `wifi_connected`, `status_running`, `resume_result_str`+`ctrl_resume_result_t`,
> `scan_result_str`+`ctrl_scan_result_t`, `do_action`, and `s_sm` into a new
> `ctrl_internal.h` (same convention as `radio_internal.h`). Full host suite
> 27/27; `idf.py build` clean. Short of the 55% stretch goal because `ctrl.c`
> is dominated by `orchestrator_task`/`scan_task`/`scan_wait_for_state`/
> `scan_wait_for_radio_start` — confirmed genuine FreeRTOS polling loops
> (`vTaskDelay` + no injectable clock), correctly out of scope, not a shortfall
> in the work done on the testable surface (which hit 75% function coverage).
The `_task` functions (`orchestrator_task`, `scan_task`) are FreeRTOS loops —
out of scope for host unit tests (see below). The **pure helpers** around them
are not:
- [x] **`wifi_connected`**: state-check helper — true/false against the relevant
      wifi_mgr state values it inspects.
- [x] **`status_running`**: same shape — verify it reflects orchestrator state
      correctly across the states it distinguishes, including NOT
      false-positiving on `UNDERRUN_RATE=`/`UNDERRUNS=` substrings containing
      `RUN=1`.
- [x] **`scan_result_str`** / **`resume_result_str`**: string-formatting helpers —
      one test per enum/result value they handle, confirming exact output text.
- [x] **`do_action`**: identified the actual signature (`ctrl_action_t
      do_action(ctrl_action_t act, const ctrl_cfg_t *cfg)`, using file-static
      `s_sm` — de-static'd that too). Tested every action branch: SEND_STATUS
      (connected → SEND_START/STARTING, disconnected → SEND_CONNECT/CONNECTING,
      and confirmed a bt_link TIMEOUT is treated as disconnected even with
      stale `RUN=1` data in the buffer), SEND_CONNECT (correct MAC in the
      command string), SEND_START, RESUME_RADIO's full branch matrix
      (volume-failed skips station lookup entirely, no-station-configured,
      station-not-found, play-enqueue-failed, full success — distinguished via
      observable side effects: whether/how `radio_play_async` was invoked,
      confirmed against the printed `DIAG|CTRL|RESUME_FAILED|reason=...` lines
      matching exactly), and the unknown/default action → `CTRL_ACT_WAIT`.
- [x] `scan_wait_for_radio_start` / `scan_wait_for_state`: confirmed these embed
      real `vTaskDelay` polling loops with no injectable clock — deferred,
      out of scope, per the note above.

### BT-1 — `audio_processor_config.c` runtime-reconfig API (16.7% → 88.3% ✅ DONE)
> Achieved 88.3% line / 100% func (was 16.7%/30%). 28 new cases added directly
> to `test_audio_processor_diag.c` (no new CMake target needed, as planned).
> `configure_i2s` was de-static'd (declared in `audio_processor_internal.h`) —
> it's genuinely unreachable via the public setters otherwise, since none of
> them ever pass NULL through; same de-static-for-testability convention this
> codebase already uses elsewhere. Added a small call-count/last-value spy to
> the SHARED test-only mock `audio_processor_core_logic_stubs.c` for
> `i2s_manager_init` and `nvs_storage_set_i2s_pins` (mirroring the existing
> `nvs_set_volume` spy pattern) — additive only, verified all 5 other binaries
> that link this shared mock still pass. Full host suite 74/74 (916 total
> cases, was 891); `idf.py build` clean.
>
> **Two things resolved that were open questions in this doc:**
> 1. `set_channels` validates its enum arg (rejects non-MONO/STEREO);
>    `set_bit_depth` does **not** (accepts any int) — confirmed asymmetric,
>    left as-is (a product decision, not fixed here — flagging for awareness).
> 2. The planned "`audio_processor_stop()` itself fails while was_running"
>    case has **no reachable trigger** in this harness: `audio_processor_stop()`
>    only fails on `!s_is_initialized`/`!s_is_running`, both already false
>    in that scenario. Documented in the test file rather than fabricating
>    a scenario the code can't produce.
- [x] **Uninitialized-state guard**: every setter (`set_sample_rate`, `set_mute`,
      `set_channels`, `set_bit_depth`, `set_i2s_pins`) returns
      `ESP_ERR_INVALID_STATE` when `!s_is_initialized` — one test per function
      (cheap, and it's the first branch in all five).
- [x] **`set_mute`**: toggles `s_audio_config.mute` true→false→true; confirm
      `audio_processor_get_config`/`get_status` reflect it.
- [x] **`set_channels`**: rejects anything other than `AUDIO_CHANNEL_MONO`/
      `_STEREO` with `ESP_ERR_INVALID_ARG`.
  - [x] No-op path: setting the same channel mode it's already in → `ESP_OK`,
        no reconfigure triggered (`i2s_manager_init` call count stays 0 —
        used as the reconfigure-happened signal instead of a stop/start spy,
        since a no-op structurally can't reach either).
  - [x] Changing while stopped → reconfigures without a stop/restart cycle.
  - [x] Changing while running → stops, reconfigures, restarts (`s_is_running`
        true throughout the observable before/after; `i2s_manager_init`
        called exactly once).
  - [x] `i2s_manager_init` failure during reconfigure → propagates the error.
        Noted: `s_audio_config` is updated BEFORE `i2s_manager_init` is
        called, so it reflects the new value even on failure — real behavior,
        not a rollback, documented rather than asserted-away.
- [x] **`set_bit_depth`**: same no-op/stopped/running/init-failure matrix as
      `set_channels`.
- [x] **`set_sample_rate`**: same matrix (the "stop fails" sub-case has no
      reachable trigger — see note above).
- [x] **`set_i2s_pins`**: stop/reconfigure/restart matrix; confirmed
      `nvs_storage_set_i2s_pins` is called with the exact 4 values passed in
      (new spy in the shared stub, since none existed).
- [x] **`configure_i2s`** (de-static'd): NULL `config` → `ESP_ERR_INVALID_ARG`.
- [x] **`audio_processor_get_config`**: round-trip after all 4 setters;
      rejects when `!s_is_initialized`; rejects NULL output (confirmed it
      does guard, checked after `!s_is_initialized`).

### BT-2 — `audio_processor_sync_diag.c` (0% → target 100%)
Small (41 lines), both functions untested:
- [ ] **`audio_processor_emit_sync_worker_diag`**: call it and assert it returns
      `ESP_OK` (or whatever its real contract is — read the function first,
      this wasn't inspected in the audit) and emits the expected diagnostic
      output/counters.
- [ ] **`mock_generate_i2s_audio`**: this looks like a host-test-only synthetic
      data generator; if it's genuinely test infrastructure rather than
      production logic, downgrade this file's priority — confirm which before
      spending time on it.

### BT-3 — `bt_manager.c` remaining gaps (70.1% → target 85%+)
- [ ] **`bt_manager_pair`** / **`bt_manager_connect`**: thin wrappers —
      `return (bt_pair(mac)==ESP_OK) ? 0 : -1` and the `bt_connect` equivalent.
      Sibling wrapper `bt_manager_start_pair` already has `UNIT_TEST`-gated
      forced-failure hooks (`bt_manager_forced_pair_failure`); these two don't
      but could use the identical pattern — success (0) and forced-failure (-1)
      cases, mirroring the existing hook style exactly.
- [ ] **`bt_manager_set_name`**: on host build (`#else` branch) it's a no-op
      returning 0 regardless of `name` — cheap test, currently unexercised.
- [ ] **`bt_get_device_list_snapshot`** / **`bt_get_paired_devices_snapshot`**:
      NULL `out` → `ESP_ERR_INVALID_ARG`; uninitialized `bt_ctx` →
      `ESP_ERR_INVALID_STATE` (with the lock correctly released before
      returning — check for a leaked lock on this path, that's a real bug class);
      success path copies the right bytes (compare against `bt_get_device_list()`/
      `bt_get_paired_devices()`'s direct-pointer view to confirm the snapshot
      matches).

## P2 — smaller / lower urgency

- [ ] **`esp_i2s_source` `bt_link.c`** (62.9%, 291 instrumented lines): the
      UART1 command-protocol glue to the WROOM32 — worth a pass after I2S-1/2/3,
      not before.
- [ ] **`esp_i2s_source` `i2s_out.c`** (69.5%, 305 lines): audit which
      uncovered functions are pure (pump/gain math, testable) vs. driver-call
      glue (`ESP_PLATFORM`-only, out of scope) before committing to a coverage
      target — don't assume the gap is closeable without checking first.
- [ ] **`esp_bt_audio_source` `audio_processor.c`** (62.3%, 228 lines):
      `audio_processor_cleanup_partial_init`, `_drain_ring`,
      `_get_work_buffer_bytes`, `_is_synth_mode_enabled`, `_is_wav_active`,
      `_set_dram_only`, `_set_synth_mode` are 0-call — mostly small
      state-accessor/flag functions, batch them into one task once P0/P1 land.
- [ ] **`esp_bt_audio_source` `audio_processor_diag.c`** (66.0%, 106 lines):
      `audio_processor_dump_tag_queue`, `diag_dump_bytes` untested — diagnostic
      dump formatting, low risk, low effort; good filler task.

---

## Out of scope (documented so nobody re-derives this)

- **FreeRTOS task-loop bodies**: `stream_task`, `decoder_task`, `orchestrator_task`,
  `scan_task` (esp_i2s_source); any `*_task` in esp_bt_audio_source. These are
  infinite loops around blocking I/O — not host-unit-testable without a much
  larger harness investment. Their *logic* is covered indirectly by testing the
  pure helper functions they call (which is exactly what P0/P1 above target).
- **`main.c` in both projects**: boot glue (NVS init, task creation, Kconfig
  wiring) — low logic density, better covered by the on-device boot smoke test
  than host mocks of `esp_wifi`/`esp_bt`/etc.
- **`test/host_test/esp_idf_stubs/bt/common/osi/{allocator,list}.c`**: these are
  host-test *stub implementations* of the Bluedroid OSI layer, not production
  code — their own coverage number doesn't indicate an application-logic gap.
- **`#ifdef ESP_PLATFORM`-only branches** (e.g. `redirect_target_allowed`'s DNS
  re-check): device-only by design (needs lwIP `getaddrinfo`); already
  documented as such in `url_policy.h`'s own comments. Don't chase these on host.

---

## Priority order

I2S-1 (security-relevant, P0) → I2S-2 (P0, clean/cheap) → BT-1 (P1, largest
single-file % gain) → I2S-3 → BT-2 → BT-3 → P2 batch.

Rationale: fix the one gap with real security exposure first: everything else
here is about confidence/regression-safety, not an active risk.
