# ESP32 Bluetooth Audio Reliability Hardening Specification v1.0

**Repository baseline:** `esp32_btaudio-master_2607131425.zip`  
**Primary projects:** `esp_i2s_source/` and `esp_bt_audio_source/`  
**Excluded:** `archive/` and unrelated historical artifacts  
**Target ESP-IDF:** v5.5.1  
**Intended implementer:** Claude Code using a local Qwen3.6 27B model  
**Status:** Authoritative implementation specification for the next hardening pass

## 1. Purpose

The current firmware has a working end-to-end audio path and a generally sound architecture. This hardening release must preserve that successful behavior while correcting ownership, concurrency, lifecycle, error-reporting, cleanup, and persistence defects that can cause rare crashes, stale state, duplicated tasks, corrupted audio, or false-success responses.

This is **not** a feature rewrite. It is a reliability pass over the existing implementation.

The release is complete only when:

1. No asynchronous task or queue retains a pointer to caller stack memory.
2. A component never reports `ESP_OK` after its required worker failed to start or stop.
3. A new radio or audio generation cannot start while an old generation is still alive.
4. Shared cross-task state has one documented synchronization mechanism.
5. Low-rate streams do not silently lose decoded samples.
6. I2S stop/restart works when the external clock disappears.
7. NVS and task-creation failures are visible to callers and the web UI.
8. Host and device tests cover the corrected failure paths.

## 2. Normative language

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

- **MUST/MUST NOT:** required for acceptance.
- **SHOULD:** expected unless a documented technical reason is recorded.
- **MAY:** optional.

A logged warning is not a substitute for returning an error when the requested operation failed.

## 3. Locked behavior and non-goals

### 3.1 Locked hardware/audio contract

The following behavior is already proven and MUST remain unchanged unless a separate hardware investigation explicitly authorizes a change:

- ESP32-WROOM32 is the I2S **master receiver**.
- ESP32-S3 is the I2S **slave transmitter**.
- Sample rate is 44.1 kHz.
- Stereo PCM.
- 32-bit I2S slots with signed 16-bit audio in the most-significant 16 bits.
- Philips framing, `ws_width = 32`, `ws_pol = false`, `bit_shift = true`.
- S3 pins: BCLK GPIO15, WS GPIO16, DOUT GPIO7.
- WROOM32 pins: BCLK GPIO18, WS GPIO19, DIN GPIO22.
- UART command link remains 115200 8N1 using the current pins.
- Radio, tone, and silence continue to be selected by one S3 audio feeder.
- The existing deep decoded-PCM jitter buffer and prebuffer/rebuffer behavior remain.

### 3.2 Non-goals

This release MUST NOT:

- Replace ESP-IDF, FreeRTOS, the decoder library, React UI, or the UART protocol.
- Change the successful I2S wiring or slot format.
- Add ESP-ADF.
- Redesign station UX.
- Add unrelated features.
- Perform broad naming/style refactors while fixing behavior.
- Touch `archive/`.
- silently reduce buffer sizes, timeouts, decoder support, or diagnostics to make tests pass.

## 4. Engineering rules

### 4.1 Ownership

Every asynchronous object MUST have a single explicit owner.

- A pointer placed in a queue MUST remain valid until the consumer acknowledges release.
- Stack objects MUST NOT be queued by pointer.
- A task handle MUST remain valid until the task has confirmed exit.
- A component MUST NOT clear or overwrite a live task handle.
- A resource allocated during initialization MUST be released on every later failure path.

### 4.2 Lifecycle states

Components with workers MUST use an explicit state, not only a shared boolean.

Recommended states:

```c
typedef enum {
    COMPONENT_STATE_UNINITIALIZED = 0,
    COMPONENT_STATE_STOPPED,
    COMPONENT_STATE_STARTING,
    COMPONENT_STATE_RUNNING,
    COMPONENT_STATE_STOPPING,
    COMPONENT_STATE_FAULTED,
} component_state_t;
```

