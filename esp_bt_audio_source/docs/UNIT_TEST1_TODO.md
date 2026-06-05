# UNIT_TEST1_TODO.md

New unit test coverage identified in the June 2025 coverage gap analysis.
Items are ordered by priority: HIGH (critical untested logic), MEDIUM (missing edge cases), LOW (nice to have).

All new tests belong in `esp_bt_audio_source/test/host_test/` and must be registered
with CTest by adding entries to the CMakeLists.txt there.

---

## HIGH — Critical logic with no host tests

### TEST-1: `bt_pairing_store.c` — pairing state machine

**Files to create:**
- `test/host_test/test_bt_pairing_store.c`

**Why:** The pairing state machine (`bt_pairing_confirm`, `bt_pairing_submit_pin`,
`bt_pairing_clear_pending_flags`) has zero host test coverage. Pairing bugs are
timing-dependent and hard to catch on-device; the host mock infrastructure already
exists in `mocks/mock_gap.c`.

#### Subtasks

- [x] Understand the pairing state machine: read `bt_pairing_store.c` and
  `include/bt_pairing_store.h` in full; trace the data flow through
  `bt_pairing_handle_pin_request` → `bt_pairing_handle_ssp_confirm` →
  `bt_pairing_handle_auth_complete`
- [x] Set up test file scaffolding:
  - Copy the setUp/tearDown pattern from `test_bt_connection_manager.c` (includes,
    mock resets, bt_ctx init)
  - Include `bt_pairing_store.h`, `bt_manager_internal.h`, `mock_gap.h`
  - Add `bt_manager_test_set_initialized(1)` in setUp
- [x] **bt_pairing_confirm() — normal SSP flow**
  - [ ] Test: call `bt_pairing_confirm(valid_mac, true)` when a pending SSP request
    exists → assert `esp_bt_gap_ssp_confirm_reply` called with `confirm=true`
  - [ ] Test: call `bt_pairing_confirm(valid_mac, false)` → assert reply called with
    `confirm=false`
  - [ ] Test: call with NULL mac → assert returns error, no GAP call made
  - [ ] Test: call with no pending request (`bt_pairing_clear_pending()` first) →
    assert returns error or uses fallback pending addr
- [x] **bt_pairing_confirm() — device mismatch**
  - [ ] Test: pending addr is A, confirm arrives for addr B → assert correct behavior
    (reject or use pending addr, depending on implementation)
- [x] **bt_pairing_submit_pin() — normal PIN flow**
  - [ ] Test: call with valid MAC and valid PIN when PIN request pending → assert
    `esp_bt_gap_pin_reply` called with correct bytes
  - [ ] Test: PIN shorter than ESP_BT_PIN_CODE_LEN → assert padded or handled safely
  - [ ] Test: PIN exactly ESP_BT_PIN_CODE_LEN → passes through unchanged
  - [ ] Test: NULL pin → assert returns error, no GAP call
  - [ ] Test: NULL mac with pending addr set → assert falls back to pending addr
- [x] **bt_pairing_submit_pin() — NVS default PIN fallback**
  - [ ] Test: NULL pin with NVS default stored → assert NVS default PIN is used
  - [ ] Test: NULL pin with no NVS default → assert returns error or uses "0000"
- [x] **bt_pairing_clear_pending_flags()**
  - [ ] Test: call when both PIN and SSP flags set → both cleared after call
  - [ ] Test: call when neither flag set → no-op, no crash
  - [ ] Test: call when only one flag set → only that flag cleared
- [x] **bt_pairing_addr_is_zero()**
  - [ ] Test: all-zeros MAC → returns true
  - [ ] Test: one non-zero byte → returns false
  - [ ] Test: all-ones MAC (FF:FF:FF:FF:FF:FF) → returns false
- [x] **bt_pairing_handle_auth_complete() — success path**
  - [ ] Test: auth complete with status=0 (success) and valid MAC → assert
    `EVENT|PAIR|SUCCESS|<MAC>` event emitted via command interface
- [x] **bt_pairing_handle_auth_complete() — failure path**
  - [ ] Test: auth complete with non-zero status → assert `EVENT|PAIR|FAILED` emitted
