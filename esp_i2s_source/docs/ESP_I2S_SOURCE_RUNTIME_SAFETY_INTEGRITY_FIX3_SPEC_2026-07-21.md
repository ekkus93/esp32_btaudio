# ESP I2S Source Runtime Safety, Security, and State Integrity — FIX3 Specification

**Status:** Implementation specification  
**Date:** 2026-07-21  
**Applies to:** `esp_i2s_source/`  
**Review source:** `docs/review-source/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_CODE_REVIEW_2026-07-21.md`  
**Implementation plan:** `docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_TODO_2026-07-21.md`

## 1. Normative status and precedence

This document defines the required behavior for the FIX3 repair. It does not replace the overall product architecture in `docs/SPEC.md`; it overrides older documents only where they conflict with the runtime-safety, authentication, persistence, lifecycle, fallback, validation, and verification requirements below.

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

No implementation may be declared complete solely because host tests pass. Any change under `#ifdef ESP_PLATFORM` MUST also pass an ESP-IDF v5.5.1 build. Final completion additionally requires the device gates in §15.

## 2. Goals

FIX3 has six goals:

1. Prevent task/resource use-after-free and shutdown races.
2. Make lifecycle states truthful: a state reports observed reality, not requested intent.
3. Enforce authenticated mutation of the web API.
4. Make persistent data transactional, validated, and non-destructive on corruption.
5. Remove silent truncation, unsafe fallback, and quiet failure behavior.
6. Expand verification so the repaired device-only paths cannot regress unnoticed.

## 3. Non-goals

FIX3 does not redesign the I2S electrical contract, replace the radio codec library, add a new UI framework, change the UART command protocol on the WROOM32, or require automatic trading between firmware versions. It may add internal lifecycle states, status fields, error codes, tests, and configuration symbols needed to make existing behavior safe.

## 4. Global invariants

### 4.1 Ownership and reclamation

1. A task-owned object MUST NOT be freed, deleted, reset, or reused until every task that can access it has acknowledged exit.
2. A join timeout MUST retain all task-owned resources and transition the subsystem to an explicit `*_JOIN_PENDING` or equivalent state.
3. A timeout is not permission to force-delete a task, event group, queue, semaphore, ring, channel, session, or request.
4. Task handles are owned by lifecycle/control code. Worker tasks SHOULD set exit event bits and MUST NOT be the sole source of truth for safe reclamation.
5. A request visible to a worker MUST retain a worker reference until the worker completes or cancels it.

### 4.2 Truthful state publication

1. A subsystem MUST publish a success state only after the underlying operation has returned success or an explicit worker acknowledgement proves success.
2. A newer worker state MUST NOT be overwritten by a stale lifecycle caller.
3. If a driver operation fails, the public state and return code MUST preserve that failure.
4. Expected transient conditions such as missing external I2S clocks MAY use a non-fault state such as `WAITING_FOR_CLOCK`; they MUST NOT be mislabeled RUNNING or terminal error.

### 4.3 Transactional publication

For persistent settings and data:

1. Build and validate a complete candidate in local memory.
2. Persist the candidate.
3. Commit it.
4. When required by the schema, read it back and validate it.
5. Publish it to runtime state only after persistence succeeds.
6. On failure, preserve the previous runtime state and return the exact error.

A component MAY apply hardware after persistence only if it can roll back persistence on hardware failure or explicitly enter a fault state that reports the runtime/persistent mismatch. Silent divergence is forbidden.

### 4.4 Input integrity

The firmware MUST reject, not truncate:

- Wi-Fi SSIDs and passwords.
- AP SSIDs and passwords.
- Station names and URLs.
- Radio play URLs.
- HTTP JSON bodies and string fields.
- UART/WROOM32 commands.
- Bearer tokens.

Every bounded-string validator MUST inspect one byte beyond the maximum permitted payload when possible, for example `strnlen(value, max + 1)`, so exact overflow is detectable.

### 4.5 Error visibility

1. A logged failure MUST also be represented by a return code, state, status field, operation result, or machine-readable diagnostic when the caller needs to act on it.
2. “Best effort” is forbidden for security, lifecycle ownership, persistence integrity, and required state transitions.
3. A degraded feature MUST return `ESP_ERR_INVALID_STATE`, HTTP 503, or a specific equivalent. It MUST NOT crash or pretend success.
4. Secrets MUST be redacted from normal logs and status responses.

