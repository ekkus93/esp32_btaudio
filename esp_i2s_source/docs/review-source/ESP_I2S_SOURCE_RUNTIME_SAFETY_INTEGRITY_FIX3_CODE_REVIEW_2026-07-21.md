# ESP I2S Source Runtime Safety and Integrity — Code Review

**Snapshot reviewed:** `esp32_btaudio-master_2607211238.zip`  
**Scope:** `esp_i2s_source/` only  
**Review date:** 2026-07-21  
**Purpose:** Source document for the FIX3 specification and implementation TODO.

## 1. Verification baseline

The following host-side command was run from `esp_i2s_source/`:

```bash
./tools/verify_host.sh
```

Observed result:

- 19/19 C host-test executables passed with strict warnings enabled.
- 19/19 passed under AddressSanitizer.
- 19/19 passed under UndefinedBehaviorSanitizer.
- `tools/test_s3_gate_assert.py` passed.
- The overall command failed at `npm ci` because `web/package.json` and `web/package-lock.json` are not synchronized. The lockfile is missing `esbuild@0.28.1` and its platform packages.
- An ESP-IDF device build was not run in the review environment. Device-only code under `#ifdef ESP_PLATFORM` therefore remains unverified by compilation here.

The passing host tests are useful, but they do not cover the most dangerous paths: ESP-IDF task scheduling, I2S channel lifecycle, UART task teardown, Wi-Fi driver failures, NVS corruption, HTTP authorization, decoder stalls, or hardware shutdown.

## 2. Positive findings

The following work should be preserved:

1. The host-test build is offline, strict, and sanitizer-capable.
2. The I2S pending-block arithmetic correctly retains unwritten data across partial writes and consumes only real PCM accepted by the driver.
3. The I2S writer does not hold a spinlock around `i2s_channel_write()`.
4. I2S framing is explicit: 44.1 kHz, Philips format, stereo, 32-bit slots, signed 16-bit PCM in the upper half of each slot.
5. The I2S ring explicitly requires PSRAM.
6. I2S gain publication occurs only after successful NVS commit.
7. `bt_link` uses per-request completion semaphores and reference ownership rather than a single shared request object.
8. `bt_link` deep-copies asynchronous events and invokes subscribers outside the subscriber mutex.
9. Wi-Fi credential updates generally use a candidate-before-publish pattern.
10. Structured `DIAG|...` output provides a good foundation for hardware gates.

## 3. Release-blocking findings

### SEC-001 — Mutating HTTP endpoints are unauthenticated

**Severity:** P0 security  
**Evidence:**

- `components/web_ui/web_ui_auth.c:96-118` implements `web_ui_auth_check()`.
- No caller exists in the repository.
- `components/web_ui/web_ui.c:259-325` registers mutating handlers directly.
- `/api/console` forwards arbitrary WROOM32 commands in `components/web_ui/web_ui_bt.c:415-467`.

**Impact:** Any client able to reach port 80 can change Wi-Fi credentials, start or stop audio, alter stations, connect Bluetooth devices, and issue raw WROOM32 commands.

**Required disposition:** Fail closed. Every POST, PUT, and DELETE endpoint must require a valid bearer token. The raw console must never be an exception.

---

### SEC-002 — Authentication token generation and loading are invalid

**Severity:** P0 security/reliability  
**Evidence:** `components/web_ui/web_ui_auth.c:27-94`.

Defects:

- Thirty-two random binary bytes are stored as a C string. NUL bytes truncate it and arbitrary bytes may be invalid in HTTP headers or logs.
- `nvs_get_str()` is called with an output buffer while `required_len` is zero.
- Stored string length is compared with 32 even though the returned length includes the terminator.
- `ct_strcmp()` does not reject a candidate with extra trailing characters.
- Token persistence failure is logged but the function returns `ESP_OK` and enables an ephemeral token.
- `web_ui_start()` logs auth initialization failure and starts the server anyway.

**Required disposition:** Use a printable fixed-length encoding, exact-length constant-time comparison, persist-before-publish, and abort web startup on auth initialization failure.

---

### WEB-001 — Bluetooth web state is never initialized

**Severity:** P0 runtime  
**Evidence:**