- [x] **Device change during pending pairing**
  - [ ] Test: PIN request arrives for device A, then PIN request arrives for device B
    before first is resolved → assert second request replaces first cleanly (no hang)
- [x] **bt_pairing_format_mac()**
  - [ ] Test: valid 6-byte MAC → produces `XX:XX:XX:XX:XX:XX` format string
  - [ ] Test: all-zeros MAC → `00:00:00:00:00:00`
  - [ ] Test: NULL output buffer → no crash
- [x] Register in CMakeLists.txt and run `ctest --output-on-failure` to confirm all pass

---

### TEST-2: `cmd_handlers_system.c` — STATUS command and error fallbacks

**Files to create:**
- `test/host_test/test_cmd_handlers_system.c`

**Why:** `cmd_handle_status()` has no test for the fallback path when
`bt_get_streaming_info()` fails. The underrun rate calculation divides by
`total_callbacks`, which is zero on a fresh session — untested division-by-zero risk.

#### Subtasks

- [x] Read `cmd_handlers_system.c` in full to map all handlers and their
  dependencies (what mocks are needed)
- [x] Set up test file scaffolding:
  - Include `cmd_handlers.h`, `command_interface.h`, `mock_uart.h`, `bt_manager.h`
  - setUp: `mock_uart_init(115200); cmd_init(); mock_uart_reset_tx();`
  - tearDown: `cmd_deinit();`
  - Add weak override for `bt_get_streaming_info` to inject errors
- [x] **cmd_handle_status() — happy path**
  - [ ] Test: streaming info available, connected, running → assert response contains
    `OK|STATUS|...` with non-zero bytes_requested and valid underrun rate
  - [ ] Test: streaming info available, zero total_callbacks → assert underrun rate
    is 0.0 (not NaN/inf), response is valid
- [x] **cmd_handle_status() — streaming info unavailable**
  - [ ] Test: mock `bt_get_streaming_info()` to return `ESP_FAIL` → assert response
    still sends `OK|STATUS|...` with `STREAM_INFO=UNAVAILABLE` fallback, not a crash
  - [ ] Test: mock returns `ESP_ERR_INVALID_STATE` → same assertion
- [x] **cmd_handle_status() — not initialized**
  - [ ] Test: bt_manager not initialized → assert response contains initialized=0
- [x] **cmd_handle_help()**
  - [ ] Test: call with no params → assert response contains `OK|HELP|` prefix
  - [ ] Test: assert output is not empty (at least one command listed)
- [x] **cmd_handle_version()**
  - [ ] Test: version string returned in `OK|VERSION|<string>` format
  - [ ] Test: string is non-empty
- [x] **cmd_handle_spanlog() — no spans**
  - [ ] Test: spanlog with count=0 or empty ring → assert `OK|SPANLOG|...` with
    zero entries, no crash
- [x] **cmd_handle_spanlog() — allocation failure**
  - [ ] Test: mock platform_malloc to return NULL → assert graceful error response,
    not crash
- [x] Register in CMakeLists.txt and confirm all tests pass

---

### TEST-3: `audio_processor_state.c` — audio format helpers and state

**Files to create or extend:**
- `test/host_test/test_audio_processor_state.c` (new), OR extend
  `test/host_test/test_audio_processor_core_logic.c`

**Why:** `audio_bytes_per_sample()` is called in the audio pipeline for every
buffer allocation. The 24-bit and 32-bit paths have no coverage. Incorrect values
here cause silent miscalculation of buffer sizes.

#### Subtasks

- [x] Read `audio_processor_state.c` to identify all exported or accessible
  functions and their dependencies on global state
- [x] **audio_bytes_per_sample()**
  - [ ] Test: `AUDIO_BIT_DEPTH_16` → returns 2
  - [ ] Test: `AUDIO_BIT_DEPTH_24` → returns 4 (packed 32-bit) or 3 (check impl)
  - [ ] Test: `AUDIO_BIT_DEPTH_32` → returns 4
  - [ ] Test: invalid/unknown bit depth value → returns a safe default (2 or 0),
    no crash