### 4.6 PSRAM-required memory

The following buffers are `PSRAM_REQUIRED`:

- I2S source ring.
- Radio compressed-input ring.
- Radio decoded-PCM ring.

They MUST be allocated with SPIRAM capability. If allocation fails, initialization MUST return `ESP_ERR_NO_MEM`, record the failed buffer name and requested size, and leave the subsystem in a visible unavailable/fault state. Unrestricted/internal-heap fallback is forbidden.

## 5. Web authentication and route security

### 5.1 Token format

1. Generate 32 cryptographically random bytes with `esp_fill_random()`.
2. Encode them as 64 lowercase hexadecimal characters.
3. Store the token as a 65-byte NUL-terminated C string.
4. The token MUST contain only `[0-9a-f]` and MUST have exactly 64 characters.
5. The NVS string length is therefore exactly 65 including the terminator.

Binary bytes MUST NOT be used directly as a C string or HTTP header value.

### 5.2 Token initialization

`web_ui_auth_init()` MUST behave as follows:

- If the token key is absent on first boot, generate and persist a new token.
- If a valid token exists, load it.
- If the namespace/key read fails for any reason other than “not found,” return that error.
- If an existing token is malformed, return a corruption/invalid-data error. Do not silently replace it.
- Publish `s_token_ready=true` only after a valid token has been loaded or a newly generated token has been committed successfully.
- Print `AUTH|BOOTSTRAP_TOKEN|<token>` exactly once for a newly committed token.
- Never print a token merely held in uncommitted RAM.

If authentication initialization fails, `web_ui_start()` MUST return the error before starting the HTTP server.

### 5.3 Comparison and header parsing

1. Require an `Authorization` header with exact form `Bearer <64-hex-token>`.
2. Reject missing, oversized, undersized, non-hex, prefixed, suffixed, or whitespace-extended values.
3. Compare exactly 64 bytes in constant time only after exact length validation.
4. Do not expose token validity through different response bodies or timing-sensitive early character comparison.

### 5.4 Protected routes

Every HTTP `POST`, `PUT`, and `DELETE` route MUST pass through one centralized authorization dispatcher before its feature handler runs. This includes:

- `/api/wifi`
- `/api/apmode`
- `/api/tone`
- `/api/radio`
- `/api/stations`
- `/api/scan`
- `/api/volume`
- `/api/prebuffer`
- `/api/btvolume`
- `/api/ctrl`
- `/api/bt`
- `/api/console`

No feature handler may rely on the frontend to enforce authorization.

Unauthorized requests MUST return:

- HTTP 401.
- `WWW-Authenticate: Bearer`.
- JSON error code `AUTH_REQUIRED`.
- No secret or token fragment.

GET endpoints MAY remain unauthenticated unless they expose sensitive data. They MUST never include the token, Wi-Fi password, AP password, or secret material.

### 5.5 Bootstrap and rotation

The initial usable flow is:

1. First boot commits the token.
2. Firmware prints the bootstrap marker once over local USB serial.
3. The user enters the token in the web UI.
4. The frontend sends it only in the Authorization header.

Token rotation MUST be available through a local physical-presence path. For this board revision, a local USB-console command such as `AUTH ROTATE` is acceptable and preferred unless a dedicated non-bootstrapping application button is documented. Rotation MUST:

- Generate and commit a candidate token.
- Publish the new token only after commit.
- Invalidate the old token immediately after publication.
- Print the new token once.
- Emit `AUTH|TOKEN_ROTATED` without the token.
- Never be exposed as an unauthenticated network operation.

### 5.6 Web BT submodule lifecycle

1. `web_ui_bt_init()` MUST return `esp_err_t`.
2. It MUST create its mutex before any handler is registered.
3. If `bt_link` is available, it MUST subscribe and record the subscription handle/result.
4. If `bt_link` is unavailable, the web server MAY still start, but BT-dependent endpoints MUST return HTTP 503 with `BT_LINK_UNAVAILABLE`.
5. A `web_ui_bt_deinit()` function MUST unsubscribe, stop/join any BT-web helper tasks, and delete the mutex only after those tasks exit.
6. `web_ui_stop()` MUST call web submodule cleanup before destroying shared resources.

## 6. I2S output lifecycle

### 6.1 States