- `web_ui_bt_init()` exists at `components/web_ui/web_ui_bt.c:470-478`.
- No call exists.
- `bt_get_h()` unconditionally takes `s_bt_mtx` at `web_ui_bt.c:297-317`.

**Impact:** `/api/bt` can dereference a null semaphore. Paired/discovered state and event subscriptions never work.

**Required disposition:** Initialize and deinitialize the BT web submodule explicitly. In degraded boot, endpoints must return 503 rather than dereference missing resources.

---

### I2S-001 — Start can report RUNNING after the writer already entered WAITING or FAULTED

**Severity:** P0 runtime/state integrity  
**Evidence:**

- Writer sets `I2S_EVT_WRITER_STARTED` before the first driver operation at `components/i2s_out/i2s_out.c:102-110`.
- Writer may then set `WAITING_FOR_CLOCK` or `FAULTED` at `i2s_out.c:120-153`.
- `i2s_out_start()` waits only for STARTED and then unconditionally stores RUNNING at `i2s_out.c:291-304`.

**Impact:** Callers can receive success and observe RUNNING after the writer has failed or is waiting for clocks.

**Required disposition:** Separate task-entry acknowledgement from first operational-state acknowledgement. The lifecycle owner must read the state established by the worker and never overwrite a newer state.

---

### I2S-002 — Start timeout leaves the module wedged in STARTING

**Severity:** P0 lifecycle  
**Evidence:** `components/i2s_out/i2s_out.c:294-300`.

On timeout, the code sets `stop_requested` and returns without joining the task, disabling the channel, or restoring a usable state. `stop()` and `deinit()` reject STARTING.

**Required disposition:** A timed-out start must run bounded cancellation and join logic. If join succeeds, disable the channel and return to IDLE. If join does not succeed, enter a join-pending fault state and retain every task-owned resource.

---

### I2S-003 — Stop timeout and channel-disable failure allow unsafe reclamation or false IDLE

**Severity:** P0 lifecycle/use-after-free  
**Evidence:**

- Stop timeout stores `FAULTED` at `i2s_out.c:329-338`.
- Deinit allows FAULTED and deletes the channel, event group, and ring at `i2s_out.c:349-380`.
- Stop always stores IDLE even when `i2s_channel_disable()` fails at `i2s_out.c:341-346`.

**Impact:** A still-running writer may access deleted objects. A channel that failed to disable is reported as idle and may be deleted illegally.

**Required disposition:** Add an explicit join-pending state and track whether the channel is enabled. Deinit is legal only after task exit acknowledgement and successful channel disable.

---

### RADIO-001 — `radio_deinit()` dereferences freed session memory and force-frees live task state

**Severity:** P0 use-after-free  
**Evidence:** `components/radio/radio.c:161-178`.

`radio_deinit()` saves `s_active_session`, calls `radio_stop_sync()`, and then reads the saved session. On successful stop, the session was freed. On timeout, `session_destroy_force()` deletes the event group and frees the session while tasks may still use it.

**Required disposition:** Remove force destruction. `radio_deinit()` must return an error and preserve resources when workers have not acknowledged exit.

---

### RADIO-002 — Radio publishes RUNNING before workers prove successful startup

**Severity:** P0 state integrity  
**Evidence:** `components/radio/radio.c:394-421`.

The play path creates the stream and decoder tasks and immediately publishes RUNNING. `RADIO_EVT_DECODER_STARTED` exists but is never set. A worker may fail or exit before the state publication.

**Required disposition:** Introduce explicit task-start and operational-ready acknowledgements. Use BUFFERING/STARTING until stream and decoder readiness is observed.

---

### RADIO-003 — Radio task-creation failure can free a session while the first worker is alive

**Severity:** P0 use-after-free  
**Evidence:** `components/radio/radio.c:400-435`.

When decoder task creation fails, the code requests stream stop, waits, ignores whether the wait timed out, and then deletes the session.

**Required disposition:** If the first worker does not acknowledge exit, retain the session and enter join-pending fault state.

---

### RADIO-004 — Large radio rings silently fall back from PSRAM to internal heap

**Severity:** P1 release blocker  
**Evidence:** `components/radio/radio.c:271-289`.

The decoded PCM ring is approximately 1 MiB. Internal fallback can exhaust or fragment memory required by Wi-Fi, TLS, HTTP, tasks, and codecs.