- [x] **audio_get_runtime_work_bytes() — fallback path**
  - [ ] Test: `s_runtime_work_bytes` is 0 (uninitialized) → returns the compile-time
    default, not zero
  - [ ] Test: `s_runtime_work_bytes` set to a valid value → returns that value
- [x] **Weak stub `bt_manager_is_a2dp_connected()`**
  - [ ] Test: when not overridden (default stub), returns disconnected (0 or false)
  - [ ] Test: override with connected=true → audio source selection reflects connection
- [x] Register tests and confirm pass

---

## MEDIUM — Tested files with important gaps inside them

### TEST-4: `commands_helpers.c` — string utility edge cases

**Files to create:**
- `test/host_test/test_commands_helpers.c`

**Why:** `cmd_safe_append()`, `copy_truncated_identifier()`, and
`cmd_parse_log_level()` are used throughout the command interface but have no
dedicated tests for their boundary conditions. A buffer overflow in any of these
propagates into every command response.

#### Subtasks

- [x] Read `commands_helpers.c` in full to map functions and their signatures
- [x] **cmd_safe_copy()**
  - [ ] Test: normal copy shorter than dest → correct string, null-terminated
  - [ ] Test: source exactly fills dest (no room for null) → truncated, still
    null-terminated
  - [ ] Test: source longer than dest → truncated at dest_size-1, null-terminated
  - [ ] Test: NULL source → dest[0] = '\0' or no crash
  - [ ] Test: zero dest_size → no write, no crash
- [x] **cmd_safe_append()**
  - [ ] Test: append to empty dest → result equals suffix
  - [ ] Test: append to nearly-full dest (1 byte left) → truncates suffix, still
    null-terminated
  - [ ] Test: dest already full (strlen == dest_size-1) → no change, no crash
  - [ ] Test: zero-length suffix → dest unchanged
  - [ ] Test: NULL suffix → no crash
- [x] **copy_truncated_identifier()**
  - [ ] Test: source shorter than dst_size → copied verbatim, no ellipsis
  - [ ] Test: source exactly dst_size-1 chars → copied verbatim, null-terminated
  - [ ] Test: source longer than dst_size → truncated with `...` suffix, total
    length equals dst_size-1
  - [ ] Test: dst_size <= 4 (smaller than ellipsis) → define expected behavior and
    test it doesn't crash
  - [ ] Test: NULL source → no crash
- [x] **cmd_parse_log_level()**
  - [ ] Test: `"NONE"` → maps to `ESP_LOG_NONE`
  - [ ] Test: `"ERROR"` → maps to `ESP_LOG_ERROR`
  - [ ] Test: `"WARN"` → maps to `ESP_LOG_WARN`
  - [ ] Test: `"INFO"` → maps to `ESP_LOG_INFO`
  - [ ] Test: `"DEBUG"` → maps to `ESP_LOG_DEBUG`
  - [ ] Test: `"VERBOSE"` → maps to `ESP_LOG_VERBOSE`
  - [ ] Test: lowercase `"debug"` → case-insensitive match or returns false
  - [ ] Test: numeric string `"3"` → maps to correct level or returns false
  - [ ] Test: unknown string `"BANANA"` → returns false, output unchanged
  - [ ] Test: NULL input → returns false, no crash
- [x] **cmd_append_metadata()**
  - [ ] Test: appending key=value to empty buf → `key=value`
  - [ ] Test: appending second key=value → `key1=val1,key2=val2` (comma separator)
  - [ ] Test: append that would overflow buf → truncated, still null-terminated
  - [ ] Test: NULL key or NULL value → no crash
- [x] Register in CMakeLists.txt and confirm all tests pass

---

### TEST-5: `audio_processor_diag.c` — `apply_volume()` edge cases

**Files to extend:**
- `test/host_test/test_audio_processor_diag.c` (extend existing file)

**Why:** `apply_volume()` does clamped integer scaling on every audio buffer.
Incorrect clamping causes audio distortion or integer overflow. The 24-bit and
32-bit paths are unexercised.

#### Subtasks

- [x] Read `apply_volume()` in `audio_processor_diag.c` to understand the 16/24/32
  bit paths and clamping approach
