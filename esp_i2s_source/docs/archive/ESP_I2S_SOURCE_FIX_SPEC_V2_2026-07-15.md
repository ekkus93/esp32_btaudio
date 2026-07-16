# `esp_i2s_source` Repair and Hardening Specification v2

**Status:** Implementation specification  
**Date:** 2026-07-15  
**Target:** ESP32-S3 source firmware in `esp_i2s_source/`  
**Primary goal:** Produce a reliable ESP32-S3 internet-radio/tone source that sends 44.1 kHz stereo audio over I2S to the ESP32-WROOM32 A2DP bridge and remains controllable through Wi-Fi, console, and the web UI.

---

# 1. Normative language

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are requirements.

- **MUST / MUST NOT:** release-blocking.
- **SHOULD:** expected unless a documented reason and test justify a different implementation.
- **MAY:** optional.

No implementation may replace a required error with a silent fallback.

---

# 2. Scope

This specification covers:

- Boot and subsystem lifecycle
- I2S wire contract and audio production
- Tone/signal generation
- UART command link to the WROOM32
- Wi-Fi STA/AP provisioning and mDNS
- Internet radio download, playlist resolution, ICY demux, decode, resample, buffering, and stop/restart
- Station and orchestration persistence
- USB console
- HTTP/JSON API
- React web UI
- Host, device, gate, and E2E tests
- Logging, diagnostics, security, and error handling

This specification does not require changing the WROOM32 firmware unless device validation proves that its existing I2S/UART contract differs from the contract below.

---

# 3. Product-level behavior

## 3.1 Normal boot

On a normal boot with both boards wired and valid Wi-Fi credentials, the S3 MUST:

1. Initialize NVS.
2. Initialize each singleton subsystem exactly once.
3. Initialize and start the I2S output lifecycle.
4. Start the single audio producer/arbitration task.
5. Initialize the WROOM UART link and perform a nonmutating bounded health probe.
6. Initialize radio buffers and command ownership.
7. Load stations and orchestration configuration.
8. Start Wi-Fi in STA or AP+STA according to stored state.
9. Start console and HTTP control surfaces.
10. Start the boot orchestrator.
11. Emit one machine-readable boot-complete marker.

A successful boot MUST NOT call any subsystem initializer twice.

## 3.2 Degraded boot

The firmware MUST remain controllable whenever reasonably possible.

| Failure | Required behavior |
|---|---|
| WROOM32 absent / no I2S clock | Enter `I2S_WAITING_FOR_CLOCK`; keep Wi-Fi, web, console, stations, and radio controls available. Do not block boot. |
| UART link unavailable | Mark WROOM unreachable; do not run mutating self-tests; keep local tone/radio buffering and control available. Retry health probe with bounded backoff. |
| Wi-Fi credentials absent/invalid | Start provisioning AP. |
| STA connection fails repeatedly | Start AP+STA provisioning fallback and expose the reason. |
| Radio initialization fails | Keep tone, Wi-Fi, console, and diagnostics available; mark radio unavailable. |
| Web server fails | Keep USB console and core audio functioning. |
| NVS load is corrupt | Preserve the corrupt blob for diagnostics, load safe in-RAM defaults, and do not overwrite persistent data automatically. |
| Required internal allocation fails | Return a precise error and enter a degraded/fault state. Do not silently consume another memory class. |

## 3.3 No mutating boot self-test

Boot health checks MUST NOT change volume, station, pairing, connection target, AP settings, or any persisted state.

Allowed UART health commands are read-only commands such as `VERSION` and `STATUS`. Each probe MUST have a bounded timeout and MUST NOT delay the rest of boot by more than 1 second total.

---

# 4. Architecture and ownership

## 4.1 Single-owner rule

Every asynchronous subsystem MUST have one task that owns its mutable lifecycle:

| Subsystem | Owner |
|---|---|
| I2S channel and pending output block | I2S writer task, coordinated by I2S lifecycle mutex |
| Audio source arbitration | `audio_out_task` |
| UART RX/TX protocol session | `bt_link_task` |
| Radio play/stop/session lifecycle | `radio_cmd_task` |
| Wi-Fi state-machine transitions | Wi-Fi event loop plus serialized manager command path |
| Control/orchestrator state | `orchestrator_task` |
| Scan operation | Control owner state; helper task only if joined/acknowledged |
| HTTP server | ESP HTTP task; long operations are queued, not executed synchronously |