The I2S state machine MUST include at least:

- `UNINITIALIZED`
- `IDLE`
- `STARTING`
- `RUNNING`
- `WAITING_FOR_CLOCK`
- `STOPPING`
- `FAULTED`
- `FAULTED_JOIN_PENDING`

`FAULTED_JOIN_PENDING` means a worker may still access resources. Deinit is forbidden in this state.

The implementation MUST separately track whether the I2S channel is currently enabled. The channel-enabled flag may become false only after `i2s_channel_disable()` returns `ESP_OK`.

### 6.2 Worker acknowledgements

Use distinct event bits for:

- Writer task entered.
- Writer reached its first operational state.
- Writer exited.

The writer’s first operational state is one of:

- `RUNNING` after a successful write.
- `WAITING_FOR_CLOCK` after `ESP_ERR_TIMEOUT`.
- `FAULTED` after a non-timeout driver error.

The writer MUST set the operational-ready bit after publishing that state. `i2s_out_start()` MUST read that published state and MUST NOT unconditionally store RUNNING.

### 6.3 Start behavior

1. Start is legal from IDLE only; it is idempotent from RUNNING or WAITING_FOR_CLOCK.
2. Enable the channel and set the channel-enabled flag only after success.
3. If task creation fails, disable the channel. If disable succeeds, return to IDLE. If disable fails, enter FAULTED and retain the channel.
4. Wait for first operational state or writer exit with a bounded timeout.
5. Return `ESP_OK` for RUNNING or WAITING_FOR_CLOCK.
6. Return the recorded driver error for FAULTED.
7. On start timeout, request stop and run bounded join/disable cleanup. Do not leave STARTING indefinitely.
8. If join times out, enter `FAULTED_JOIN_PENDING` and preserve all resources.

### 6.4 Stop behavior

1. Stop is idempotent from IDLE.
2. Stop is legal from STARTING, RUNNING, WAITING_FOR_CLOCK, FAULTED, and JOIN_PENDING recovery attempts.
3. Request worker stop and wait for writer exit.
4. On wait timeout, retain resources and publish `FAULTED_JOIN_PENDING`.
5. After exit acknowledgement, clear the lifecycle-owned task handle.
6. Disable the channel.
7. Publish IDLE only after disable succeeds.
8. If disable fails, publish FAULTED, keep `channel_enabled=true`, and allow a later stop/recovery call to retry disable.

### 6.5 Deinitialization

Deinit is legal only when:

- State is IDLE or UNINITIALIZED.
- Writer exit has been acknowledged.
- Task handle is null from lifecycle-owner cleanup.
- Channel-enabled is false.

Deinit MUST return `ESP_ERR_INVALID_STATE` otherwise. It MUST check and propagate `i2s_del_channel()` failure before deleting dependent resources.

### 6.6 I2S diagnostics

- Missing-clock write timeouts increment `write_timeouts` and set WAITING_FOR_CLOCK, but do not overwrite `last_error` with a terminal error.
- `last_error` records the most recent non-transient driver/lifecycle failure.
- A later confirmed successful start MAY clear `last_error`.
- `i2s_out_get_stats()` MUST populate `ring_peak` from the ring implementation.
- The audio producer MUST back off in both FAULTED and JOIN_PENDING states.

## 7. Bluetooth UART link lifecycle

### 7.1 Explicit lifecycle

`bt_link` MUST have a serialized lifecycle state such as:

- `UNINITIALIZED`
- `STARTING`
- `RUNNING`
- `STOPPING`
- `STOPPED`
- `FAULTED_JOIN_PENDING`

`bt_link_send()` is legal only in RUNNING.

### 7.2 Startup ordering and cleanup

1. Create passive resources first: queues, mutexes, event group, session state.
2. Create worker tasks.
3. Wait for task-entry acknowledgements before publishing RUNNING.
4. If any later task creation fails, request stop for already-created tasks and wait for exit acknowledgement.
5. Reclaim resources only after all created tasks have exited.
6. If join times out, retain every shared object and return timeout/join-pending state.

### 7.3 Request completion

Each request MUST carry:

- Command state.
- A local transport `esp_err_t` result.
- Result/data fields.
- Completion semaphore.
- Exact reference ownership.

On UART write failure, shutdown cancellation, or local lifecycle rejection, complete the request immediately with the local transport error. Do not wait for a peer timeout.