**Required disposition:** Compressed and decoded PCM rings are `PSRAM_REQUIRED`. Failure must return `ESP_ERR_NO_MEM`; unrestricted heap fallback is forbidden.

---

### RADIO-005 — Reconnect backoff stops working after the first successful start

**Severity:** P1 runtime  
**Evidence:** `components/radio/radio_stream.c:234-244`.

The wait includes `RADIO_EVT_STREAM_STARTED` without clearing the bit. Once set, subsequent waits return immediately, producing a tight reconnect loop.

**Required disposition:** Use a dedicated stop-aware delay or clear-on-exit event semantics. Add a timing test.

---

### RADIO-006 — Failed playlist resolution silently treats playlist text as audio

**Severity:** P1 quiet failure  
**Evidence:** `components/radio/radio_stream.c:92-128` and `components/radio/radio.c:355-358`.

**Impact:** `.m3u` or `.pls` text can be sent to the decoder, obscuring the real error and causing endless resynchronization.

**Required disposition:** If input is known or detected to be a playlist, resolution failure is terminal and visible. Direct audio URLs may pass through only when they are not classified as playlists.

---

### RADIO-007 — Decoder errors can produce endless silent playback

**Severity:** P1 release blocker  
**Evidence:** `components/radio/radio_decode.c:65-165`.

Examples:

- Decoder-open failures retry indefinitely.
- Decoder-info return status is ignored.
- Resampler initialization status is ignored.
- Resampler stalls do not publish `RADIO_ERR_RESAMPLER_STALLED`.
- A full no-progress input buffer causes silent one-byte drops with no bounded threshold.

**Required disposition:** Bound retries and resynchronization, publish exact error reasons, and transition to a visible fault after threshold exhaustion.

---

### RADIO-008 — Fresh-device prebuffer default is zero

**Severity:** P1 behavior  
**Evidence:** `components/radio/radio.c:644-655`.

The local load variable is initialized to the default, but the atomic is updated only when NVS read succeeds. A missing key leaves the atomic at zero.

**Required disposition:** Publish the default before reading NVS; distinguish missing key from NVS failure.

---

### BTLINK-001 — Initialization failure deletes resources under a running event task

**Severity:** P0 use-after-free  
**Evidence:** `components/bt_link/bt_link.c:343-369`.

The event-dispatch task is created first. If UART-task creation fails, cleanup immediately deletes the queue, mutex, and event group while the event task is alive.

**Required disposition:** Request stop, wait for task exit acknowledgement, and only then reclaim resources. If join times out, return a join-pending error and retain resources.

---

### BTLINK-002 — Active requests are leaked or abandoned during shutdown

**Severity:** P1 lifecycle  
**Evidence:** `components/bt_link/bt_link.c:221-282` and `390-403`.

The worker drains queued requests but does not complete and release `s_active`. Deinit simply assigns `s_active = NULL`.

**Required disposition:** Complete active and queued requests with a local transport cancellation error, signal callers, and release worker references exactly once.

---

### BTLINK-003 — Commands are accepted after workers have stopped

**Severity:** P1 quiet failure  
**Evidence:** `components/bt_link/bt_link.c:441-499`.

`bt_link_send()` checks only whether `s_send_mutex` exists. Requests can be queued after `bt_link_stop()` when no worker will consume them.

**Required disposition:** Require explicit RUNNING state before and after acquiring the send lock.

---

### BTLINK-004 — UART write failures are reported as peer timeouts

**Severity:** P1 diagnostics  
**Evidence:** `components/bt_link/bt_link.c:229-239`.

A short or failed local write only logs a warning. The request then waits for normal command timeout.

**Required disposition:** Complete the request immediately with the local transport error.

---

### WIFI-001 — NVS string lengths include the terminator and are used as payload lengths

**Severity:** P0 memory safety  
**Evidence:**

- `components/wifi_mgr/wifi_mgr.c:126-147`
- `wifi_mgr.c:264-286`
- `wifi_mgr.c:329-337`

A maximum 32-character SSID is returned with length 33 and can be copied into a 32-byte `wifi_config_t` field. AP loading also copies the terminator and writes another terminator one byte later.