A component in `STOPPING` or `FAULTED` MUST reject a new start until the old worker is proven gone or the component is explicitly recovered.

### 4.3 Error semantics

- `ESP_OK` means the requested operation completed or was successfully accepted by a durable owner that can report its eventual result.
- Failure to create a required task MUST return `ESP_ERR_NO_MEM` or another specific error.
- Failure to stop a required task by the deadline MUST return `ESP_ERR_TIMEOUT`.
- Runtime-only application of a setting with failed persistence MUST not be represented as fully successful.
- Web JSON MUST distinguish accepted, applied, persisted, and failed outcomes where relevant.

### 4.4 Synchronization

- `volatile` MUST NOT be used as a replacement for mutual exclusion or C memory ordering.
- Multi-field state snapshots MUST be copied under one lock or via a validated sequence-counter design.
- 64-bit counters accessed from multiple tasks on 32-bit ESP32 targets MUST be protected or atomic.
- External callbacks MUST NOT be invoked while holding an internal state mutex.

### 4.5 No dangerous fallback behavior

The following are prohibited:

- Forgetting a task that missed its shutdown deadline.
- Starting a second worker over shared buffers because the first worker “probably stopped.”
- Returning success after a task-creation failure.
- Dropping decoded input merely because an output buffer filled.
- Claiming persistence when `nvs_commit()` failed.
- Reporting a subsystem ready when route registration or required startup failed.

## 5. Required S3 changes (`esp_i2s_source`)

## 5.1 UART Bluetooth link request ownership

### Problem

`bt_link_send()` queues a pointer to a stack-local request and waits on a module-global completion semaphore. If the caller times out, the link task can later write into dead stack memory. A late completion can also be consumed by a later command.

### Required design

`bt_link` MUST use per-request storage and per-request completion.

Each request MUST:

- Be heap- or pool-allocated.
- Own its completion semaphore or notification.
- Have an explicit ownership state.
- Remain valid if the calling task abandons the wait.
- Be freed exactly once.

The configured timeout supplied to `bt_link_init()` MUST be stored and used. There MUST NOT be a separate hard-coded `BT_LINK_DEFAULT_TIMEOUT_MS` wait in `bt_link_send()`.

The send mutex MAY remain to serialize commands, but completion MUST not use a shared semaphore.

### Required request states

```c
typedef enum {
    BT_REQ_QUEUED = 0,
    BT_REQ_ACTIVE,
    BT_REQ_COMPLETED,
    BT_REQ_ABANDONED,
} bt_link_request_lifetime_t;
```

The request state transition and free decision MUST be protected by a mutex or critical section.

### Behavior

- If the caller receives completion, it copies the result and frees the request.
- If the caller’s outer wait expires, it marks the request abandoned and returns `ESP_ERR_TIMEOUT` without freeing memory still visible to the worker.
- If the worker later sees an abandoned request, it frees it instead of signalling a destroyed synchronization object.
- An abandoned queued request SHOULD be discarded before transmission.
- One request’s completion MUST never satisfy another request.

### Initialization cleanup

`bt_link_init()` MUST unwind:

- UART driver installation.
- Queue creation.
- semaphore/mutex creation.
- task creation.

A failed initialization MUST leave the component retryable.

## 5.2 Radio session ownership and generation isolation

### Problem

The current global `s_playing` boolean can be set false by stop and true by a new play while old stream/decoder tasks remain alive. Old and new tasks can then operate on the same global rings and static scratch buffers.

### Required design

Radio playback MUST be represented by an explicit session object with a monotonically increasing generation ID.

```c
typedef struct radio_session {
    uint32_t generation;
    char url[RADIO_URL_MAX];
    TaskHandle_t stream_task;
    TaskHandle_t decoder_task;
    EventGroupHandle_t events;
    volatile bool stop_requested;
} radio_session_t;
```

A single radio-control mutex MUST protect the active session pointer and lifecycle state.

Required event bits:

```c
#define RADIO_EVT_STREAM_EXITED  BIT0
#define RADIO_EVT_DECODER_EXITED BIT1
#define RADIO_EVT_STARTED        BIT2
#define RADIO_EVT_FAILED         BIT3
```

### Start contract

`radio_play()` MUST:

1. Validate the URL.
2. Stop and fully join any previous session.
3. Refuse to start if the previous session did not exit.
4. Resolve the URL without dereferencing a null HTTP client handle.
5. Allocate and initialize a new session.
6. Reset shared rings only after the old session is gone.
7. Create both required tasks.
8. If either task creation fails, request stop, join any created task, release the session, and return failure.
9. Publish the new session as running only after startup succeeds.

### Stop contract

`radio_stop()` SHOULD return `esp_err_t` rather than `void`.

It MUST:

- Mark only the current session for stop.
- Wait for both exit bits.
- Return `ESP_ERR_TIMEOUT` if both workers do not exit by the deadline.
- Keep the session handle/state intact on timeout.
- Never permit a new session while the prior one is still alive.
- Free the session only after both workers have exited.

HTTP reads MUST use a finite timeout so stop can make progress even when a server stalls.

### Scratch buffers

Decoder and stream scratch storage MUST NOT be shared by overlapping generations. Either:

- allocate buffers per session/task, or
- enforce and test the invariant that generations never overlap.

Per-task allocation is preferred.

## 5.3 Radio decoder correctness

### 5.3.1 Consume all resampler input

The decoder MUST honor `in_used` returned by `radio_resampler_run()`.

For each decoded PCM block, it MUST loop until all input frames are consumed or a clear fatal/stall condition occurs.

This is required for 22.05 kHz and 32 kHz input, where output expansion can fill the resampler output buffer before all input is consumed.

### 5.3.2 Do not drop partial PCM-ring writes

`pcm_write()` can return less than requested. The decoder MUST either:

- retry until all produced PCM is queued, with bounded waits and stop checks, or
- return an explicit overflow/fault state.

It MUST NOT silently discard the unwritten tail.

### 5.3.3 Preserve compressed input tails

If the decoder consumes zero bytes because it needs more input, the unconsumed bytes MUST be retained and new bytes appended. The next outer loop MUST NOT overwrite the tail.

A bounded accumulation buffer MUST be used. If the buffer fills without decoder progress, record a decode/resync error and discard the minimum amount needed to make progress.

### 5.3.4 Unsupported codec behavior

An unknown or unsupported codec MUST transition the radio session to a visible error state. The system MUST NOT continue downloading indefinitely while no decoder drains the compressed ring.

The status response SHOULD include a reason such as:

- `unsupported_content_type`
- `decoder_open_failed`
- `network_open_failed`
- `decoder_stalled`
- `stop_timeout`

## 5.4 Radio and I2S telemetry synchronization

All radio telemetry written by stream and decoder tasks and read by HTTP handlers MUST be synchronized.

A lock-protected snapshot MUST include all related fields:

- playing/lifecycle state.
- buffering state.
- codec.
- HTTP status.
- bitrate.
- URL/station/title.
- bytes received.
- compressed-ring occupancy.
- reconnects.
- overflow drops.
- decoder rate/channels/errors.
- PCM-ring occupancy.
- last error reason.
- generation.

Independent hot counters MAY use atomics, but the status API must return a coherent snapshot.

`i2s_out_stats_t`, including 64-bit counters, MUST be copied under a critical section, mutex, or sequence counter.

## 5.5 PCM ring memory ordering

The S3 `pcm_ring` MUST use C11 atomics or a lock. Aligned `volatile size_t` fields are not sufficient for inter-core producer/consumer ordering.

Preferred implementation:

- `_Atomic size_t head`
- `_Atomic size_t tail`
- `_Atomic size_t peak`
- producer acquire-loads `tail` and release-stores `head` after copying payload.
- consumer acquire-loads `head` and release-stores `tail` after copying payload.