During stop:

- Complete the active request with `ESP_ERR_INVALID_STATE` or a dedicated cancellation error.
- Complete every queued request similarly.
- Signal each caller exactly once.
- Release the worker reference exactly once.

### 7.4 Send gating

`bt_link_send()` MUST verify RUNNING:

1. Before acquiring the send mutex.
2. Again after acquiring it, to close the stop race.

It MUST reject invalid or overlong commands without truncation.

### 7.5 Event dispatch

Event queue overflow MAY drop informational events, but the component MUST maintain a drop counter and rate-limit warnings. Subscriber callbacks remain outside the subscriber mutex.

## 8. Station persistence and URL policy

### 8.1 CRC-32

Use standard reflected IEEE CRC-32:

- Initial value `0xFFFFFFFF`.
- Polynomial `0xEDB88320`.
- Process least-significant bit first.
- Final XOR `0xFFFFFFFF`.

The known-answer vector `"123456789"` MUST equal `0xCBF43926`.

### 8.2 V2 blob validation

Before publishing a V2 station blob, validate all of the following:

- Exact blob size.
- Magic.
- Version.
- Exact header size.
- Exact payload size.
- CRC.
- `count` in `[0, STATION_MAX]`.
- Every active name and URL has a NUL terminator within its field.
- Every active URL passes syntactic URL validation.
- Every active ID is nonzero.
- IDs are unique.
- URLs are unique.
- `store.next_id` is nonzero and strictly greater than every active ID, except a documented wraparound fault is returned.
- Header `next_id` equals `store.next_id`, or remove the redundant header field in a versioned schema update.
- Unused entries are ignored but SHOULD be zeroed when writing.

Validation MUST occur on a local candidate before assigning `s_store`.

### 8.3 Missing, corrupt, and unsupported storage

These cases are distinct:

- **Both V2 and legacy keys absent:** seed defaults, persist them, read back, validate, then publish.
- **Valid V2 present:** load it.
- **V2 present but corrupt/unsupported:** preserve it, return a visible error, do not seed over it, and do not attempt legacy migration as if V2 were absent.
- **V2 absent, valid legacy present:** migrate.
- **V2 absent, legacy present but invalid:** preserve legacy data, report corruption, and do not overwrite it.
- **NVS unavailable/error:** return the NVS error.

The module MUST NOT mark itself initialized after a failed initial persist or failed validation.

A read-only in-memory recovery view MAY be added, but status must explicitly say `persistence_corrupt=true` or equivalent and mutation must remain disabled until the user chooses recovery.

### 8.4 Legacy migration

The legacy schema MUST be declared explicitly as the historical `STA1` shape:

```c
typedef struct {
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
} station_v1_t;

typedef struct {
    uint32_t magic;
    int32_t count;
    station_v1_t items[STATION_MAX];
} stations_blob_v1_t;
```

Migration requirements:

1. Read the stored blob size before selecting a parser.
2. Require exact legacy size and `magic == 0x53544131`.
3. Validate count, terminators, URLs, and uniqueness.
4. Preserve order, names, and URLs.
5. Assign stable IDs `1..count`; set `next_id=count+1`.
6. Convert old `ctrl` last-station index to the corresponding stable ID. Index `0` maps to ID `1`; negative or out-of-range maps to none.
7. Write only the new `stations_v2` key.
8. Commit, read back, and validate before publication.
9. Retain the old key for rollback evidence.
10. On migration persistence failure, keep validated legacy data available read-only where possible and report `migration_pending`; do not damage the legacy key.

### 8.5 Persistence helper

All station mutations MUST use one persistence helper that:

- Opens NVS.
- Writes the blob.
- Commits.
- Closes only if open succeeded.
- Returns the exact error.

No mutation path may directly repeat open/set/commit/close logic.

### 8.6 URL syntax and SSRF policy

All station additions, updates, direct radio play requests, playlist-resolved URLs, redirects, and reconnects MUST pass the same policy.

By default, reject destinations in:

- IPv4 unspecified `0.0.0.0/8`.
- IPv4 loopback `127.0.0.0/8`.
- IPv4 link-local `169.254.0.0/16`.
- IPv4 private `10/8`, `172.16/12`, `192.168/16`.
- IPv4 multicast `224/4`.
- Limited broadcast `255.255.255.255`.
- IPv6 unspecified `::`.
- IPv6 loopback `::1`.
- IPv6 link-local `fe80::/10`.
- IPv6 unique-local `fc00::/7`.
- IPv6 multicast `ff00::/8`.
- IPv4-mapped IPv6 addresses that map to a rejected IPv4 address.