Public APIs MUST enqueue a command or take a short configuration lock. They MUST NOT independently mutate a lifecycle owned by another task.

## 4.2 Explicit lifecycle state

Each nontrivial subsystem MUST expose a state enum. At minimum:

```c
typedef enum {
    LIFE_UNINITIALIZED = 0,
    LIFE_INITIALIZING,
    LIFE_IDLE,
    LIFE_STARTING,
    LIFE_RUNNING,
    LIFE_STOPPING,
    LIFE_WAITING,
    LIFE_FAULTED,
} lifecycle_state_t;
```

A component MAY use a more specific enum, but the following rules apply:

- `init()` is idempotent: a second call with the same configuration returns `ESP_OK` and performs no ESP-IDF operation.
- A second call with conflicting configuration returns `ESP_ERR_INVALID_STATE`.
- `start()` is idempotent while running.
- `stop()` is idempotent while stopped.
- Resources are freed only after every task that can reference them has acknowledged exit.
- `FAULTED` does not imply safe reclamation.
- State and task/resource handles are protected by one lifecycle mutex.

## 4.3 No blocking while holding spinlocks

Code MUST NOT call any of the following while inside `taskENTER_CRITICAL`, a port spinlock, or an interrupt-disabled region:

- I2S/UART/Wi-Fi/HTTP/NVS driver calls
- Queue or semaphore waits
- Logging/`printf`
- Heap allocation/free
- Any function with a nonzero timeout

Critical sections are limited to copying/updating small counters or pointers.

## 4.4 Pointer ownership

Every pointer crossing a task, queue, callback, or asynchronous API boundary MUST have an explicit owner and lifetime.

Allowed patterns:

1. Fixed-size value copied into a queue item.
2. Immutable heap object with atomic reference count.
3. Caller-owned object whose use completes synchronously before return.
4. Component-owned object returned only as a copied snapshot.

Forbidden patterns:

- Pointer into a cJSON tree queued after the tree is deleted.
- Stack pointer queued to another task.
- Shared heap object freed by either caller or worker based on an unsynchronized flag.
- Callback retaining pointers into a reusable UART line buffer.

---

# 5. Boot sequence specification

## 5.1 Required order

The intended `app_main()` order is:

```text
NVS
boot diagnostics
bt_link_init
radio_init
stations_init
wifi_mgr_init
i2s_out_init
i2s_out_start
audio_out_start
console_start
ctrl_init
web_ui_start (optional/degraded)
ctrl_start
bounded read-only WROOM probe (may run asynchronously)
BOOT_COMPLETE marker
low-rate diagnostics loop
```

`radio_init` and Wi-Fi may be reordered if the implementation proves a better dependency order, but all singleton initializers MUST still be called once.

## 5.2 Required boot result object

Boot should track each component rather than aborting blindly:

```c
typedef struct {
    esp_err_t bt_link;
    esp_err_t radio;
    esp_err_t stations;
    esp_err_t wifi;
    esp_err_t i2s;
    esp_err_t audio_task;
    esp_err_t console;
    esp_err_t ctrl;
    esp_err_t web;
} boot_result_t;
```

The firmware MUST classify components as required or optional. A required component failure may prevent audio operation, but SHOULD still leave diagnostics available. `ESP_ERROR_CHECK()` may remain only for NVS/heap/platform failures where no controlled mode is possible.

## 5.3 Boot marker

Exactly one marker MUST be emitted per boot generation:

```text
DIAG|BOOT|COMPLETE|required_ok=1,degraded=0,boot_id=<hex>
```

A crash after this marker still fails the device gate.

---

# 6. I2S contract

## 6.1 Hardware roles

- ESP32-WROOM32: I2S master and A2DP bridge.
- ESP32-S3: I2S slave transmitter.
- S3 sends audio data to WROOM32.
- WROOM32 drives BCLK and WS.

## 6.2 Signal pins

The existing project constants are normative unless hardware wiring says otherwise:

- BCLK: `I2S_OUT_GPIO_BCLK`
- WS/LRCLK: `I2S_OUT_GPIO_WS`
- S3 DOUT: `I2S_OUT_GPIO_DOUT`

The final README and wiring diagram MUST resolve constants to physical GPIO numbers.

## 6.3 Audio framing

The wire format MUST be:

- Sample rate: **44,100 frames/second**
- Channels: **2**
- Slot width: **32 bits per channel**
- Frame width: **64 BCLK cycles**
- BCLK target: **2,822,400 Hz**
- WS target: **44,100 Hz**
- WS width: **32 BCLK cycles**
- Format: Philips/I2S, one-bit shift
- Sample payload: signed 16-bit PCM, left-justified in the high 16 bits of each signed 32-bit slot
- Low 16 bits: zero
- Interleaving: left, right

Packing MUST avoid signed-shift undefined behavior:

```c
static inline int32_t pack_s16_msb(int16_t sample)
{
    return (int32_t)sample * INT32_C(65536);
}
```

## 6.4 I2S startup without clocks

I2S initialization MUST be independent of WROOM presence. If the external master clocks are absent:

- `i2s_out_start()` MUST return promptly.
- State becomes `I2S_WAITING_FOR_CLOCK` or the writer records bounded write timeouts.
- The writer MUST retry with backoff no faster than 10 Hz.
- No core watchdog may fire.
- Boot and control surfaces continue.

When clocks appear, the component MUST recover without reboot.

## 6.5 Ring and pending-write semantics

The I2S path MUST NOT discard source bytes merely because a driver write times out or writes partially.

Required behavior:

1. Producer writes complete packed frames into an SPSC ring.
2. Writer peeks or copies a block into a persistent pending buffer.
3. Writer calls the sink outside locks.
4. Writer advances/consumes only the number of bytes actually accepted.
5. Timeout retains pending data and backs off.
6. Fatal sink error transitions to `FAULTED` and records the error.
7. Stop can interrupt retries within 250 ms.

All write lengths MUST be a multiple of 8 bytes (one stereo frame).

## 6.6 I2S statistics

The public snapshot MUST include:

```c
typedef struct {
    uint64_t bytes_accepted;
    uint64_t frames_accepted;
    uint64_t silence_bytes;
    uint64_t underrun_events;
    uint64_t underrun_bytes;
    uint64_t write_timeouts;
    uint64_t write_errors;
    uint64_t partial_writes;
    uint64_t source_drop_bytes;
    size_t ring_used;
    size_t ring_capacity;
    size_t ring_peak;
    lifecycle_state_t state;
    esp_err_t last_error;
} i2s_out_stats_t;
```

Stats are copied atomically/under a short lock. No lock is held across the driver call.

## 6.7 Memory class

The configured 256 KiB I2S ring MUST be allocated from PSRAM on the target. If PSRAM is unavailable or insufficient, initialization MUST return `ESP_ERR_NO_MEM`; it MUST NOT silently allocate the ring from internal RAM.

Small DMA/internal driver buffers remain the responsibility of ESP-IDF.

---

# 7. Audio source arbitration

## 7.1 Single producer

Exactly one `audio_out_task` produces blocks for `i2s_out`.

## 7.2 Priority

Each block uses one coherent source snapshot:

1. If radio state is `RUNNING` and radio owns output:
   - If PCM is ready: use radio PCM.
   - If buffering/rebuffering: output silence.
2. Else if tone is on: use tone.
3. Else: output silence.

A tone MUST NOT unexpectedly play during radio buffering unless the user explicitly enables a future “monitor tone” feature.

## 7.3 Block behavior

- Block size MUST be expressed in frames, not ambiguous bytes.
- The current 256-frame block is acceptable.
- Missing radio frames are zero-filled.
- Gain is applied to signed 16-bit PCM using saturating arithmetic.
- Packing follows Section 6.3.
- The producer MUST have a task handle, start acknowledgment, stop flag, and exit acknowledgment.
- If I2S is faulted, producer MUST wait/back off rather than spin on zero-byte writes.

---

# 8. Tone and signal generation

## 8.1 Input validation