**Required disposition:** Validate returned NVS lengths, subtract exactly one terminator byte, and reject malformed or oversized values.

---

### WIFI-002 — Fresh-device AP SSID is empty

**Severity:** P0 provisioning  
**Evidence:**

- Public contract names `ESP32-S3-Audio`.
- `derive_ap_password()` initializes only the password at `wifi_mgr.c:429-433`.
- `s_ap_ssid` is otherwise zero-initialized and changed only by an NVS override.

**Required disposition:** Initialize the documented default AP SSID and password before loading overrides.

---

### WIFI-003 — Wi-Fi lifecycle ignores driver failures and reports RUNNING anyway

**Severity:** P0 state integrity  
**Evidence:** `components/wifi_mgr/wifi_mgr.c:292-379`, `513-529`, `662-738`.

Ignored results include `esp_wifi_set_mode`, `esp_wifi_set_config`, `esp_wifi_start`, `esp_wifi_connect`, `esp_wifi_disconnect`, and mDNS operations. `s_wifi_started` becomes true after an unchecked call. `wifi_mgr_init()` marks RUNNING after a void action function.

**Required disposition:** Every driver operation that changes state must return and propagate `esp_err_t`. Runtime state may be published only after confirmed success.

---

### WIFI-004 — Public Wi-Fi mutators can dereference an uninitialized mutex

**Severity:** P0 degraded-boot crash  
**Evidence:** `components/wifi_mgr/wifi_mgr.c:547-593`.

**Required disposition:** Every public API must validate manager lifecycle before taking internal synchronization objects.

---

### STN-001 — Station CRC-32 always collapses to zero

**Severity:** P0 persistence integrity  
**Evidence:** `components/radio/stations.c:48-66`.

The implementation shifts right and then tests bit 31, which cannot remain set. The polynomial is never applied. The implementation returns zero for standard and random test inputs.

**Required disposition:** Implement standard reflected IEEE CRC-32 and add the `123456789 -> 0xCBF43926` known-answer test.

---

### STN-002 — Loaded station blobs are insufficiently validated

**Severity:** P0 persistence/memory safety  
**Evidence:** `components/radio/stations.c:116-139`.

The loader does not validate header size, duplicate or zero IDs, `next_id`, string terminators, URLs, duplicate URLs, or unused data invariants.

**Required disposition:** Validate the complete payload before publication.

---

### STN-003 — Corrupt station storage is silently overwritten with defaults

**Severity:** P1 data loss/quiet fallback  
**Evidence:** `components/radio/stations.c:191-232`.

Missing storage, corrupt storage, unsupported version, NVS errors, and migration failure all collapse into seeding defaults. The code may then overwrite V2 storage.

**Required disposition:** Seed only when both current and legacy keys are genuinely absent. Preserve corrupt or unsupported blobs and return a visible error.

---

### STN-004 — Station mutation paths close uninitialized NVS handles

**Severity:** P0 runtime  
**Evidence:** `components/radio/stations.c:325-335`, `378-388`, `425-434`, `471-480`.

`nvs_close(h)` is called even when `nvs_open()` failed.

**Required disposition:** Centralize persistence in a helper that closes only a successfully opened handle.

---

### URL-001 — Claimed SSRF restrictions are not implemented

**Severity:** P1 security  
**Evidence:**

- `components/radio/include/station_store.h:52-56` claims private/local destination rejection.
- `components/radio/station_store.c:13-32` checks only scheme, length, and control characters.
- Direct radio playback does not consistently call station URL validation.

**Required disposition:** Validate literals, DNS results, redirects, and reconnects. Reject unspecified, loopback, link-local, private, multicast, and broadcast addresses unless an explicit build option permits local streams.

---

### CTRL-001 — Orchestrator reads shared configuration without synchronization

**Severity:** P0 data race/state integrity  
**Evidence:** `components/ctrl/ctrl.c:52-143`, `374-393`.

The code comments claim task-local configuration but writes the copy back to the shared global. `do_action()` and the task read `s_cfg` without the mutex while HTTP/console setters may modify it.

**Required disposition:** Each operation uses an immutable snapshot copied under lock. Do not write snapshots back into the shared object.

---

### CTRL-002 — Control configuration is published before persistence succeeds

