# AUTOSTART1_TODO.md

Auto-reconnect-on-boot feature: after a power cycle or reset, the ESP32
automatically reconnects to the most-recently-connected Bluetooth audio device
and starts streaming I2S audio — no manual CONNECT command required.

## Architecture summary

- **Phase 1 — NVS persistence**: add `nvs_storage_get_last_connected_mac()` /
  `nvs_storage_set_last_connected_mac()` to store the last-connected MAC across reboots.
- **Phase 2 — Save on connect**: hook into `bt_connection_manager.c` to write the MAC
  to NVS whenever a connection is successfully established.
- **Phase 3 — Boot-time reconnect**: in `main.c`, after `bt_manager_init()`, read the
  stored MAC and call `bt_connect()` when audio autostart is enabled.
- **Phase 4 — LAST_MAC command**: add a `LAST_MAC get|clear` serial command so the
  stored target can be inspected and reset over UART.
- **Phase 5 — Tests**: host unit tests for all new NVS functions, the command handler,
  and the boot-time reconnect logic.

---

## PHASE 1 — NVS persistence for last-connected MAC

**Files:** `components/nvs_storage/nvs_storage.c`, `components/nvs_storage/nvs_storage.h`

- [x] Read `nvs_storage_set_device_name()` and `nvs_storage_get_device_name()` as the
  pattern to follow (string key, same namespace `bt_audio_cfg`)
- [x] Add `nvs_storage_set_last_connected_mac(const char* mac)` to `nvs_storage.c`:
  - Validate `mac` is non-NULL and non-empty; return `ESP_ERR_INVALID_ARG` otherwise
  - Open namespace `bt_audio_cfg` read-write
  - Write string under key `"last_mac"` via `nvs_storage_set_str()`
  - Commit and close; return any error
- [x] Add `nvs_storage_get_last_connected_mac(char* buf, size_t buf_len)` to `nvs_storage.c`:
  - Validate `buf` non-NULL and `buf_len > 0`; return `ESP_ERR_INVALID_ARG` otherwise
  - Open namespace read-only
  - Read string under key `"last_mac"` via `nvs_storage_get_str()`
  - Return `ESP_ERR_NOT_FOUND` (propagated from get_str) when key absent
  - Close handle; return error code
- [x] Declare both functions in `nvs_storage.h` with doc comments
- [x] Update status for each subtask above as completed

---

## PHASE 2 — Persist MAC on successful connection

**Files:** `components/bt_manager/bt_connection_manager.c`

- [x] Read `bt_connection_state_handler()` (the function that calls
  `update_connection_state()`) to find the exact code path that fires when state
  transitions to `BT_CONNECTION_STATE_CONNECTED`
- [x] At the point where `s_last_connected_addr` is populated (line ~95) and state
  becomes CONNECTED, add:
  ```c
  nvs_storage_set_last_connected_mac(s_last_connected_addr);
  ```
  - Wrap in an `if (err != ESP_OK) ESP_LOGW(...)` so a failed NVS write is logged
    but never fatal to the connection
- [x] Add `#include "nvs_storage.h"` to `bt_connection_manager.c` if not already present
- [x] Verify the save happens only on genuine connections (not on reconnect-loop
  transitions) — `s_last_connected_addr` is already set correctly at the right point
- [x] Update status for each subtask above as completed

---

## PHASE 3 — Boot-time auto-reconnect in main.c

**Files:** `main/main.c`

- [x] Read `load_audio_boot_config()` and the section of `app_main()` where
  `bt_manager_init()` is called, to understand the current init ordering
- [x] Read `bt_manager_is_autostart_enabled()` to understand when audio autostart
  is active at boot