The policy MUST apply to:

- Literal IP hosts.
- Every result returned by DNS.
- Every redirect destination.
- Every reconnect resolution.

If any DNS result is disallowed, the implementation SHOULD reject the destination rather than selecting a different result, preventing DNS-rebinding ambiguity.

Local streams may be enabled only through `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS=y`, default `n`.

## 9. Wi-Fi manager

### 9.1 Default AP configuration

Before NVS override loading:

- AP SSID MUST be `ESP32-S3-Audio`.
- AP password MUST be `password` for the current development baseline.
- Length fields MUST be payload lengths excluding the terminator.

Normal logs and status MUST redact the password.

### 9.2 Exact string loading

A shared helper MUST load NVS strings using the following contract:

1. Destination capacity includes the terminator.
2. Pass destination capacity to `nvs_get_str()`.
3. On success, returned length must be at least 1 and at most capacity.
4. Destination byte at `returned_len - 1` must be NUL.
5. Payload length is `returned_len - 1`.
6. Validate payload length against the field’s protocol maximum.
7. Reject malformed values; do not repair them by truncation.

### 9.3 Validation

- SSID: 1..32 bytes.
- STA passphrase: empty, 8..63 bytes, or exactly 64 hexadecimal characters only when `CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK=y`.
- AP password: empty or 8..63 bytes.
- Validators MUST use `strnlen(max + 1)` or equivalent.
- A 64-character non-hex STA password is invalid and MUST NOT become a 63-character passphrase.

### 9.4 Driver error propagation

The following operations MUST be checked and propagated:

- `esp_wifi_set_mode`
- `esp_wifi_set_config`
- `esp_wifi_start`
- `esp_wifi_stop`
- `esp_wifi_connect`
- `esp_wifi_disconnect`
- event-handler registration/unregistration
- relevant netif creation/destruction
- mDNS initialization, hostname, instance, and service registration

`apply_sta()`, `apply_ap()`, `ensure_ap_config()`, and `apply_action()` MUST return `esp_err_t`.

`s_wifi_started` may become true only after `esp_wifi_start()` succeeds and false only after a confirmed stop/deinit.

`wifi_mgr_init()` may publish RUNNING only after the selected startup action succeeds.

### 9.5 Lifecycle cleanup

Initialization MUST track which resources were created by the current attempt. On failure, unwind them in reverse order. A retry after failure MUST not encounter stale netifs, handlers, or initialized driver state.

Public mutation APIs MUST return `ESP_ERR_INVALID_STATE` before taking `s_mgr_mtx` unless the manager is RUNNING.

### 9.6 Runtime setting transactions

`wifi_mgr_set_ap_enabled()` and `wifi_mgr_set_ap_config()` MUST not publish runtime state when persistence fails. If persistence succeeds but live driver application fails, the component MUST either:

- Roll NVS back to the previous value and preserve old runtime state; or
- Enter an explicit fault/mismatch state with a diagnostic and return failure.

Logging success after an ignored driver failure is forbidden.

### 9.7 Event-handler failures

Failures from `esp_wifi_connect()` invoked in event handlers cannot be returned to the original caller, so they MUST:

- Record a manager last-error.
- Emit a machine-readable diagnostic.
- Transition the state machine to a visible retry/fault path.
- Avoid infinite tight retry loops.

## 10. Radio lifecycle, memory, streaming, and decoding

### 10.1 States

The radio lifecycle MUST include:

- `STOPPED`
- `STARTING`
- `BUFFERING`
- `RUNNING`
- `STOPPING`
- `FAULTED`
- `FAULTED_JOIN_PENDING`

`playing` is true only when a session exists and has not been terminally stopped. `RUNNING` MUST not be published merely because tasks were created.

### 10.2 Session ownership

1. The active session remains published until both workers acknowledge exit and lifecycle code is ready to destroy it.
2. `session_destroy_force()` or equivalent is forbidden.
3. `radio_stop_sync()` on timeout leaves the session attached, state JOIN_PENDING, and resources intact.
4. A later stop/deinit may retry the join.
5. `radio_deinit()` MUST return `esp_err_t` rather than silently forcing teardown.
6. If command-worker exit is not acknowledged, deinit returns timeout and retains queue, mutexes, rings, and task-owned state.

