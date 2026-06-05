# BUG_FIXES1_TODO.md

Bug fixes and improvements identified during the June 2025 full code review.
Items are ordered by priority: Bugs first (must fix), then Warnings (should fix), then Design (worth addressing).

---

## BUGS — Must Fix

### BUG-1: Error code silently discarded in `nvs_storage_get_i2s_pins()`

**File:** `components/nvs_storage/nvs_storage.c` ~lines 92–124  
**Risk:** Function always returns `ESP_OK` even when pin reads fail; callers use garbage GPIO numbers silently.

- [x] Read the current implementation of `nvs_storage_get_i2s_pins()` and map out all four `nvs_storage_get_i32()` call sites
- [x] Refactor to propagate the first non-OK return value (or use accumulated error with chained `if (err == ESP_OK)` checks like `nvs_storage_set_i2s_pins()` already does)
- [x] Ensure the function returns the actual error code when any individual pin read fails
- [x] Add a `ESP_LOGE` log entry identifying which pin key failed to load
- [x] Add or update host unit tests in `test_nvs_storage` to assert that a simulated read failure returns a non-OK code (not `ESP_OK`)
- [x] Run `ctest` to confirm all nvs_storage tests still pass

---

### BUG-2: `nvs_storage_add_paired_device()` increments count before confirming write succeeded

**File:** `components/nvs_storage/nvs_storage.c` ~lines 381–391  
**Risk:** Device count grows even when the MAC blob write fails, producing a corrupt pairing list that silently accumulates phantom entries.