- [x] After `bt_manager_init()` succeeds (and after `cmd_init()` so the command
  interface is ready to log events), add a helper function
  `attempt_autoconnect_if_configured()`:
  - Read `nvs_storage_get_last_connected_mac(last_mac, sizeof(last_mac))`
  - If returns non-OK (not found or error): log INFO "no last-connected device, skipping auto-connect" and return
  - If `bt_manager_is_autostart_enabled()` returns false: log INFO "autostart disabled, skipping auto-connect" and return
  - Otherwise: log INFO "auto-connecting to last device: <mac>" and call `bt_connect(last_mac)`
  - `bt_connect()` is non-blocking; the connection manager's existing retry logic handles the rest
- [x] Call `attempt_autoconnect_if_configured()` in `app_main()` after both
  `bt_manager_init()` and `cmd_init()` have succeeded (control plane before data plane)
- [x] Ensure `bt_ok` guard is checked before calling so auto-connect is skipped if BT
  init failed (graceful degradation policy)
- [x] Update status for each subtask above as completed

---

## PHASE 4 — LAST_MAC serial command

**Files:**
- `components/command_interface/include/command_interface.h` (add `CMD_TYPE_LAST_MAC`)
- `components/command_interface/commands.c` (add parse entry)
- `components/command_interface/cmd_handlers_bt.c` (add handler)
- `components/command_interface/include/cmd_handlers.h` (declare handler)

**Protocol:**
```
> LAST_MAC get
< OK|LAST_MAC|<mac_address>         # e.g. OK|LAST_MAC|AA:BB:CC:DD:EE:FF
< OK|LAST_MAC|NONE                  # if no device stored yet

> LAST_MAC clear
< OK|LAST_MAC|CLEARED
```

- [x] Add `CMD_TYPE_LAST_MAC` to the `cmd_type_t` enum in `command_interface.h`
- [x] Add `"LAST_MAC"` parse branch in `cmd_parse()` in `commands.c` (follow the
  `CMD_TYPE_AUDIO_AUTOSTART` pattern — single token, rest goes to params)
- [x] Add `cmd_handle_last_mac()` declaration to `cmd_handlers.h`
- [x] Implement `cmd_handle_last_mac(const cmd_context_t *ctx)` in `cmd_handlers_bt.c`:
  - If `param_count < 1`: send `ERR|LAST_MAC|MISSING_PARAM` and return
  - Subcommand `"get"` (case-insensitive):
    - Call `nvs_storage_get_last_connected_mac(mac, sizeof(mac))`
    - If `ESP_OK`: send `OK|LAST_MAC|<mac>`
    - If `ESP_ERR_NOT_FOUND` or any error: send `OK|LAST_MAC|NONE`
  - Subcommand `"clear"` (case-insensitive):
    - Call `nvs_storage_set_last_connected_mac("")` (empty string acts as clear)
      OR add `nvs_storage_clear_last_connected_mac()` if you prefer an explicit erase
    - Send `OK|LAST_MAC|CLEARED`
  - Unknown subcommand: send `ERR|LAST_MAC|UNKNOWN_SUBCMD|<param>`
- [x] Wire `CMD_TYPE_LAST_MAC` into the `cmd_execute()` switch in `commands.c`
- [x] Add `LAST_MAC` entry to the help table in `cmd_handlers_system.c`
- [x] Update status for each subtask above as completed

---

## PHASE 5 — Tests

### 5a: NVS persistence tests

**File:** `test/host_test/test_nvs_storage_errors.c` (extend existing file)

- [x] Add `set_str_result` injectable mock (similar to `set_blob_result`) to control
  `nvs_storage_set_str()` failures
- [x] Test: `nvs_storage_set_last_connected_mac("AA:BB:CC:DD:EE:FF")` with healthy
  NVS → returns `ESP_OK`
- [x] Test: `nvs_storage_set_last_connected_mac(NULL)` → returns `ESP_ERR_INVALID_ARG`
- [x] Test: `nvs_storage_set_last_connected_mac("")` → returns `ESP_ERR_INVALID_ARG`
- [x] Test: `nvs_storage_set_last_connected_mac(...)` with commit failure injected →
  returns the commit error, does not crash