### 10.3 Worker acknowledgements

Use separate event bits for:

- Stream task entered.
- Decoder task entered.
- Stream became operationally ready after valid HTTP response/content handling.
- Decoder became operationally ready after decoder open and valid format information.
- Stream exited.
- Decoder exited.
- Command worker exited.

After task-entry acknowledgement, play may return queued/started status and state BUFFERING. RUNNING is published only after the required operational readiness condition is met. A worker exit before readiness converts startup to failure.

### 10.4 Partial task creation

If the second worker cannot be created:

1. Request stop on the first worker.
2. Wait for first-worker exit acknowledgement.
3. Destroy the session only after acknowledgement.
4. On timeout, retain the session and publish JOIN_PENDING.

### 10.5 Initialization and PSRAM

`radio_init()` MUST allocate into local candidates and publish globals only after every required object exists. Any failure unwinds all local allocations.

Compressed and PCM ring allocation MUST use SPIRAM capability only. No fallback.

Reject zero capacities before ring arithmetic.

### 10.6 Prebuffer

At startup, publish `PREBUF_MS_DEFAULT` before reading NVS.

- Missing key: keep default and return/log normal first-boot status.
- Valid stored value: clamp only if the schema explicitly allows clamp; otherwise reject invalid persisted data and keep default with a visible warning.
- NVS error other than not-found: keep safe runtime default but record a persistence warning/error in status. Do not pretend the load succeeded.

### 10.7 Reconnect backoff

Reconnect delay MUST be stop-aware and must actually wait on every reconnect. Event bits used for startup acknowledgement MUST not short-circuit later backoff.

Recommended schedule:

- 500 ms
- 1 s
- 2 s
- 4 s
- 8 s
- 15 s maximum thereafter

A successful stable connection may reset the backoff. The implementation MUST have a host test proving elapsed delay does not collapse after a prior successful start.

### 10.8 Playlist resolution

Classify input as playlist when indicated by extension, content type, or parsed response structure. For classified playlists:

- Fetch and parse failure is terminal for that attempt.
- Empty playlist is an error.
- Unsupported entries are errors with detail.
- Never feed playlist text to the audio decoder.

A direct URL may pass through only when it is not classified as a playlist. Every resolved URL must pass the URL/SSRF policy.

### 10.9 HTTP handling

- Require successful client allocation.
- Require 2xx response unless a validated redirect is followed.
- Validate content type and/or codec detection.
- Apply URL policy on each redirect and reconnect.
- Publish exact HTTP status and error detail.
- Do not retry permanent unsupported-content errors as if they were transient network failures.

### 10.10 Decoder fault thresholds

The decoder MUST have bounded recovery thresholds. Baseline constants:

- Maximum consecutive decoder-open failures: 3.
- Maximum consecutive decoder no-progress iterations with buffered input: 64.
- Maximum bytes dropped for frame resynchronization per connection: 4096.
- Maximum consecutive resampler no-progress iterations: 8.

Every decoder API return value and consumption count MUST be validated. A decoder must never claim to consume more input than supplied.

When a threshold is exceeded:

- Set the specific `radio_err_t`.
- Store concise error detail.
- Request session stop.
- Exit the worker.
- Transition to FAULTED after join, or JOIN_PENDING if join fails.

Single-byte resynchronization MAY be used within the bounded budget, with a rate-limited diagnostic and a counter. Silent unbounded byte loss is forbidden.

### 10.11 Resampler

- Check initialization result.
- Check process result and progress.
- Publish `RADIO_ERR_RESAMPLER_STALLED` on repeated no-progress.
- Do not mark the resampler ready after failed initialization.

## 11. Control orchestrator and scan

### 11.1 Configuration snapshots

1. `s_cfg` is shared and protected by `s_mtx`.
2. An operation that needs configuration copies a complete immutable snapshot under the mutex.
3. `do_action()` receives the snapshot or only the exact values it needs.
4. The orchestrator MUST NOT read `s_cfg` without synchronization.
5. `ctrl_start()` MUST not write a copied snapshot back to `s_cfg`.
6. Repeated `ctrl_start()` calls MUST be rejected or idempotent; they MUST NOT create multiple orchestrator tasks.