`pcm_ring_reset()` remains legal only while the producer is quiesced and MUST use atomic operations.

The host build MUST use C11.

## 5.6 I2S writer lifecycle

### Problem

The writer uses `i2s_channel_write(..., portMAX_DELAY)`. If the external WROOM32 clock stops, the task can block indefinitely. Current stop logic may return while the task remains alive.

### Required behavior

- I2S writes MUST use a finite timeout.
- The writer MUST check the stop state after timeout or write error.
- The writer MUST signal started and exited events.
- `i2s_out_start()` MUST fail if a writer handle already exists, even if a boolean says stopped.
- `i2s_out_stop()` MUST wait for the writer exit acknowledgement.
- On stop timeout, it MUST return `ESP_ERR_TIMEOUT`, retain the task handle, and enter `FAULTED` or `STOPPING`.
- A new writer MUST NOT be created until the old writer is gone.
- The channel MUST be disabled only after the writer exits, unless a separately verified ESP-IDF-safe unblock sequence is implemented.

The writer MUST record non-timeout I2S errors and avoid a tight error loop.

## 5.7 Defined 16-to-32-bit sample packing

This expression is prohibited:

```c
(int32_t)sample << 16
```

because left-shifting a negative signed integer is undefined behavior.

Use defined arithmetic:

```c
block32[i] = (int32_t)block[i] * INT32_C(65536);
```

or an explicitly verified unsigned bit-pattern conversion.

The resulting wire format MUST remain identical.

## 5.8 Web radio command serialization

The web server MUST NOT create an independent play/stop task per request using one shared static URL buffer.

Required design:

- One radio command queue.
- One radio-control worker owns calls to `radio_play()` and `radio_stop()`.
- Each queued play command contains its own URL copy and optional station ID.
- Queue creation and worker startup occur before the web server accepts requests.
- Queue-full or task-start failures are returned to HTTP clients.
- Commands are processed in order.
- A later command MAY supersede an earlier queued play command only if the behavior is explicit and tested.

The HTTP response MUST not say `{"ok":true}` before the command is safely queued.

## 5.9 Controller/web initialization order

The controller state and mutex MUST be initialized before route handlers can call `ctrl_get_cfg()` or related functions.

Preferred order:

1. NVS.
2. stateful component initialization.
3. controller initialization.
4. control worker startup.
5. web server startup and route registration.

If controller initialization and orchestration are currently combined, split them into `ctrl_init()` and `ctrl_start()` or provide an equivalent invariant.

## 5.10 HTTP server route registration

Every `httpd_register_uri_handler()` result MUST be checked.

If any required route fails to register:

- stop the server.
- clear the server handle.
- return the registration error.
- do not print/return ready status.

A small registration helper SHOULD remove repetitive code.

## 5.11 HTTP client null checks

Every result from `esp_http_client_init()` MUST be checked before any other client API is called.

Playlist resolution SHOULD return an `esp_err_t` plus an output URL rather than silently falling through on allocation failure.

Best-effort fallback to the original URL MAY remain for parse or playlist-fetch failure, but allocation failure and invalid state MUST be logged and propagated according to the caller contract.

## 5.12 Persistence result propagation

The following APIs SHOULD return `esp_err_t` or a result structure instead of `void`/boolean-only success:

- `i2s_out_set_gain()`
- `radio_set_prebuffer_ms()`
- station add/update/remove/move wrappers that persist.
- `ctrl_note_station()` or its persistence operation.
- Wi-Fi reset/credential persistence operations.

Required semantics:

- If runtime state changed but persistence failed, return an error and expose `applied=true, persisted=false` where appropriate.
- Check `nvs_open`, `nvs_set_*`, `nvs_erase_*`, and `nvs_commit` independently.
- Log the exact failing operation and `esp_err_to_name(err)`.
- Do not roll back runtime audio gain solely because persistence failed unless the API explicitly promises transactional behavior.

## 5.13 Initialization cleanup