- [x] Test: `nvs_storage_get_last_connected_mac(buf, sizeof(buf))` when key is stored
  → returns `ESP_OK` and buf contains the MAC
- [x] Test: `nvs_storage_get_last_connected_mac(buf, sizeof(buf))` when key absent →
  returns `PLATFORM_ERR_STORAGE_NOT_FOUND`
- [x] Test: `nvs_storage_get_last_connected_mac(NULL, 18)` → returns `ESP_ERR_INVALID_ARG`
- [x] Test: `nvs_storage_get_last_connected_mac(buf, 0)` → returns `ESP_ERR_INVALID_ARG`
- [x] Run `ctest --output-on-failure` to confirm

### 5b: LAST_MAC command handler tests

**File:** `test/host_test/test_cmd_handlers_bt.c` (extend)

- [x] Test: `LAST_MAC get` when MAC stored → response `OK|LAST_MAC|AA:BB:CC:DD:EE:FF`
- [x] Test: `LAST_MAC get` when no MAC stored (inject NOT_FOUND from NVS) →
  response `OK|LAST_MAC|NONE`
- [x] Test: `LAST_MAC clear` → response `OK|LAST_MAC|CLEARED`
- [x] Test: `LAST_MAC` with no subcommand → response `ERR|LAST_MAC|MISSING_PARAM`
- [x] Test: `LAST_MAC badparam` → response `ERR|LAST_MAC|UNKNOWN_SUBCMD`
- [x] Run `ctest --output-on-failure` to confirm

### 5c: Boot-time auto-connect logic tests

**File:** `test/host_test/test_commands.c` or a new `test/host_test/test_autoconnect.c`

Note: The boot-time reconnect path in `main.c` is hard to unit-test directly. Test the
individual components and use an integration-style test that exercises the logic:

- [x] Test: when last MAC is stored in NVS and autostart is enabled, `bt_connect()` is
  called during the boot sequence (use the existing `bt_manager_test_get_scan_start_count`
  or add a `bt_manager_test_get_connect_calls()` hook to verify)
- [x] Test: when last MAC is stored but autostart is disabled → `bt_connect()` NOT called
- [x] Test: when no last MAC is stored → `bt_connect()` NOT called
- [x] Test: when BT init failed (`bt_ok = false`) → `bt_connect()` NOT called
- [x] Run `ctest --output-on-failure` to confirm

---

## Tracking

| ID | Description | File | Status |
|---|---|---|---|
| PHASE-1a | nvs_storage_set_last_connected_mac() | nvs_storage.c | [x] Done |
| PHASE-1b | nvs_storage_get_last_connected_mac() | nvs_storage.c | [x] Done |
| PHASE-1c | Declarations in nvs_storage.h | nvs_storage.h | [x] Done |
| PHASE-2a | Save MAC to NVS on CONNECTED transition | bt_connection_manager.c | [x] Done |
| PHASE-3a | attempt_autoconnect_if_configured() helper | main.c | [x] Done |
| PHASE-3b | Call helper after bt_manager_init in app_main | main.c | [x] Done |
| PHASE-4a | CMD_TYPE_LAST_MAC enum entry | command_interface.h | [x] Done |
| PHASE-4b | Parse branch in cmd_parse() | commands.c | [x] Done |
| PHASE-4c | cmd_handle_last_mac() implementation | cmd_handlers_bt.c | [x] Done |
| PHASE-4d | Wire into cmd_execute() and help table | commands.c + cmd_handlers_system.c | [x] Done |
| PHASE-5a | NVS persistence tests | test_nvs_storage_errors.c | [x] Done |
| PHASE-5b | LAST_MAC command handler tests | test_cmd_handlers_bt.c | [x] Done |
| PHASE-5c | Boot-time auto-connect logic tests | test_autoconnect.c | [x] Done |