- [x] **16-bit samples**
  - [ ] Test: `volume=0` → all samples become 0 (silence)
  - [ ] Test: `volume=100` → samples pass through unchanged
  - [ ] Test: `volume=50` → samples approximately halved
  - [ ] Test: large positive sample (INT16_MAX) with `volume=100` → no overflow,
    stays at INT16_MAX
  - [ ] Test: large negative sample (INT16_MIN) with `volume=100` → clamped to
    INT16_MIN (no wrap)
  - [ ] Test: sample that would overflow after scaling → clamped to INT16 range
- [x] **32-bit samples**
  - [ ] Test: same volume=0/50/100 scenarios as 16-bit
  - [ ] Test: INT32_MAX with volume=100 → no overflow
  - [ ] Test: INT32_MIN with any volume → clamped, not wrapped
- [x] **NULL buffer handling**
  - [ ] Test: NULL buf pointer → no crash (early return or assert)
- [x] **Zero-length buffer**
  - [ ] Test: size=0 → no crash, no writes
- [x] Add new test functions to the existing test_audio_processor_diag binary
  registration in CMakeLists.txt (if the new tests are in a new file, add a new
  CTest entry)

---

### TEST-6: `bt_connection_manager.c` — reconnect retry sequence

**Files to extend:**
- `test/host_test/test_bt_connection_manager.c` (extend existing file)

**Why:** The reconnect state machine has a `test_reconnect_results` mock array
that supports injecting sequences of success/failure, but multi-step retry
sequences (fail → retry → fail → give up) don't appear to be tested.

#### Subtasks

- [x] Read the reconnect logic in `bt_connection_manager.c` to understand how
  `test_reconnect_results` is consumed and what the retry limit is
- [x] Grep existing `test_bt_connection_manager.c` for reconnect tests to
  understand what IS already covered
- [x] **Retry-then-succeed sequence**
  - [ ] Test: inject [fail, fail, success] → assert connected after 3 attempts,
    correct number of `esp_a2d_source_connect` calls
- [x] **Retry-then-exhaust sequence**
  - [ ] Test: inject [fail, fail, fail, ...] up to retry limit → assert connection
    eventually transitions to DISCONNECTED with correct event emitted
- [x] **Immediate success**
  - [ ] Test: inject [success] → connected on first attempt, no retry
- [x] **Cancel during retry**
  - [ ] Test: call `bt_disconnect()` while in retry backoff → assert retry stops,
    state goes to DISCONNECTED cleanly

---

### TEST-7: `cmd_handlers_bt.c` — new DEBUG dispatch table

**Files to extend:**
- `test/host_test/test_cmd_handlers_bt.c` (extend existing file)

**Why:** The `cmd_handle_debug()` function was just refactored into a dispatch
table (DESIGN-3). The individual subcommand handlers should be verified to confirm
the refactoring preserved behavior, especially for the host-path (non-ESP_PLATFORM)
variants.

#### Subtasks

- [x] **MOCK_ON subcommand**
  - [ ] Test: DEBUG MOCK_ON → response `OK|DEBUG|MOCK_ON`, mock flag enabled
- [x] **MOCK_ADD subcommand**
  - [ ] Test: DEBUG MOCK_ADD AA:BB:CC:DD:EE:FF → event emitted, response OK
  - [ ] Test: DEBUG MOCK_ADD with no MAC param → `ERR|DEBUG|MOCK_ADD_MISSING`
- [x] **LOG subcommand**
  - [ ] Test: DEBUG LOG * INFO → `OK|DEBUG|LOG_SET|*:INFO`
  - [ ] Test: DEBUG LOG with fewer than 3 params → `ERR|DEBUG|LOG_MISSING`
  - [ ] Test: DEBUG LOG * BADLEVEL → `ERR|DEBUG|LOG_BAD_LEVEL`
- [x] **DRAM subcommand (host path)**
  - [ ] Test: DEBUG DRAM ON → `OK|DEBUG|DRAM_ON_MOCK` (host build)
  - [ ] Test: DEBUG DRAM OFF → `OK|DEBUG|DRAM_OFF_MOCK`
  - [ ] Test: DEBUG DRAM BANANA → `ERR|DEBUG|DRAM_BAD_PARAM`
  - [ ] Test: DEBUG DRAM (no param) → `ERR|DEBUG|DRAM_MISSING_PARAM`