### 11.2 Persist-before-publish

`ctrl_set_sink()` and `ctrl_note_station()` MUST:

1. Copy current config under lock.
2. Build candidate.
3. Release lock if persistence may block, or use a separate update serialization lock/version.
4. Persist candidate.
5. Reacquire lock and publish only if the expected prior version is still current.

A simpler single mutex may remain held during NVS if no callback can re-enter it, but the candidate still MUST be persisted before assigning `s_cfg`.

### 11.3 Resume result

`CTRL_EV_RESUME_DONE` means every required resume step succeeded. It MUST NOT be emitted when:

- Volume command transport failed.
- Volume command returned error/timeout.
- Required station ID was absent.
- Station ID was not found.
- Radio play enqueue failed.

Add a resume-failure event or feed `ok=false` into the state machine. Status must identify the failed phase.

### 11.4 Scan state machine

Each phase must inspect both `esp_err_t` and WROOM command state. Required behavior:

- If radio cannot stop within timeout, abort before Bluetooth inquiry.
- If disconnect fails, abort or explicitly continue only under a documented safe policy.
- If scan command fails, skip the inquiry wait and begin rollback.
- If reconnect fails, report restore failure.
- If volume restore fails, report partial restore failure.
- If radio resume fails or never reaches BUFFERING/RUNNING, report restore failure.
- Final log/diagnostic must be `restored=true/false` and include failed phase.

“scan done, A2DP restored” is legal only after observed restoration.

### 11.5 Task lifecycle

Scan and orchestrator task handles require serialized creation and exit acknowledgement. Task creation failure and duplicate start must be visible.

## 12. Degraded boot and capability safety

### 12.1 Principle

Independent component failure MAY allow the firmware to continue, but only if every dependent API is capability-aware.

### 12.2 Capability map

Boot status MUST be retained in a runtime capability structure accessible to web/status/console code. At minimum:

- I2S available.
- Audio producer available.
- BT link available.
- Radio available.
- Stations available.
- Control available.
- Wi-Fi available.
- Web available.

### 12.3 Dependency rules

- Audio producer requires initialized I2S. It may produce tone without radio.
- Radio endpoints require radio; station-ID operations additionally require stations.
- BT endpoints require BT link and initialized web BT state.
- Wi-Fi mutations require Wi-Fi manager RUNNING.
- Control start requires control, BT link, radio, stations, and Wi-Fi capabilities required by the configured action.
- Unavailable endpoints return HTTP 503 with a stable error code.

### 12.4 Task creation checks

Every `xTaskCreate()` result must be checked, including health probes and clock diagnostics. Failure must update boot/degraded status or emit a visible diagnostic.

### 12.5 NVS initialization

NVS remains a required boot dependency. Automatic NVS erase on `NO_FREE_PAGES` or `NEW_VERSION_FOUND` is existing behavior, but because erase destroys credentials, stations, and auth token, the firmware MUST emit an explicit pre-erase and post-erase diagnostic. A future spec may require user confirmation; FIX3 at minimum makes the destructive recovery visible.

## 13. Frontend behavior

### 13.1 API client

A single API helper MUST:

- Read the token from session storage.
- Add `Authorization: Bearer <token>` to every mutating request.
- Parse structured JSON errors.
- Surface 401 as “token required or invalid.”
- Surface 503 as a named unavailable capability.
- Never silently ignore a failed mutation.

### 13.2 Token entry

The SPA MAY load without a token. Mutating controls must show a token-entry flow when the token is absent or rejected. The token must not be placed in query parameters, URLs, analytics, logs, or DOM text after submission.

Session storage is the default. A persistent “remember” option MAY use local storage only with explicit user choice.

### 13.3 Async operations

Any endpoint that returns an operation ID must display pending, success, and failure states. Polling must not overlap, and failures must not be converted to success UI state.

### 13.4 Reproducible build

`web/package.json` and `web/package-lock.json` MUST be synchronized. A clean checkout must pass:

```bash
cd web
npm ci
npm run build
npm test
```

The embed script output and checksum must be updated only from the successful clean build.

## 14. Diagnostics and status

Every subsystem should expose:

- Lifecycle state.
- Last non-transient error.
- Whether a join is pending.
- Whether persistence is healthy.
- Whether the capability is available.