- Frequency MUST be finite.
- Sine/piano frequency range is 0 to 20,000 Hz; values outside are rejected or clamped according to the API contract. The recommended API returns `ESP_ERR_INVALID_ARG`.
- Amplitude is 0.0 to 1.0 or 0 to 100 percent, with one canonical internal representation.
- State/output pointers are validated.

## 8.2 Phase

Phase MUST remain in `[0,1)` using bounded arithmetic, preferably fixed-point or `phase -= floor(phase)` for finite double values.

## 8.3 Coherent configuration

Tone configuration MUST be published as one snapshot:

```c
typedef struct {
    bool on;
    tone_voice_t voice;
    float hz;
    float amplitude;
    uint32_t generation;
} tone_config_t;
```

The audio task reads one complete snapshot per block.

## 8.4 Click suppression

On/off/amplitude changes SHOULD use a 5–20 ms linear ramp. Frequency/voice changes SHOULD start at a zero crossing or crossfade when practical.

---

# 9. Bluetooth UART link

## 9.1 Initialization

- UART1 is installed exactly once.
- `bt_link_init()` is idempotent for the same timeout/configuration.
- The component stores its task handle.
- A `bt_link_stop()`/`bt_link_deinit()` path joins the task and deletes UART/queue/mutex resources.

## 9.2 Command validation

A command MUST:

- Be non-null and nonempty.
- Fit completely in `BT_LINK_LINE_MAX`, including terminator.
- Contain no `\r`, `\n`, NUL inside the declared string, or nonprintable control bytes.
- Use only commands permitted by the calling API. Raw console access is separately authenticated.

Overlong commands are rejected, never truncated.

## 9.3 Request lifetime

Use a two-owner reference-counted request or equivalent safe design.

Normative request model:

```c
typedef struct {
    atomic_uint refs;       /* caller + worker */
    SemaphoreHandle_t done;
    bt_link_cmd_state_t state;
    esp_err_t transport_error;
    char cmd[BT_LINK_LINE_MAX];
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
} bt_link_request_t;
```

Rules:

- Caller creates with `refs = 2` only when the worker has accepted the queue item.
- Caller releases its reference after copying results or timing out.
- Worker releases after completing/canceling the command.
- The last release deletes the semaphore and frees the object.
- Worker never reads the request after signaling and releasing.
- No unsynchronized `abandoned` flag exists.

## 9.4 Result semantics

Return a structured result:

```c
typedef struct {
    esp_err_t transport_error;
    bt_link_cmd_state_t protocol_state;
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
} bt_link_result_t;
```

- Queue/UART/timeout failures appear in `transport_error`.
- Peer `ERR` appears in `protocol_state` and maps to a documented non-OK public return or domain code.
- UART partial/failed writes are reported immediately.

## 9.5 Events

- Subscription table is protected by a mutex or copied immutable snapshot.
- Unsubscribe is supported.
- Events are copied, not pointers into a mutable line buffer.
- User callbacks do not execute on the UART owner task. Dispatch through an event queue/task.

---

# 10. Wi-Fi manager

## 10.1 Lifecycle

`wifi_mgr_init()` MUST:

- Be idempotent.
- Use a lifecycle mutex/state.
- Check every ESP-IDF call.
- Store default netif handles.
- Store event handler instance handles for unregister.
- Return errors rather than abort internally.
- Provide `wifi_mgr_deinit()` for tests/recovery.

## 10.2 Credential representation

Wi-Fi protocol fields are byte arrays, not C strings at their maximum lengths.

- STA SSID: 1–32 bytes.
- Passphrase: open (`0` bytes), WPA passphrase (`8–63` characters), or 64-character hex PSK if supported by the target API.
- AP SSID: 1–32 bytes.
- AP WPA passphrase: 8–63 characters; empty means open only when explicitly allowed.

Exact 32-byte SSIDs MUST not be truncated. The implementation MUST track lengths separately and use `memcpy` into ESP-IDF config structures.

## 10.3 State synchronization

Credential/config/state fields are protected by one manager mutex or command queue. Event handlers MUST reject stale events using state and connection-attempt generation.

## 10.4 Persistence

Updates use candidate → persist → publish/apply semantics:

1. Validate the complete candidate.
2. Save all keys and commit.
3. Apply live configuration.
4. Publish status.
5. If live apply fails, report it and either roll back or mark `persisted_not_applied` explicitly.