`radio_init()`, `bt_link_init()`, `i2s_out_init()`, web-control initialization, and other multi-step functions MUST use one cleanup path that releases resources in reverse acquisition order.

After any failed init:

- global handles/pointers MUST be null.
- state MUST be `UNINITIALIZED` or `STOPPED` as appropriate.
- a retry MUST be possible.

## 6. Required WROOM32 changes (`esp_bt_audio_source`)

## 6.1 Bluetooth context synchronization model

### Locked decision

The current claimed owner-task model is not implemented consistently: `BtAppTask` is not started in production, and A2DP callbacks directly modify `bt_ctx`.

For this hardening release, use a **mutex-protected `bt_ctx` model**. Do not attempt a partial owner-task conversion.

### Required implementation

Add a platform-neutral mutex abstraction to `platform_shim`:

```c
typedef struct platform_mutex_s *platform_mutex_t;

platform_mutex_t platform_mutex_create(void);
void platform_mutex_delete(platform_mutex_t mutex);
esp_err_t platform_mutex_lock(platform_mutex_t mutex, uint32_t timeout_ms);
esp_err_t platform_mutex_unlock(platform_mutex_t mutex);
```

Use FreeRTOS mutexes on device and pthread mutexes on host.

Add internal BT context helpers:

```c
esp_err_t bt_ctx_lock(uint32_t timeout_ms);
void bt_ctx_unlock(void);
esp_err_t bt_ctx_get_status(bt_mgr_status_response_t *out);
```

All production reads and writes of `bt_ctx` fields MUST be audited and protected.

Rules:

- Update related fields under one lock.
- Copy callback pointers and callback arguments while locked, then unlock before invoking callbacks.
- Do not hold the lock while calling ESP-IDF Bluetooth APIs, NVS, audio functions, logging-heavy operations, or user callbacks.
- Device-list snapshot APIs MUST copy under the same lock.
- `bt_manager_get_status()` MUST directly take a locked snapshot; it MUST no longer queue pointers to stack request objects.

The obsolete manager request/response path (`BT_APP_SIG_MGR_REQUEST`, stack request + semaphore) SHOULD be removed after tests are updated. `BtAppTask` may remain only for independently used work-dispatch functionality.

The comments in `bt_manager_internal.h`, `BT_STATE_ACCESS_CONTRACT.md`, and related files MUST be corrected to match reality.

## 6.2 Audio processor lifecycle

The audio processor MUST use explicit lifecycle state and task acknowledgement.

### Startup

`audio_processor_start()` MUST:

1. Reject start unless initialized and `STOPPED`.
2. Clear startup/exit event bits.
3. Set state to `STARTING`.
4. Create the engine task.
5. Wait for `ENGINE_RUNNING_BIT` or `ENGINE_STOPPED_BIT`.
6. Return the engine’s startup error if it exits before running.
7. Return `ESP_ERR_TIMEOUT` if no acknowledgement arrives.
8. Set `s_is_running`/state to running only after `ENGINE_RUNNING_BIT`.

The engine task MUST store a startup error before signalling stopped when allocation or initialization fails.

A timeout or early worker death MUST NOT be logged as non-fatal success.

### Shutdown

`audio_processor_stop()` MUST:

1. Set state to `STOPPING` and request cooperative stop.
2. Wait for `ENGINE_STOPPED_BIT`.
3. On success, confirm the task handle is null, then stop/deinit dependent runtime resources as designed.
4. On timeout, return `ESP_ERR_TIMEOUT`, retain the task handle, do not stop I2S underneath a live engine, and set `FAULTED` or remain `STOPPING`.
5. Never clear a live task handle.
6. Reject restart while a live old engine exists.

### State reporting

Status/diagnostics MUST expose `starting`, `running`, `stopping`, `stopped`, and `faulted`, or an equivalent enum.

## 6.3 Audio initialization rollback

`audio_processor_init()` MUST use a single reverse-order cleanup path.