- [x] **Unknown subcommand**
  - [ ] Test: DEBUG NOTASUBCMD → `ERR|DEBUG|UNKNOWN_SUBCMD|NOTASUBCMD`
- [x] **Missing subcommand**
  - [ ] Test: DEBUG (no params) → `ERR|DEBUG|MISSING_PARAM`

---

## LOW — Status code mapping and weak stub behavior

### TEST-8: `command_interface.c` — status code names

**Files to extend:**
- `test/host_test/test_commands.c` (extend existing file with a few assertions)

**Why:** `cmd_status_to_name()` is a simple switch but has no test. Any new status
code added without a switch case silently returns "UNKNOWN" — a test catches this.

#### Subtasks

- [x] **cmd_status_to_name() — all defined codes**
  - [ ] Test: `CMD_SUCCESS` → `"CMD_SUCCESS"` (or equivalent)
  - [ ] Test: `CMD_ERROR_INIT_FAILED` → non-NULL, non-empty string
  - [ ] Test: `CMD_ERROR_INVALID_PARAM` → non-NULL, non-empty string
  - [ ] Test: `CMD_ERROR_UNKNOWN` → non-NULL, non-empty string
  - [ ] Test: `CMD_ERROR_NOT_INITIALIZED` → non-NULL, non-empty string
  - [ ] Test: `CMD_ERROR_TOO_MANY_PARAMS` → non-NULL, non-empty string
  - [ ] Test: out-of-range value (e.g., 999) → returns non-NULL (fallback string),
    no crash

---

## CMakeLists.txt changes required

For each new test file above, add a corresponding entry in
`test/host_test/CMakeLists.txt`. Pattern to follow (copy from an existing entry):

```cmake
add_executable(test_bt_pairing_store test_bt_pairing_store.c)
target_link_libraries(test_bt_pairing_store unity bt_manager_host command_interface_host nvs_storage_host util_safe_host platform_shim_host)
add_test(NAME test_bt_pairing_store COMMAND test_bt_pairing_store)

add_executable(test_cmd_handlers_system test_cmd_handlers_system.c)
target_link_libraries(test_cmd_handlers_system unity command_interface_host bt_manager_host audio_processor_host nvs_storage_host util_safe_host platform_shim_host)
add_test(NAME test_cmd_handlers_system COMMAND test_cmd_handlers_system)

add_executable(test_audio_processor_state test_audio_processor_state.c)
target_link_libraries(test_audio_processor_state unity audio_processor_host util_safe_host platform_shim_host)
add_test(NAME test_audio_processor_state COMMAND test_audio_processor_state)

add_executable(test_commands_helpers test_commands_helpers.c)
target_link_libraries(test_commands_helpers unity command_interface_host util_safe_host platform_shim_host)
add_test(NAME test_commands_helpers COMMAND test_commands_helpers)
```

Adjust `target_link_libraries` based on what each test actually uses; only link
what is needed to avoid pulling in unrelated mocks.

---

## Tracking

| ID | Description | File(s) | Priority | Status |
|---|---|---|---|---|
| TEST-1 | bt_pairing_store.c — full state machine | test_bt_pairing_store.c | HIGH | [x] Done |
| TEST-2 | cmd_handlers_system.c — STATUS + fallbacks | test_cmd_handlers_system.c | HIGH | [x] Done |
| TEST-3 | audio_processor_state.c — format helpers | test_audio_processor_state.c | HIGH | [x] Done |
| TEST-4 | commands_helpers.c — string utilities | test_commands_helpers.c | MEDIUM | [x] Done |
| TEST-5 | audio_processor_diag.c — apply_volume() | extend test_audio_processor_diag.c | MEDIUM | [x] Done |
| TEST-6 | bt_connection_manager.c — reconnect retry | extend test_bt_connection_manager.c | MEDIUM | [x] Done |
| TEST-7 | cmd_handlers_bt.c — DEBUG dispatch table | extend test_cmd_handlers_bt.c | MEDIUM | [x] Done |
| TEST-8 | command_interface.c — status code names | extend test_commands.c | LOW | [x] Done |