Required new markers include examples such as:

```text
DIAG|AUTH|READY|source=loaded
DIAG|AUTH|ERROR|stage=commit,err=...
DIAG|I2S|JOIN_PENDING|timeout_ms=...
DIAG|BTLINK|CANCELLED|reason=shutdown
DIAG|STATIONS|CORRUPT|key=stations_v2,reason=crc
DIAG|WIFI|ERROR|op=esp_wifi_start,err=...
DIAG|RADIO|FAULT|gen=...,reason=DECODER_STALLED
DIAG|CTRL|SCAN_DONE|restored=0,failed_phase=reconnect
```

Do not log credentials, bearer tokens except the one-time bootstrap/rotation marker, full Authorization headers, or private key material.

## 15. Verification and acceptance

### 15.1 Host verification

The clean repository must pass:

```bash
./tools/verify_host.sh
```

This means strict, ASan, UBSan, Python gate tests, `npm ci`, frontend build, and frontend unit tests all succeed.

### 15.2 Required new host tests

At minimum:

- Auth token encoding, NVS-length validation, exact comparison, persistence failure, and route dispatch.
- I2S lifecycle reducer/mock tests for start race, start timeout, stop timeout, disable failure, and deinit gating.
- BT link second-task creation failure, active cancellation, send-after-stop, UART short write.
- CRC known-answer and corruption detection.
- Full station blob invariant validation.
- Missing versus corrupt station storage behavior.
- Legacy migration and control index-to-ID conversion.
- URL address-policy tests for all blocked IPv4/IPv6 ranges.
- Wi-Fi exact 32-byte SSID, NVS terminator handling, default AP, driver failure propagation, and API-before-init.
- Radio PSRAM allocation failure with proof that no internal fallback is attempted.
- Radio partial task creation and join timeout.
- Reconnect backoff timing.
- Playlist resolution failure.
- Decoder bounded-failure thresholds.
- Control persist-before-publish, config snapshot, resume failure, and scan rollback.
- Degraded endpoint 503 behavior.

### 15.3 ESP-IDF build

After every phase that changes device code:

```bash
. "$HOME/esp/v5.5.1/esp-idf/export.sh"
cd esp_i2s_source
idf.py fullclean
idf.py build
```

A normal incremental build may be used during development, but phase acceptance requires at least one clean build.

### 15.4 Hardware gate

On the ESP32-S3 and WROOM32 pair:

1. First boot shows AP `ESP32-S3-Audio` with password `password`.
2. First boot prints one valid 64-hex auth token after NVS commit.
3. Unauthenticated POST/PUT/DELETE requests return 401.
4. Authenticated requests work.
5. `/api/bt` never crashes when WROOM32 is absent; it returns 503.
6. Boot without I2S clocks reaches WAITING_FOR_CLOCK and does not hang.
7. Applying WROOM32 clocks transitions to RUNNING without reboot.
8. Removing clocks returns to WAITING_FOR_CLOCK without data corruption or watchdog reset.
9. Radio plays validated MP3 and AAC streams, including playlist inputs.
10. Network interruption shows real reconnect backoff and recovery.
11. Forced decoder failure becomes visible fault rather than endless silent RUNNING.
12. Scan reports truthful restoration result.
13. Station corruption injection is detected and original NVS data is not overwritten.
14. Reboot retains committed settings and does not retain failed mutations.
15. `./tools/s3_device_gate.sh --port /dev/ttyACM0` passes with any new required markers.

### 15.5 Endurance

Run at least a two-hour radio playback test with:

- Periodic Wi-Fi interruption.
- At least one BT disconnect/reconnect.
- I2S clock removal/reapplication if safe for the setup.
- Heap, PSRAM, task count, reconnect count, underrun, and decoder error monitoring.

Acceptance requires no reset, watchdog, use-after-free symptom, monotonic memory leak, tight reconnect loop, or false success state.

## 16. Completion definition

FIX3 is complete only when:

- Every task in the companion TODO is checked off with evidence.
- Every referenced file in this handoff exists at the exact path named.
- `./tools/verify_host.sh` passes from a clean checkout.
- ESP-IDF clean build passes.
- Hardware gate evidence is captured.
- No forbidden fallback in §4 or §16 remains.
- Documentation reflects actual behavior rather than intended behavior.