Credential erase treats `ESP_ERR_NVS_NOT_FOUND` as already erased and still erases all related keys before commit.

## 10.5 AP policy

Development default may remain:

```text
SSID: ESP32-S3-Audio
Password: password
```

But:

- It MUST be labeled an insecure development default.
- Release configuration MUST require a unique password, forced first-run change, or API authentication.
- Public status MUST NOT include the AP password.
- The UI MUST NOT expect the current password to be returned.

## 10.6 mDNS

mDNS is “up” only after every required setup call succeeds. Failures are exposed in status and retried with bounded backoff.

---

# 11. Radio subsystem

## 11.1 Single lifecycle owner

Only `radio_cmd_task` may create, start, stop, join, publish, or free a radio session. Public sync and async APIs enqueue commands to this owner. If a synchronous API is retained, it sends a command and waits for a separate completion object.

## 11.2 Session ownership

A session contains:

- Generation
- Immutable resolved URL
- Stop token
- Stream and decoder task handles
- Started/exited event bits
- Per-session buffers or references
- State/error snapshot

The session is published in `STARTING` before tasks can run. Each worker acknowledges startup. Transition to `RUNNING` occurs only after both start acknowledgments.

A session is freed only when:

```text
stream_exited == true AND decoder_exited == true AND no public owner references remain
```

A stop timeout marks `FAULTED_JOIN_PENDING`; it does not free memory.

## 11.3 Stop responsiveness

- HTTP reads use bounded timeouts and check stop between reads.
- Reconnect backoff waits are interruptible by a task notification/event.
- All workers acknowledge stop within 2 seconds under normal network failures.
- Release gate fails if stop exceeds 3 seconds.

## 11.4 HTTP and playlist resolution

- Require HTTP status 200–299 after redirects.
- Limit redirects and reject loops.
- Apply body/header size limits.
- Distinguish direct stream from playlist by content type, extension, and bounded sniffing.
- Playlist resolution failure returns a playlist-specific error. It MUST NOT feed playlist text to the codec decoder.
- Support at minimum PLS `File1`, M3U first valid non-comment URL, BOM removal, and relative URL resolution.
- Reject unsupported URL schemes.

## 11.5 URL security

The baseline accepts `http` and `https` only. It rejects:

- Missing host
- Control characters/whitespace
- Embedded CR/LF
- URLs over `RADIO_URL_MAX - 1`
- Userinfo unless explicitly required
- Invalid/overflowing port

Release configuration SHOULD block loopback, link-local, multicast, and private-network destinations unless a setting explicitly allows local streams. This limits unauthenticated/compromised SSRF behavior.

## 11.6 Compressed buffering

Arbitrary compressed bytes MUST NOT be silently dropped. Preferred behavior is backpressure: stop reading from HTTP when the ring is full. If the network/client API makes bounded loss unavoidable, the decoder must enter a codec-aware resynchronization state and status must count a discontinuity.

## 11.7 Decoder contract

- Decoder open failure is fatal for the session; do not retry forever while discarding input.
- Check every decoder API return.
- Validate `0 <= consumed <= input_len`.
- `consumed == 0` with “need more input” preserves bytes and fetches more.
- Never force arbitrary one-byte consumption.
- Validate decoded sample rate, channels, bits, and output length.
- Support mono by explicit duplication to stereo.
- Reject unsupported multichannel data with a precise error.

## 11.8 Resampler

The output rate is fixed at 44.1 kHz stereo signed 16-bit.

The resampler MUST:

- Reject invalid input rates/channels.
- Preserve phase across calls.
- Produce chunk-boundary-equivalent output.
- Use 64-bit fixed-point or double source position with bounded drift.
- Saturate interpolation output to int16.
- Reset explicitly on stream format change/discontinuity.

Reference linear interpolation:

```text
position p is in input-frame units
left = floor(p)
frac = p - left
out = input[left] * (1-frac) + input[left+1] * frac
p += input_rate / 44100
```

Input frames retained from one call must supply the left/right pair for the next call.

## 11.9 PCM ring and prebuffer

- PCM ring is PSRAM-required.
- PCM writes use backpressure/interruptible waits rather than silent drop.
- Prebuffer threshold is computed from exact format:

```c
bytes = ((uint64_t)ms * 44100u * 2u * sizeof(int16_t) + 999u) / 1000u;
```

- `s_prebuffered` is protected by the PCM lock or atomic state.
- Rebuffering outputs silence.
- Prebuffer NVS update is transactional and never closes an invalid handle.

## 11.10 Radio status

Expose one coherent snapshot containing:

- State and active generation
- URL/station/title
- Codec and input format
- Output format
- HTTP status
- Network/read/reconnect counters
- Compressed and PCM ring occupancy
- Buffering/prebuffer state
- Decode/resampler errors
- Last error code/detail
- Worker started/exited flags

The snapshot should be published by the owner task or copied under one short lock, not assembled under three nested locks.

---

# 12. Stations and control configuration

## 12.1 Stable station identity

Every station MUST have a persistent stable 32-bit ID independent of array order:

```c
typedef struct {
    uint32_t id;
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
} station_t;
```

`last_station` becomes `last_station_id`.

## 12.2 Store errors

CRUD must distinguish:

- Invalid argument
- Invalid URL
- Too long
- Duplicate
- Store full
- Not found
- Persistence failure

Do not map these all to `ESP_ERR_NO_MEM`.

## 12.3 Persistence schema

Persistent blobs MUST contain:

- Magic
- Schema version
- Payload length
- Generation/revision
- CRC32 or checksum
- Explicit count
- Fully initialized entries

Load validates every string terminator, URL, ID uniqueness, count, length, and checksum.

Corrupt data is not automatically overwritten. Save it under a quarantine key or leave it untouched, expose an error, and use safe in-RAM defaults.

## 12.4 Transactional mutations

For stations and control config:

1. Copy live object to candidate.
2. Validate candidate.
3. Persist candidate.
4. Publish candidate only on success.

## 12.5 Control orchestrator

- `ctrl_start()` is idempotent.
- The task uses a local immutable config snapshot.
- Config update sends a reconfigure event.
- Timers use monotonic wall time, not assumed loop increments.
- Bluetooth status verifies expected MAC and stream state.
- Resume success requires confirmed volume and accepted radio play.
- Scan is one serialized operation with explicit states and event-driven completion.
- Logs accurately report partial/failure outcomes.

---

# 13. Console

- Start/stop are idempotent.
- The component has one task handle and exit acknowledgment.
- Overlong input is discarded to newline and returns `ERR LINE_TOO_LONG`.
- Quoted/escaped arguments support spaces, or the console accepts JSON after a command verb.
- Embedded CR/LF in values is rejected.
- Driver read/write failures are surfaced with rate-limited diagnostics.
- Console/UART peripheral ownership is documented and nonconflicting.
- Sensitive values are redacted.

---

# 14. HTTP API

## 14.1 General response format

Every JSON response uses:

```json
{
  "ok": false,
  "error": {
    "code": "STATION_DUPLICATE",
    "message": "A station with this URL already exists",
    "retryable": false
  }
}
```

Successful response:

```json
{
  "ok": true,
  "data": {}
}
```

## 14.2 HTTP status rules

| Condition | Status |
|---|---:|
| Success | 200 or 201 |
| Accepted asynchronous operation | 202 |
| Invalid/missing JSON or field | 400 |
| Unauthorized | 401 |
| Forbidden | 403 |
| Not found | 404 |
| Conflict/already active/duplicate | 409 |
| Body too large | 413 |
| Unsupported media type | 415 |
| Rate limited/busy | 429 |
| Internal allocation/driver failure | 500 |
| Dependency unavailable | 503 |
| Dependency timeout | 504 |

Unknown `/api/*` paths return JSON 404. SPA fallback applies only to non-API GET paths.

## 14.3 Request parsing

- Enforce `Content-Type: application/json` on JSON bodies.
- Reject bodies larger than endpoint limit before parsing.
- Read exactly the declared body or reject timeout/truncation.
- Check every cJSON allocation and type.
- Copy strings before deleting cJSON.
- Reject overlong strings; never silently truncate.
- Use strict numeric parsing/range checks.

## 14.4 Long operations

Bluetooth scan/connect, Wi-Fi provisioning, and radio start/stop SHOULD return `202` with an operation ID:

```json
{
  "ok": true,
  "data": { "operation_id": 42, "state": "QUEUED" }
}
```

Clients poll operation/status endpoints. HTTP task MUST NOT block for multi-second UART/network operations.

## 14.5 Authentication

Release builds MUST require authentication for all mutating endpoints and raw console access.

Minimum acceptable baseline:

- Random per-device token generated on first boot and stored in NVS.
- Token supplied in `Authorization: Bearer ...`.
- Constant-time comparison.
- Token never returned by generic status.
- Provisioning bootstrap has a documented physical-presence or first-run flow.

Development builds MAY disable auth only through an explicit build option and MUST print a warning marker.

## 14.6 Endpoint-specific rules

- `/api/status`: nonblocking cached snapshot; no synchronous UART round-trip.
- `/api/radio`: copies and validates URL before queueing.
- `/api/stations`: stable IDs and precise errors.
- `/api/wifi`: one provisioning operation at a time.
- `/api/apmode`: never returns current password.
- `/api/console`: authenticated, command validated, request-isolated capture.
- `/api/tone`: malformed body leaves current tone unchanged.

---

# 15. Web frontend

## 15.1 Central fetch helper

Every API call MUST use one helper that:

- Checks `response.ok`.
- Parses JSON only when content type is JSON.
- Converts structured backend errors into a typed exception.
- Includes endpoint and status.
- Supports `AbortSignal`.
- Applies a timeout.

## 15.2 Polling

Polling MUST:

- Allow at most one in-flight request per poller.
- Abort/ignore stale generations.
- Stop on unmount.
- Back off after repeated failures.
- Display stale/offline state rather than silently swallowing errors.

## 15.3 Operation workflows

For Wi-Fi changes that may disconnect the browser:

1. Backend confirms persistence/operation acceptance before applying disconnecting change when possible.
2. Frontend distinguishes an acknowledged operation followed by disconnect from a pre-ack failure.
3. Frontend displays reconnect instructions and polls the expected host.

Bluetooth and radio operations display queued/running/succeeded/failed states.

## 15.4 Secrets

The UI does not receive or prefill the current AP password. Password fields start empty and mean “leave unchanged” or “set new,” with explicit semantics.

---

# 16. Diagnostics and logging

## 16.1 Structured markers

At minimum:

```text
DIAG|BOOT|COMPLETE|...
DIAG|I2S|STATE|...
DIAG|I2S|CLOCK|...
DIAG|BTLINK|STATE|...
DIAG|WIFI|STATE|...
DIAG|RADIO|STATE|...
DIAG|ERROR|component=<name>,code=<code>,detail=<redacted>
```

## 16.2 Rate limits

- Normal health telemetry: no more than once every 5 seconds by default.
- PCNT clock measurement: disabled after stable detection or no more than once every 10 seconds.
- Repeated identical errors: rate-limited.

## 16.3 Redaction

Never log:

- Wi-Fi passphrase
- AP passphrase
- Auth token
- Authorization header
- Full sensitive URL query strings

SSID, station name, and host may be logged at development level.

---

# 17. Test specification

## 17.1 Deterministic host entry point

`tools/run_host_tests.sh` MUST work without internet on a clean supported host. Unity is vendored or supplied through an explicit local path. It MUST support:

```text
--strict     -Wall -Wextra -Werror
--asan
--ubsan
--coverage
```

A separate concurrency suite SHOULD use pthreads and ThreadSanitizer where available.

## 17.2 Required host tests

### Boot

- Each initializer called exactly once.
- Correct ordering.
- I2S/audio producer started.
- Optional failure does not abort required diagnostics.
- Read-only self-test has no `VOLUME`, `CONNECT`, `START`, or persistence call.

### I2S

- Sink success consumes exactly accepted bytes.
- Timeout consumes zero bytes.
- Partial write consumes only accepted bytes and retries remainder.
- No lock held when sink callback executes.
- Stop interrupts timeout/retry.
- lifecycle idempotence and stale event bits.
- PSRAM-required allocation failure does not fall back.

### BT link

- Exact command boundary accepted; over boundary rejected.
- CR/LF rejected.
- UART partial write reported.
- Caller timeout before worker completion.
- Worker completion before caller timeout.
- Forced preemption immediately after signal.
- Refcount frees exactly once.
- Callback may not deadlock owner; event dispatch tested.