Resources include, at minimum:

- three work buffers.
- I2S manager.
- beep manager.
- NVS debounce timer.
- audio ring buffer.
- event group.
- any later-created source state.

Every failure after work-buffer allocation MUST free those buffers.

A helper such as `audio_processor_cleanup_partial_init()` SHOULD be used by both failure paths and deinit.

## 6.4 Bluetooth initialization rollback

`bt_manager_init()` MUST track completed initialization stages and unwind them in reverse order when a later stage fails.

Stages include, as applicable:

- context mutex.
- controller initialization.
- controller enable.
- Bluedroid initialization.
- Bluedroid enable.
- GAP callback/profile registration.
- A2DP callback/profile initialization.
- AVRCP callback/profile initialization.
- pairing/device-list state.

A failed initialization MUST leave retryable state and `bt_ctx.initialized == false`.

## 6.5 Bluetooth status correctness

`bt_manager_is_connected()` and other status helpers MUST return the synchronized canonical state. They MUST NOT silently return “not connected” merely because an internal request queue was never started.

If a snapshot lock cannot be obtained within its defined deadline, return an explicit error from APIs that support errors. Boolean convenience APIs MAY conservatively return false but MUST emit rate-limited diagnostics; command/status paths must preserve the error.

## 6.6 Task-creation and command responses

Where command handling creates a task or schedules deferred work:

- Check the task/queue result before returning success.
- Return a specific command error when scheduling fails.
- Do not claim a subsystem is operational when a required command-processing task failed to start.

Existing deliberate “boot with limited functionality” behavior MAY remain, but the boot banner and machine-readable status MUST identify the unavailable component.

## 7. Cross-project API behavior

### 7.1 UART command protocol

No existing command or response line may change incompatibly. New error detail MAY be added in DATA fields or new diagnostics.

The S3 must continue to tolerate WROOM32 absence at boot, but each command invocation must have safe ownership and bounded completion.

### 7.2 Web API

Existing successful request shapes SHOULD remain compatible.

Error responses SHOULD use:

```json
{
  "ok": false,
  "error": "stable_machine_code",
  "detail": "human-readable detail"
}
```

For settings that apply in RAM but fail persistence:

```json
{
  "ok": false,
  "applied": true,
  "persisted": false,
  "error": "nvs_commit_failed"
}
```

For queued operations:

```json
{
  "ok": true,
  "accepted": true
}
```

Only return accepted after the owning queue has accepted the complete command payload.

### 7.3 Diagnostics

Add or preserve machine-readable diagnostics for:

- request timeout/abandonment.
- radio generation start/stop.
- radio stop timeout.
- decoder task creation failure.
- unsupported codec.
- resampler stall.
- I2S writer start/exit/timeout.
- audio-engine start failure and stop timeout.
- NVS persistence failure.
- partial-init rollback.

Do not log credentials or AP passwords by default in release builds.

## 8. Security/deployment hardening

The current web UI is acceptable only under the documented trusted-LAN assumption. This release MUST at least:

- stop returning the AP password from normal status APIs after provisioning.
- avoid logging Wi-Fi/AP credentials by default.
- make raw WROOM32 console forwarding disable-able with a build option.
- document that mutating web endpoints are unauthenticated in v1.

Token authentication is recommended but may be a separate feature if it would expand scope materially.

## 9. Documentation requirements

Update stale comments that still describe:

- S3 as I2S master.
- old GPIO5/6 clock pins.
- the always-on 440 Hz phase as current behavior.
- `bt_ctx` as owned exclusively by `BtAppTask`.

There MUST be one authoritative hardware contract, preferably `esp_i2s_source/docs/SPEC.md`, and component headers should refer to it rather than duplicate stale details.

## 10. Testing requirements

## 10.1 Test-first rule

Each behavioral task MUST begin with a failing test or a deterministic failure-injection hook. Structural cleanup may follow once behavior is green.

## 10.2 S3 host tests

Add or extend tests for:

- request lifetime state transitions, including abandonment vs late completion.
- no cross-request completion leakage.
- resampler consumes all 22.05/32/44.1/48 kHz input across bounded output chunks.
- compressed-tail preservation logic.
- partial PCM writes are retried or faulted explicitly.
- atomic PCM ring FIFO integrity with a pthread producer and consumer.
- ring wraparound under concurrency.
- radio lifecycle state machine: start, stop, stop timeout, failed second task creation, no overlapping generation.
- web radio queue copies URL payload and reports queue full.
- persistence helper distinguishes applied vs persisted.

AddressSanitizer MUST pass. ThreadSanitizer SHOULD be added for the pure atomic ring/lifecycle host tests if toolchain support is available.

## 10.3 WROOM32 host tests

Add or extend tests for:

- platform mutex host implementation.
- concurrent A2DP state updates and status snapshots.
- callbacks invoked without the BT context lock held.
- audio engine startup allocation failure returns failure.
- audio startup timeout does not set running.
- audio stop timeout retains task handle/state and rejects restart.
- partial initialization frees all allocations and allows retry.
- Bluetooth init rollback stage-by-stage using failure injection.

ASan and Valgrind paths should remain green.

## 10.4 Device tests

The following require explicit user confirmation before flashing hardware:

1. Boot both boards and verify the existing tone/audio path remains bit-compatible.
2. Remove or power down the WROOM32 so S3 BCLK/WS disappear; call I2S stop and verify bounded exit with no orphan task.
3. Restore clocks and verify exactly one writer starts.
4. Perform at least 100 rapid radio play/stop/play transitions; verify generation IDs never overlap.
5. Play 22.05 kHz and 32 kHz streams and verify continuous pitch/speed without periodic sample loss.
6. Disconnect/reconnect Wi-Fi during playback and verify clean rebuffer/recovery.
7. Inject or simulate decoder-task creation failure and verify the API reports failure.
8. Exercise UART command timeout and a late WROOM32 response; verify no crash and the next command receives its own response.
9. Stop WROOM audio during active streaming and verify either clean stop or explicit timeout, never forgotten task/restart duplication.
10. Run an extended soak test of at least eight hours with telemetry snapshots.

## 10.5 Acceptance invariants

During stress/soak:

- At most one radio stream task exists.
- At most one radio decoder task exists.
- At most one S3 I2S writer exists.
- At most one WROOM audio engine exists.
- No task handle is cleared before confirmed exit.
- No use-after-free/use-after-return is detected.
- No impossible/torn 64-bit statistics appear.
- No request receives another request’s completion.
- No setting reports persisted when NVS failed.
- Existing audio FFT/purity behavior is unchanged.

## 11. Build and validation commands

### S3 host tests

```bash
cd esp_i2s_source/test/host_test
rm -rf build
cmake -S . -B build -DENABLE_ASAN=ON
cmake --build build -- -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The Unity dependency currently falls back to GitHub FetchContent. Prefer an installed/vendored Unity package for offline local-LLM workflows; do not hide a network failure as a test pass.

### WROOM32 host tests

```bash
cd esp_bt_audio_source/test/host_test
rm -rf build_host_tests
cmake -S . -B build_host_tests -DENABLE_ASAN=ON
cmake --build build_host_tests -- -j"$(nproc)"
ctest --test-dir build_host_tests --output-on-failure
```

### Firmware builds

```bash
. "$HOME/esp/esp-idf/export.sh"
idf.py -C esp_i2s_source build
idf.py -C esp_bt_audio_source build
```

Do not flash without explicit user confirmation.

## 12. Completion definition

This hardening release is complete when:

- All P0 and P1 tasks in the companion TODO are checked off with evidence.
- Both projects build under ESP-IDF v5.5.1.
- Host tests pass with ASan.
- Relevant concurrency tests pass.
- Required device tests have recorded results.
- No prohibited fallback remains.
- Documentation matches the implemented ownership and hardware contracts.