- [x] Read `nvs_storage_add_paired_device()` and identify the ordering of the blob write, count increment, and commit calls
- [x] Move the device count increment to *after* both the blob write and the commit succeed
- [x] If the blob write fails, return an error without modifying the count
- [x] If the commit fails, roll back the count (either don't increment, or decrement if increment happened first)
- [x] Add a host unit test that injects a blob-write failure and asserts the device count is unchanged afterward
- [x] Add a host unit test that injects a commit failure and asserts the device count is unchanged
- [x] Run `ctest` to confirm all nvs_storage tests pass

---

### BUG-3: Off-by-one when stripping trailing whitespace on empty input in `cmd_parse()`

**File:** `components/command_interface/commands.c` line ~126  
**Risk:** When the input string is empty (`strlen == 0`), `end = s + strlen(s) - 1` points one byte before the buffer, causing an out-of-bounds read/write.

- [x] Read the whitespace-stripping block in `cmd_parse()` and reproduce the underflow mentally with an empty-string input
- [x] Add a guard: if `strlen(s) == 0`, skip the trailing-whitespace strip entirely (or return immediately for the empty case)
- [x] Verify the fix also handles a string consisting entirely of whitespace (e.g., `"   "`) without underflowing
- [x] Add a unit test in `test_commands` with an empty string input to `cmd_parse()` asserting it returns an appropriate error without crashing
- [x] Add a test with a whitespace-only string input
- [x] Run `ctest` to confirm

---

### BUG-4: Thread-unsafe `strtok()` used for I2S pin parsing

**File:** `components/command_interface/cmd_handlers_audio.c` line ~299  
**Risk:** `strtok()` uses global libc state; concurrent calls from any context (including log formatting) corrupt the parse cursor silently.

- [x] Locate the `strtok()` call in the I2S pin parsing block
- [x] Replace with `strtok_r()` using a local `char *saveptr = NULL` variable
- [x] Verify there are no other `strtok()` (non-`_r`) calls elsewhere in the component (`grep -n 'strtok[^_]' components/command_interface/`)
- [x] Run the affected handler through the existing `test_cmd_handlers_audio` tests to confirm no regressions

---

### BUG-5: `atoi()` used for safety-relevant parameters (GPIO pins, sample rate)

**File:** `components/command_interface/cmd_handlers_audio.c` lines ~303 and ~409  
**Risk:** `atoi()` returns 0 for invalid input with no error signal; GPIO 0 is a real pin and 0 Hz is an invalid sample rate, so callers cannot detect malformed commands.

- [x] Replace the `atoi()` call at line ~303 (pin number parsing) with `cmd_parse_int()` and handle the error return
- [x] Replace the `atoi()` call at line ~409 (sample rate parsing) with `cmd_parse_int()` and handle the error return
- [x] For the pin case: if `cmd_parse_int()` returns an error, send `ERR|I2S_CONFIG|BAD_PARAM|invalid pin number` and return early
- [x] For the sample rate case: if `cmd_parse_int()` returns an error, send `ERR|SAMPLE_RATE|BAD_PARAM|invalid rate` and return early
- [x] Add unit tests for both bad-pin and bad-rate inputs asserting an error response is sent
- [x] Run `ctest` to confirm

---

### BUG-6: `uint32_t` streaming stats overflow after ~6–27 hours of audio

**File:** `components/bt_manager/bt_streaming_manager.c` lines ~117–120  
**Risk:** `bytes_sent`, `bytes_requested`, `bytes_produced`, and `bytes_silence` all wrap silently after ~4 GB. At 44.1 kHz stereo 16-bit this is about 6 hours; long-running streams produce meaningless diagnostics.

- [x] Change the type of `bytes_sent`, `bytes_requested`, `bytes_produced`, and `bytes_silence` from `uint32_t` to `uint64_t`
- [x] Update the `bt_streaming_stats_t` struct definition in the header accordingly
- [x] Check all print format strings (`ESP_LOGI`, `snprintf`, etc.) that reference these fields and update format specifiers from `%"PRIu32"` to `%"PRIu64"` (or equivalent)
- [x] Verify the spinlock critical section that reads these fields still covers all fields atomically (on 32-bit ESP32, 64-bit reads are not atomic; confirm the existing spinlock covers the full read)
- [x] Update any host test that checks stats values to reflect the new type
- [x] Run `ctest` to confirm

---

### BUG-7: Integer underflow risk in `beep_overlay_fill()`

**File:** `components/audio_processor/beep_manager.c` lines ~280–283  
**Risk:** `(size_t)(total_frames - frames_generated)` is unsigned arithmetic. If a scheduling race causes `frames_generated >= total_frames` at cast time, the result wraps to a huge positive value and the overlay writes far past the output buffer.

- [x] Read the `beep_overlay_fill()` function and identify the exact cast site
- [x] Replace the subtraction with an explicit clamp:
  ```c
  size_t frames_to_mix = (frames_generated >= total_frames)
      ? 0
      : (size_t)(total_frames - frames_generated);
  ```
- [x] Verify that `frames_to_mix == 0` is handled gracefully downstream (no division by zero, no zero-length copy that causes issues)
- [x] Add a unit test in `test_beep_manager_edge_cases` (or `test_audio_processor_beep_edge_cases`) that exercises the path where `frames_generated == total_frames` and asserts no crash and zero bytes mixed
- [x] Run `ctest` to confirm

---

### BUG-8: Unsafe cast of `bytes_per_ms * duration_ms` to `size_t` in beep setup

**File:** `components/audio_processor/audio_processor_beep.c` line ~118  
**Risk:** On ESP32, `size_t` is 32-bit. For large configurations (96 kHz, stereo, 32-bit, long duration), the `uint64_t` product silently truncates, producing a beep with the wrong number of bytes.

- [x] Read the beep byte-count calculation in `audio_processor_beep.c` around line 118
- [x] Before casting to `size_t`, add an overflow guard:
  ```c
  if (total_bytes > (uint64_t)SIZE_MAX) {
      ESP_LOGE(TAG, "beep duration too large for size_t");
      return ESP_ERR_INVALID_ARG;
  }
  size_t beep_bytes = (size_t)total_bytes;
  ```
- [x] Define or locate `SIZE_MAX` — include `<stdint.h>` or `<limits.h>` if not already present
- [x] Add a unit test that passes a configuration that would overflow 32-bit and asserts `ESP_ERR_INVALID_ARG` is returned rather than a silent truncation
- [x] Run `ctest` to confirm

---

### BUG-9: NULL dereference on `s_audio_ring` in the drop path

**File:** `components/audio_processor/audio_processor_read.c` line ~74  
**Risk:** `s_drop_ring_audio` can be true during deinit while `s_audio_ring` has already been freed, causing a NULL dereference when the drop path tries to use the ring.

- [x] Read `audio_processor_read.c` around line 74 to understand the exact sequence
- [x] Change the condition from `if (s_drop_ring_audio)` to `if (s_drop_ring_audio && s_audio_ring != NULL)`
- [x] Confirm whether the NULL check is sufficient or whether a memory ordering barrier is also needed (on ESP32 FreeRTOS, task notification or the existing critical section should be sufficient)
- [x] Add a unit test that simulates `s_drop_ring_audio = true` with a NULL ring pointer and asserts no crash
- [x] Run `ctest` to confirm

---

## WARNINGS — Should Fix

### WARN-1: Direct pointer returns to mutable device lists (TOCTOU)

**File:** `components/bt_manager/bt_manager.c` lines ~510–527  
**Risk:** `bt_get_device_list()` and `bt_get_paired_devices()` return raw pointers into live `bt_ctx` state that `BtAppTask` can modify concurrently.

- [x] Read both functions and their callers to understand how the returned pointers are used
- [x] Design a copy-based API modeled on the existing `bt_manager_get_status()` queue/semaphore pattern
  - Option A: Return a snapshot struct allocated by the caller (stack or heap)
  - Option B: Queue a read request to `BtAppTask` and wait for a copy of the list
- [x] Implement the chosen approach for `bt_get_device_list()`
- [x] Implement the chosen approach for `bt_get_paired_devices()`
- [x] Update all callers to use the new copy-based API
- [x] Remove or deprecate the old raw-pointer functions
- [x] Run `ctest` to confirm no regressions

---

### WARN-2: Silent discovery drop at 20-device limit with no log

**File:** `components/bt_manager/bt_scan.c` lines ~139–147  
**Risk:** When the scan result buffer fills, additional devices are silently discarded. Users scanning in dense environments see missing devices with no indication why.

- [x] Locate the `if (bt_ctx.discovered_devices.count < 20)` guard
- [x] Add an `ESP_LOGW` (or equivalent serial event `EVENT|SCAN|OVERFLOW`) on the else branch indicating the buffer is full and the device was dropped
- [x] Consider whether the 20-device cap is a `#define` constant — if hardcoded, extract it to a named constant (e.g., `BT_MAX_DISCOVERED_DEVICES`)
- [x] Run `ctest` to confirm

---

### WARN-3: Queue overflow in `bt_app_send_mgr_request()` causes silent semaphore stall

**File:** `components/bt_manager/bt_app_core.c` lines ~127–142  
**Risk:** If the request queue is full, the message is dropped but the caller's semaphore is never signaled, leaving the caller blocked for the full 100 ms timeout with no diagnostic.

- [x] Read `bt_app_send_mgr_request()` and map the queue-send → semaphore-wait flow
- [x] On queue send failure, log `ESP_LOGW` identifying which request type was dropped and that the queue was full
- [x] Return an explicit error code (e.g., `ESP_ERR_TIMEOUT` or `BT_ERR_QUEUE_FULL`) from the function rather than letting the caller silently time out
- [x] Update callers to handle the new error return gracefully
- [x] Run `ctest` to confirm

---

### WARN-4: Uninitialized `data` buffer in `cmd_handle_i2s_config()`

**File:** `components/command_interface/cmd_handlers_audio.c` lines ~359–395  
**Risk:** If no optional parameters (rate, bit depth, channels) are present, `data` is declared but never written, and an uninitialized buffer is passed to `cmd_send_response()`.

- [x] Locate the `data` buffer declaration in `cmd_handle_i2s_config()`
- [x] Add `data[0] = '\0';` immediately after the declaration
- [x] Alternatively, initialize with a meaningful default like `"applied"` if no format params were changed
- [x] Add a unit test that calls the handler with only pin parameters (no rate/bit_depth/channels) and asserts a valid (non-garbage) response is sent
- [x] Run `ctest` to confirm

---

### WARN-5: Unprotected static mock state in `cmd_handlers_bt.c`

**File:** `components/command_interface/cmd_handlers_bt.c` lines ~11–13  
**Risk:** `s_cmd_mock_enabled`, `s_cmd_mock_pairing_addr`, and `s_cmd_mock_passkey` are written by command handlers and read by other paths without synchronization.

- [x] Audit all read and write sites for these three variables
- [x] Determine if protection is needed in production (commands are typically serial on the device, but the field is technically accessible from test code running concurrently)
- [x] If protection is warranted: guard with a mutex or move to an atomic type (`_Atomic bool` / `atomic_bool`)
- [x] If protection is test-only concern: add a comment documenting the assumption that commands are serialized
- [x] Run `ctest` to confirm

---

### WARN-6: Silent NVS data destruction on firmware version mismatch

**File:** `components/nvs_storage/nvs_storage.c` lines ~35–46  
**Risk:** When a firmware update changes NVS layout, all user config (device name, paired list, volume, I2S pins) is silently erased and the device resets to defaults with no visible indication.

- [x] Locate the erase-and-retry block in `nvs_storage_init()`
- [x] Add a prominent `ESP_LOGW` immediately before the erase:
  ```c
  ESP_LOGW(TAG, "NVS version mismatch — erasing all NVS. Device settings reset to defaults.");
  ```
- [x] Consider also sending a serial event (e.g., `EVENT|SYSTEM|NVS_ERASED|version_mismatch`) so connected hosts can detect and re-apply configuration
- [x] Run `ctest` to confirm no regressions

---

### WARN-7: `platform_storage_handle_t` is `uint32_t`, cast to pointer in host implementation

**File:** `components/platform_shim/platform_storage.h` line ~47; `platform_storage_host.c` line ~123  
**Risk:** On a 64-bit host, casting a pointer to `uint32_t` and back is undefined behavior and loses the upper 32 bits of the address.

- [x] Change `platform_storage_handle_t` from `uint32_t` to `uintptr_t` in `platform_storage.h`
- [x] Verify the ESP32 implementation doesn't need changes (it uses integer NVS handles, which fit in `uintptr_t`)
- [x] Update the host implementation casts accordingly
- [x] Check all callers for any format strings or comparisons that assume a 32-bit type
- [x] Run `ctest` on the host to confirm

---

### WARN-8: `malloc()` bypasses `platform_malloc()` in `platform_sync_esp32.c`

**File:** `components/platform_shim/platform_sync_esp32.c` line ~30  
**Risk:** Breaks the platform abstraction's memory-capability model; semaphore wrapper memory could land in an unsuitable region if the abstraction is extended to target specific memory types.

- [x] Replace the `malloc()` call with `platform_malloc(sizeof(...), MALLOC_CAP_DEFAULT)` (or the appropriate capability flag)
- [x] Verify `platform_memory.h` is included in that translation unit
- [x] Grep for other bare `malloc()`/`calloc()`/`free()` calls within `platform_shim/` that should use the platform wrappers: `grep -rn '\bmalloc\b\|\bcalloc\b\|\bfree\b' components/platform_shim/`
- [x] Fix any additional sites found
- [x] Run `ctest` to confirm

---

## DESIGN — Worth Addressing

### DESIGN-1: Two parallel A2DP callback chains

**File:** `components/bt_manager/bt_manager.c`, `bt_events_a2dp.c`, `bt_connection_manager.c`  
**Risk:** A2DP events are processed by two separate handler chains. If one updates state before the other sees it, observers get stale or inconsistent data.

- [x] Trace both A2DP event paths end-to-end with `grep` and document them in a comment or in `docs/`
- [x] Identify which handler is the authoritative one for each state variable
- [x] Consolidate to a single A2DP event dispatch path — one registration, one handler, one state-update sequence
- [x] Update any tests that mock or stub A2DP callbacks to reflect the consolidated path
- [x] Run `ctest` to confirm

---

### DESIGN-2: Bluetooth state ownership split across three modules

**Files:** `bt_manager.c` (`bt_ctx`), `bt_connection_manager.c` (local state), `bt_streaming_manager.c` (local state)  
**Risk:** Overlapping state with no documented canonical source of truth. "Is audio streaming?" has at least two answers depending on which module you ask.

- [x] Document in `docs/` (or `bt_manager_internal.h`) which module owns which state fields and which is authoritative for each observable condition (connected, streaming, pairing in progress)
- [x] Identify any state that is duplicated across modules and decide which copy is canonical
- [x] Add assertions or static analysis guards (`assert()` on init) where derived state must agree with the canonical copy
- [x] Consider whether `bt_connection_manager` and `bt_streaming_manager` local state should be eliminated in favor of the `bt_ctx` canonical copy (or vice versa)

---

### DESIGN-3: `cmd_handle_debug()` is a 250-line god function with 10+ subcommands

**File:** `components/command_interface/cmd_handlers_bt.c` lines ~509–757  
**Impact:** Hard to test, review, and maintain; adding a new subcommand requires editing a massive function.

- [x] List all subcommands currently handled inside `cmd_handle_debug()`:
  `MOCK_ON`, `MOCK_ADD`, `MOCK_PAIR`, `BEEP_DIAG`, `WORKER_DIAG`, `AUDIO_DIAG`, `AUDIO_DIAG_SUMMARY`, `AUDIO_DIAG_PROBE`, `LOG`, `FORCE_BEEP`, `DRAIN_QUEUE`, `DRAM`
- [x] Create a private dispatch table (array of `{subcommand_string, handler_fn}`) in the same file
- [x] Extract each subcommand into its own `static cmd_result_t handle_debug_<subcommand>(cmd_context_t *ctx)` function
- [x] Replace the body of `cmd_handle_debug()` with a loop over the dispatch table
- [x] Verify all existing `test_cmd_handlers_bt` tests still pass
- [x] Run `ctest` to confirm

---

### DESIGN-4: Magic strings hardcoded throughout command interface

**Files:** All `cmd_handlers_*.c` and `commands.c`  
**Impact:** A typo in any status string (`"OK"`, `"ERR"`, `"INFO"`, `"EVENT"`) produces a silently malformed response that clients cannot parse.

- [x] Define string constants in `include/command_interface.h` (or a new `include/cmd_protocol.h`):
  ```c
  #define CMD_STATUS_OK    "OK"
  #define CMD_STATUS_ERR   "ERR"
  #define CMD_STATUS_INFO  "INFO"
  #define CMD_STATUS_EVENT "EVENT"
  ```
- [x] Do a project-wide search-and-replace for the bare string literals in all `cmd_handlers_*.c` and `commands.c`
- [x] Confirm no string literal occurrences remain (except in tests that assert exact protocol output)
- [x] Run `ctest` to confirm

---

### DESIGN-5: `CMD_MAX_PARAM_LEN 32` silently truncates Bluetooth device names

**File:** `components/command_interface/include/command_interface.h` line ~70  
**Impact:** Bluetooth device names up to 64+ bytes are common; `CONNECT_NAME` with a long name silently truncates, causing connection failures with no diagnostic.

- [x] Determine the longest Bluetooth device name the system should support (spec max is 248 bytes; practical limit is 64 bytes for most consumer devices)
- [x] Increase `CMD_MAX_PARAM_LEN` to at least 64 (preferably 128 for safety margin)
- [x] Review the stack impact of the increased parameter buffer size (`CMD_MAX_PARAMS * CMD_MAX_PARAM_LEN` bytes per `cmd_context_t`)
- [x] If stack impact is a concern, consider moving `cmd_context_t` parameters to heap allocation
- [x] Add a unit test that exercises `CONNECT_NAME` with a 50-byte device name and asserts the full name is preserved
- [x] Run `ctest` to confirm

---

### DESIGN-6: `malloc()`/`platform_malloc()` used inconsistently across the codebase

**Files:** Multiple `platform_shim/` files and other components  
**Impact:** Undermines the platform abstraction; on ESP32 some allocations may land in unsuitable memory regions.

- [x] Audit all dynamic allocation calls project-wide:
  ```bash
  grep -rn '\bmalloc\b\|\bcalloc\b\|\brealloc\b\|\bfree\b' components/ main/ \
    --include='*.c' --include='*.h' | grep -v '/test/' | grep -v '/build/'
  ```
- [x] For each bare `malloc()`/`calloc()` found: determine whether it should use `platform_malloc()` instead
- [x] For platform_shim files: all dynamic allocation should go through `platform_malloc()`
- [x] For component code: use `platform_malloc()` consistently
- [x] Update corresponding `free()` calls if the allocator changes
- [x] Run `ctest` to confirm

---

## Tracking

| ID | Description | File | Status |
|---|---|---|---|
| BUG-1 | Error code loss in nvs_storage_get_i2s_pins | nvs_storage.c | [x] Done |
| BUG-2 | Device count incremented before blob write confirmed | nvs_storage.c | [x] Done |
| BUG-3 | Off-by-one on empty string in cmd_parse | commands.c | [x] Done |
| BUG-4 | strtok() → strtok_r() in I2S pin parsing | cmd_handlers_audio.c | [x] Done |
| BUG-5 | atoi() → cmd_parse_int() for pin and rate | cmd_handlers_audio.c | [x] Done |
| BUG-6 | uint32_t stat counters overflow after ~6h | bt_streaming_manager.c | [x] Done |
| BUG-7 | Integer underflow in beep_overlay_fill | beep_manager.c | [x] Done |
| BUG-8 | size_t overflow cast in beep byte calculation | audio_processor_beep.c | [x] Done |
| BUG-9 | NULL deref on s_audio_ring in drop path | audio_processor_read.c | [x] Done |
| WARN-1 | Pointer returns to mutable device lists | bt_manager.c | [x] Done |
| WARN-2 | Silent discovery drop at 20 devices | bt_scan.c | [x] Done |
| WARN-3 | Queue overflow causes silent semaphore stall | bt_app_core.c | [x] Done |
| WARN-4 | Uninitialized data buffer in i2s_config handler | cmd_handlers_audio.c | [x] Done |
| WARN-5 | Unprotected static mock state | cmd_handlers_bt.c | [x] Done |
| WARN-6 | Silent NVS erasure on version mismatch | nvs_storage.c | [x] Done |
| WARN-7 | platform_storage_handle_t truncates pointer | platform_storage.h | [x] Done |
| WARN-8 | malloc() bypasses platform_malloc() in sync | platform_sync_esp32.c | [x] Done |
| DESIGN-1 | Two parallel A2DP callback chains | bt_manager / bt_events_a2dp | [x] Done |
| DESIGN-2 | BT state split across three modules | bt_manager / bt_connection_mgr | [x] Done |
| DESIGN-3 | cmd_handle_debug() god function | cmd_handlers_bt.c | [x] Done |
| DESIGN-4 | Magic strings in command interface | cmd_handlers_*.c | [x] Done |
| DESIGN-5 | CMD_MAX_PARAM_LEN 32 truncates BT names | command_interface.h | [x] Done |
| DESIGN-6 | Inconsistent malloc/platform_malloc usage | Multiple | [x] Done |