**Severity:** P1 persistence integrity  
**Evidence:** `components/ctrl/ctrl.c:405-438`.

Both sink update and station-note paths mutate `s_cfg` before `ctrl_cfg_save()`.

**Required disposition:** Build candidate, persist candidate, then publish candidate under lock. Preserve old runtime state on failure.

---

### CTRL-003 — Resume and scan report success despite failed operations

**Severity:** P1 quiet failure  
**Evidence:** `components/ctrl/ctrl.c:80-119`, `255-337`.

`CTRL_EV_RESUME_DONE` is emitted after volume timeout, missing station, or failed play enqueue. Scan continues after stop timeout and ignores disconnect, scan, reconnect, and volume command results. It logs “A2DP restored” regardless of outcome.

**Required disposition:** Every phase must have success/failure transitions. Abort unsafe later phases when prerequisites fail and publish an explicit final result.

---

### BOOT-001 — Degraded boot calls APIs that are not safe in a degraded state

**Severity:** P0 runtime  
**Evidence:** `main/main.c:214-272` and affected public APIs.

Degraded boot is reasonable, but the current API contracts do not consistently return `ESP_ERR_INVALID_STATE`. Examples include uninitialized Wi-Fi and BT web mutexes. Task-creation return values for probes/diagnostics are also ignored.

**Required disposition:** Introduce capability checks. Optional components may fail without rebooting, but every dependent path must return 503/invalid-state rather than dereference unavailable objects.

---

### TEST-001 — Frontend lockfile makes the verification command fail

**Severity:** P1 release gate  
**Evidence:** `web/package.json`, `web/package-lock.json`, `tools/verify_host.sh`.

**Required disposition:** Regenerate and commit the lockfile with the project’s supported Node/npm versions. `npm ci`, build, unit tests, and the embed step must pass from a clean checkout.

---

### TEST-002 — Device-only lifecycle code lacks direct regression coverage

**Severity:** P1 release gate

The current host suite does not compile or execute the actual I2S device glue, Wi-Fi manager device glue, station NVS loader, web auth/registration, or several shutdown interleavings.

**Required disposition:** Add host fakes/reducers where practical, compile device translation units against mocks, add failure-injection tests, run an ESP-IDF build after every phase touching device code, and require hardware evidence for final acceptance.

## 4. Fallback policy

### Forbidden fallbacks

- Starting the HTTP server after authentication initialization failure.
- Using an in-memory-only authentication token after persistence failure.
- Treating a playlist as direct audio after playlist resolution failure.
- Allocating PSRAM-required radio rings from unrestricted/internal heap.
- Overwriting corrupt station data with defaults.
- Reporting Wi-Fi RUNNING after an ignored driver failure.
- Reporting I2S IDLE after channel-disable failure.
- Force-freeing task-owned state after join timeout.
- Reporting control resume/scan success when one or more required phases failed.
- Silently truncating credentials, URLs, station names, JSON commands, or UART commands.

### Permitted fallbacks when visible and bounded

- I2S zero-fill on underrun.
- Retaining pending I2S data after a write timeout.
- Default runtime prebuffer when the NVS key is missing; a non-missing NVS error must be reported.
- Dropping asynchronous informational BT events when the event queue is full, with counters and rate-limited diagnostics.
- Degraded boot for independent optional components, provided all unavailable capabilities fail safely and visibly.
- Loading in-memory defaults for a corrupt legacy station blob only as a read-only recovery view; the corrupt blob must remain untouched and status must report corruption.

## 5. Recommended implementation order

1. Fix the verification baseline and add missing configuration symbols.
2. Enforce authentication and initialize the web BT submodule.
3. Repair I2S lifecycle ownership and state truthfulness.
4. Repair `bt_link` startup/shutdown/request ownership.
5. Repair station CRC, validation, migration, and corruption semantics.
6. Repair Wi-Fi string handling and driver-error propagation.
7. Repair radio lifecycle and PSRAM allocation.
8. Repair stream backoff, playlist handling, and decoder fault thresholds.
9. Repair control synchronization and truthful scan/resume outcomes.
10. Harden degraded boot and capability reporting.
11. Update the frontend auth flow and lockfile.
12. Run full host, ESP-IDF build, and hardware gates.