### Wi-Fi

- Duplicate init is harmless.
- 32-byte SSID preserved exactly.
- 64-hex PSK behavior explicit.
- Missing-key erase succeeds.
- stale GOT_IP ignored after fallback generation.
- every injected ESP-IDF failure yields expected state/error.

### Radio

- Session publication/start barrier.
- Every task-creation failure path joins safely.
- Stop timeout retains session and never frees live task data.
- HTTP non-2xx rejected.
- playlist failure does not invoke decoder.
- compressed full behavior has no arbitrary silent drop.
- decoder consumed bounds.
- stop interrupts read/backoff.

### Resampler

- Ramp reference values.
- Impulse response.
- 48k→44.1k and 32k→44.1k sine frequency.
- Mono→stereo.
- Arbitrary chunk splits equal one-shot output.
- Long-run frame-count/drift bound.
- Invalid rates/channels rejected.

### Persistence/stations/control

- Corrupt blob quarantined/not overwritten.
- Transaction rollback on save failure.
- Stable station ID survives reorder/removal.
- precise error mapping.
- control config update while task runs.
- monotonic timing tests.

### Web backend/frontend

- JSON lifetime poisoning test for every async string.
- non-JSON and truncated body handling.
- status-code contract.
- auth required in release mode.
- no secret in status.
- API helper non-2xx/malformed JSON/timeouts.
- overlapping poll prevention and cancellation.

## 17.3 Device tests

Device tests MUST use Unity accounting and deterministic prerequisites.

Required:

- NVS write/read/delete always executes.
- PSRAM requirement conditioned on target configuration.
- Wi-Fi test creates netif, applies credentials, waits on IP event, and fails on timeout when credentials are provided.
- Signal test checks RMS/nonzero samples, not first sample.
- Task test uses a semaphore/event for completion.
- I2S state and clock detection.
- UART link read-only command.
- Tone end-to-end output marker.

## 17.4 Device gate

Default release gate requires all of:

- Last boot completed.
- No crash after boot marker.
- Wi-Fi reaches intended state.
- BT link reaches reachable state when companion-required mode is enabled.
- I2S state valid and byte count increasing when clocks present.
- BCLK/WS ratio in tolerance.
- Radio play/decode/PCM/audio-ready markers.
- Stop completes.
- No heap-corruption/watchdog/stack-overflow/reset loop.

Gate assertions are scoped to the last boot ID.

## 17.5 Soak tests

- 100 boot cycles.
- 500 radio play/stop cycles.
- 100 Bluetooth timeout/reconnect cycles.
- Two-hour MP3 stream.
- Two-hour AAC stream.
- Wi-Fi disconnect/reconnect during playback.
- WROOM power removed/restored while S3 remains on.
- AP/STA mode changes.
- Repeated web polling and operations from two clients.

Heap high-water and task counts must stabilize; no monotonic leak is allowed.

---

# 18. Build and quality gates

Release requires:

```text
Host strict compile: PASS
Host ASan: PASS
Host UBSan: PASS
Concurrency/TSan where supported: PASS
Python gate tests: PASS
Web TypeScript build: PASS
Frontend unit tests: PASS
Playwright mock-device suite: PASS
ESP-IDF build: PASS with no project warnings
Device Unity tests: PASS
Hardware gate: PASS
Soak suite: PASS
```

Warnings are errors for project source and test mocks. Third-party warnings may be isolated explicitly.

---

# 19. Definition of done

The repair is complete only when:

1. The supplied duplicate-init crash is impossible by design and regression-tested.
2. I2S is actually initialized, started, and fed.
3. Tone is audible end to end through the WROOM32/A2DP sink.
4. Internet radio is audible and stable for MP3 and AAC.
5. No known use-after-free, live-session free, blocking critical section, or data race remains.
6. Resampler output matches a trusted reference and is chunk-equivalent.
7. Every current silent failure is either removed or converted into an explicit, observable, tested degraded mode.
8. The device can boot and remain controllable with the WROOM absent.
9. The release HTTP API is authenticated and does not expose secrets.
10. All gates in Section 18 pass from documented one-command entry points.

