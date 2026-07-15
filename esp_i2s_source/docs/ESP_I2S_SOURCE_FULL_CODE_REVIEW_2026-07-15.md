# `esp_i2s_source` Full Code Review

**Archive reviewed:** `esp32_btaudio-master_2607150403.zip`  
**Scope:** Only `esp_i2s_source/`, including production firmware, host tests, device tests, test tools, web UI, and the supplied runtime log.  
**Review date:** 2026-07-15  
**Verdict:** **The current firmware is not bootable as shipped, and the existing green tests do not establish that it is safe or functional.**

---

## 1. Executive summary

The immediate crash in `output.txt` is fully explained by duplicate initialization in `main/main.c`:

- `wifi_mgr_init()` is called at lines 184 and 194.
- `link_selftest()` is called at lines 188 and 191.
- `link_selftest()` itself calls `bt_link_init()` every time.

The second Bluetooth initialization reports `UART driver already installed`, and the second Wi-Fi initialization attempts to create duplicate default netifs. The supplied device log ends at:

```text
E (...) uart: UART driver already installed
DIAG|BTLINK|INIT_FAIL|err=ESP_FAIL
E (...) esp_netif_lwip: ... duplicate key
assert failed: esp_netif_create_default_wifi_sta wifi_default.c:422 (netif)
```

This is a deterministic source-level boot bug, not a wiring problem.

There is also a second independent boot-completeness failure: `app_main()` never calls `i2s_out_init()`, never calls `i2s_out_start()`, and never creates `audio_out_task()`. Therefore, removing the duplicate calls alone will allow the firmware to proceed farther, but it still will not generate the intended I2S audio stream.

The review also found several memory-safety and concurrency defects that can produce use-after-free, data races, corrupted audio, deadlocks, or silent loss:

1. `POST /api/radio` retains a pointer into a cJSON document, deletes the document, and then passes the dangling URL pointer to `radio_play_async()`.
2. `bt_link_send()` and the worker share and free a request object using an unsynchronized `abandoned` flag. The worker accesses the request after signaling the caller, while the caller can immediately delete the semaphore and free the request.
3. The I2S writer holds a FreeRTOS critical section while calling `i2s_channel_write()` with a 100 ms timeout.
4. The I2S pump removes bytes from the ring before knowing whether the sink accepted them; timeouts and partial writes lose audio.
5. Radio stop/failure paths can delete an event group and free a session while one or both worker tasks may still be running.
6. The resampler implementation is mathematically wrong for non-DC signals. The current tests primarily validate output counts and constant signals, so they do not detect the waveform error.
7. Many API/UI paths intentionally swallow errors or return HTTP 200 with `{ok:false}`, making real failures look like success.

### Priority summary

| Priority | Meaning | Count in this review |
|---|---|---:|
| P0 | Boot blocker, memory corruption, use-after-free, deadlock, or fundamentally wrong audio | 12 |
| P1 | Serious functional failure, race, data loss, security exposure, or misleading success | 41 |
| P2 | Robustness, maintainability, test quality, diagnostics, or contract ambiguity | 45+ |

The exact issue list follows. Some related findings are grouped so that the implementation plan remains actionable rather than becoming a list of tiny style observations.

---

## 2. What was checked

### 2.1 Source inventory

The directory contains the following major areas:

- `main/main.c`
- `components/i2s_out`
- `components/signal_gen`
- `components/tone`
- `components/bt_link`
- `components/wifi_mgr`
- `components/radio`
- `components/ctrl`
- `components/cmd_console`
- `components/web_ui`
- `web/src` and `web/e2e`
- `test/host_test`
- `test/test_device`
- `test/wifi_simple`
- `tools`
- `output.txt`

### 2.2 Reproducible checks

| Check | Result | Interpretation |
|---|---|---|
| Correlate `output.txt` with `main/main.c` | **Confirmed crash cause** | Duplicate Wi-Fi and Bluetooth initialization exactly explains the runtime assertion. |
| Run documented host-test entry point unmodified | **Blocked before compilation** | CMake tries to fetch Unity from GitHub when Unity is not already installed, despite the script claiming no internet is needed. |
| Host tests with temporary out-of-tree Unity/harness corrections and ASan | **18/18 CTest targets passed** | Existing assertions pass, but they miss multiple real defects found by review. No production source was changed for this check. |
| Python gate tests | **10 passed** | The parser/gate unit tests pass, but the gate permits missing I2S and link functionality by default. |
| Web build | **Not reproducible offline** | `npm ci --offline` could not satisfy the lockfile from the local cache. |
| ESP-IDF firmware build | **Not run here** | `idf.py` is not installed in this environment. The supplied hardware log is still direct evidence for the boot defect. |
| Hardware I2S/audio validation | **Not run here** | Requires the S3, WROOM32, clock wiring, and an A2DP sink. |

### 2.3 Important limitation

This is a comprehensive static and host-test review, not a claim that every possible hardware-only defect has been observed. The P0 defects below are source-proven; several are also runtime-proven by `output.txt`.

---

## 3. What is good in the code

The project has several sound design intentions worth preserving:

1. **Pure logic has been separated from ESP-IDF glue in several places.** `wifi_sm`, `ctrl_sm`, `radio_parse`, `station_store`, `radio_resampler`, `pcm_ring`, and signal generation can be host-tested.
2. **`pcm_ring` uses C11 atomics for its SPSC indices.** This is materially better than using `volatile` as a synchronization primitive.
3. **The radio lifecycle attempts to use session generations and worker-exit event bits.** The implementation is unsafe in some failure paths, but the intended ownership model is directionally correct.
4. **Station mutations are built in a candidate blob and persisted before publishing to the live store.** This is one of the few places where persistence and runtime state are treated transactionally.
5. **There are useful machine-readable diagnostics.** `DIAG|BOOT`, `DIAG|BTLINK`, `DIAG|WIFI`, `DIAG|RADIO`, and I2S telemetry are appropriate for a device gate.
6. **The audio output task is intended to be the single source arbiter.** A single owner of I2S production is the right model; it simply is not started and needs a more explicit arbitration contract.
7. **The code generally bounds copied strings.** The remaining problems are exact-boundary truncation, semantic validation, and lifetime—not widespread unbounded `strcpy` use.
8. **Tests exist for many pure components.** The problem is coverage quality and realism, not a complete absence of tests.

These strengths should be retained while fixing lifecycle ownership and error semantics.

---

# 4. P0 findings

## BOOT-001 — `wifi_mgr_init()` is called twice and crashes the device

**Files:** `main/main.c:181-196`, `components/wifi_mgr/wifi_mgr.c:245-276`  
**Evidence:** `output.txt:257-259`

The second call recreates `WIFI_STA_DEF` and `WIFI_AP_DEF`. ESP-IDF rejects the duplicate key and asserts inside `esp_netif_create_default_wifi_sta()`.

**Required fix:** Call `wifi_mgr_init()` exactly once. Also make `wifi_mgr_init()` explicitly idempotent so a future duplicate caller returns `ESP_OK` without touching ESP-IDF global state.

---

## BOOT-002 — `link_selftest()` is called twice; each call reinitializes UART1

**Files:** `main/main.c:143-164`, `main/main.c:187-191`, `components/bt_link/bt_link.c:121-181`  
**Evidence:** `output.txt:250-256`

The first self-test installs UART1. The second attempts to install it again and reports `UART driver already installed`.

**Required fix:** Initialize `bt_link` once in the boot sequence. Make the health check use the already initialized link. Make `bt_link_init()` idempotent or return a documented `ESP_ERR_INVALID_STATE` without partial changes.

---

## BOOT-003 — I2S is never initialized, started, or fed

**Files:** `main/main.c:78-115`, `main/main.c:166-236`, `components/i2s_out/i2s_out.c:107-215`

`audio_out_task()` exists but is never passed to `xTaskCreate()`. There is no call to `i2s_out_init(I2S_RING_BYTES)` or `i2s_out_start()` anywhere in the production startup path.

**Consequences:**

- `i2s_out_write()` always returns zero because `s_ring` is null.
- `audio_out_task()`, if accidentally started without initialization, would spin forever in the inner `while` with one-tick delays.
- I2S statistics remain zero.
- No audio reaches the WROOM32.

**Required fix:** Add an explicit I2S lifecycle phase and create the audio source task only after successful I2S initialization/start.

---

## WEB-001 — `POST /api/radio` has a deterministic use-after-free

**File:** `components/web_ui/web_ui_radio.c:20-64`

The handler obtains `url->valuestring`, deletes the cJSON tree, and then calls `radio_play_async(url)`. `url` points into memory owned by the deleted tree.

Representative current flow:

```c
const char *url = cJSON_GetObjectItem(j, "url")->valuestring;
cJSON_Delete(j);
return radio_play_async(url);
```

**Required fix:** Copy the URL into a fixed local buffer before `cJSON_Delete()`, validate truncation, then enqueue the copy.

---

## BTLINK-001 — Request completion has a caller/worker use-after-free race

**Files:** `components/bt_link/bt_link.c:26-42`, `:89-116`, `:191-256`

Worker completion sequence:

1. Writes the request result.
2. Calls `xSemaphoreGive(req->done_sem)`.
3. Reads `req->abandoned`.

Caller completion sequence:

1. Wakes from `xSemaphoreTake()`.
2. Copies fields.
3. Deletes `done_sem`.
4. Frees `req`.

The caller can free `req` immediately after the worker signals it, before the worker executes the `if (req->abandoned)` read. That is a use-after-free. The timeout path also writes `req->abandoned` while the worker reads it without synchronization.

**Required fix:** Replace the `abandoned` ownership scheme with explicit two-party reference counting or a worker-owned request plus a separate caller result object. The worker must never access a request after signaling and releasing its ownership.

---

## I2S-001 — Blocking I2S write occurs inside a FreeRTOS critical section

**File:** `components/i2s_out/i2s_out.c:69-100`

`writer_task()` enters `taskENTER_CRITICAL(&s_stats_mux)`, then calls `i2s_out_pump_once()`, which calls `i2s_channel_write()` with a 100 ms timeout. Critical sections disable scheduling/interrupt behavior and must be extremely short. A blocking driver call under a spinlock can stall a core, trip watchdogs, and deadlock depending on driver internals.

**Required fix:** Perform the ring read and I2S write with no critical section held. Accumulate stats in local variables and merge counters under a short critical section after the driver call.

---

## I2S-002 — Pump drains audio before the sink accepts it

**File:** `components/i2s_out/i2s_out_pump.c:9-32`

`pcm_ring_read()` advances the consumer tail before `sink()` is called. On timeout, error, or partial write, the data has already been removed and is lost. The function does not expose sink status, does not retry a partial write, and does not increment an error/drop counter.

**Required fix:** Use one of these designs:

- Add `pcm_ring_peek()` plus `pcm_ring_consume()` and consume only bytes actually written; or
- Keep a persistent pending block in the writer and do not read another block until the pending block is fully sent.

---

## RADIO-001 — Failure cleanup can free a live session

**File:** `components/radio/radio.c:783-824`

If decoder task creation fails, the code requests stream stop, waits up to the timeout, ignores whether the exit bit was observed, then deletes the event group and frees the session. If the stream task did not exit, it continues using freed session/event-group memory.

**Required fix:** Never free a session unless every task that can reference it has acknowledged exit. A timeout must retain the session, mark it faulted, and prevent teardown/restart until a controlled recovery or reboot.

---

## RADIO-002 — Faulted stop path frees workers without proving they exited

**File:** `components/radio/radio.c:827-880`

When `s_radio_state == RADIO_STATE_FAULTED`, `radio_stop_sync()` immediately clears the active pointer, deletes the event group, and frees the session. `FAULTED` is set specifically when both exit bits were *not* observed, so this path frees memory while workers may still be live.

**Required fix:** `FAULTED` must not mean “safe to free.” It must mean “ownership retained; restart blocked.” Add a separate `JOINED`/all-workers-exited condition before reclamation.

---

## RADIO-003 — Session is published after worker tasks start

**File:** `components/radio/radio.c:745-810`

Both workers are created before `s_active_session` is assigned and before state becomes `RUNNING`. A fast worker can report an error or exit before publication. The creator can then overwrite fault state with `RUNNING`, and global helpers/test hooks do not see the active session during the window.

**Required fix:** Publish the session in `STARTING` state before creating workers, or hold worker execution behind a start barrier. Only transition to `RUNNING` after both tasks have acknowledged startup.

---

## RADIO-004 — Decoder forces one-byte consumption and can corrupt compressed frames

**File:** `components/radio/radio.c:430-530`

On decoder error, if `raw.consumed == 0`, the implementation forces `raw.consumed = 1` “for progress.” Dropping one arbitrary byte from MP3/AAC input can destroy frame alignment and turn a recoverable “need more data” condition into persistent corruption. There is also no robust distinction between `need more input`, recoverable frame error, and fatal decoder error.

**Required fix:** Respect the decoder contract. Preserve pending input on `need more data`; resynchronize only using codec-aware sync-word scanning after a defined error threshold.

---

## RESAMPLE-001 — Linear resampler produces the wrong waveform

**File:** `components/radio/radio_resampler.c:1-65`

A simple 48 kHz ramp demonstrates the defect. For input `0, 1000, 2000, 3000, 4000, 5000`, a correct 48 kHz → 44.1 kHz linear resampler should produce samples near source positions `0, 1.088, 2.177, 3.265, 4.354`. The current implementation produces a delayed/interpolated sequence approximately `0, 88, 1176, 2265, 3353, 4442` and emits the wrong count. The previous-frame bookkeeping uses the wrong interval.

**Required fix:** Rewrite the resampler around an absolute/fixed-point source position with explicit left/right samples and chunk-boundary carry. Add exact ramp, impulse, sine-frequency, and chunk-equivalence tests.

---

# 5. Detailed production-code findings

## 5.1 `main/main.c`

### MAIN-001 — Boot order is duplicated and internally contradictory (P0)

Lines 181-201 repeat Wi-Fi and link setup. The comments are duplicated as well, indicating a bad merge rather than intentional redundancy.

### MAIN-002 — The “skip I2S when WROOM32 absent” comment has no implementation (P1)

Line 181 says I2S is skipped when the WROOM32 is absent, but there is no presence detection and no I2S branch at all. The behavior is neither “always start” nor “detect and start.”

### MAIN-003 — Boot self-test changes user state (P1)

`link_selftest()` sends `VOLUME 40`. A health check must not change the configured volume. It can also overwrite the orchestrator’s persisted target before the orchestrator applies it.

### MAIN-004 — Boot self-test can delay startup by roughly six seconds (P1)

Three commands use a 2,000 ms default timeout. With the companion absent, boot is synchronously delayed before radio, stations, console, control, and web startup.

### MAIN-005 — `bt_link_send()` return value is ignored in the health check (P2)

The code reports only the output state. Queue failure, invalid state, or allocation failure can be mislabeled as a protocol timeout.

### MAIN-006 — `ESP_ERROR_CHECK` turns recoverable subsystem failures into resets (P1)

Wi-Fi, radio, stations, console, ctrl initialization, and ctrl start are all fatal. A radio source should be able to expose a diagnostic web/console interface when one optional subsystem fails. Required and optional failures need an explicit policy.

### MAIN-007 — PCNT frequency telemetry consumes about 400 ms every loop (P1)

Two measurements are made sequentially for 200 ms each, followed by a 1,000 ms delay. The diagnostic interval is therefore roughly 1.4 seconds, not one second. Handles are allocated and destroyed every cycle.

### MAIN-008 — PCNT setup return values are mostly ignored (P1)

The code ignores results from edge-action setup, watch-point add, enable, clear, start, get-count, stop, and disable. Invalid data can be printed as if measured.

### MAIN-009 — PCNT high limit may be reached repeatedly during BCLK measurement (P2)

BCLK is approximately 2.82 MHz, far above the 32,000 watch point during a 200 ms window. Accumulation behavior must be tested and every operation checked; a dedicated peripheral configuration should be created once rather than rebuilt each time.

### MAIN-010 — Audio feeder has no lifecycle or fault exit (P1)

`audio_out_task()` is an immortal loop. There is no task handle, stop flag, start acknowledgment, or response to an I2S fault.

### MAIN-011 — Audio feeder can spin forever if I2S is not initialized (P1)

`i2s_out_write()` returns zero when the ring is null. The inner loop never exits.

### MAIN-012 — Source arbitration policy is ambiguous (P2)

When radio is running but buffering, `radio_audio_ready()` is false and the code emits the tone. The comment also says underrun falls to silence. The product contract should say whether buffering outputs silence or a user tone. The recommended behavior is silence while radio owns the output.

---

## 5.2 `components/i2s_out`

### I2S-003 — Lifecycle globals are not synchronized (P1)

`s_ring`, `s_tx_chan`, `s_events`, `s_writer_task`, `s_running`, and `s_state` are read/written by multiple tasks without one lifecycle mutex. `volatile` does not make a compound lifecycle data-race-free.

### I2S-004 — `start()` reports RUNNING before writer startup is acknowledged (P1)

The writer sets `I2S_EVT_WRITER_STARTED`, but `i2s_out_start()` never waits for it. It returns `ESP_OK` immediately after task creation.

### I2S-005 — Stale event bits can make future stops appear successful (P1)

`I2S_EVT_WRITER_EXITED` is consumed in `stop()`, but lifecycle transitions and task-handle assignment are not protected. Repeated start/stop calls can race with event-bit clearing and `s_writer_task = NULL`.

### I2S-006 — Stop timeout leaves an unclear ownership state (P1)

On timeout, state becomes `FAULTED`, but there is no recovery or deinit. The channel remains enabled and the writer may still exist.

### I2S-007 — No `i2s_out_deinit()` exists (P2)

The ring, event group, and I2S channel can never be cleanly reclaimed. This blocks lifecycle tests and controlled recovery.

### I2S-008 — Sink partial writes are treated as generic failure with no retry (P1)

`i2s_channel_write()` can report less than `len`; the sink returns `-1` and all data has already been removed from the ring.

### I2S-009 — Sink errors are not counted or surfaced (P1)

The stats structure only advances `bytes_written` on success. There is no `write_timeouts`, `write_errors`, `partial_bytes`, `dropped_bytes`, `last_error`, or fault transition.

### I2S-010 — Underrun semantics are inflated when the sink fails (P2)

The pump records zero-fill underruns before sink success. If the block is not written, stats claim an underrun was emitted even though nothing reached I2S.

### I2S-011 — Gain setter closes an uninitialized NVS handle (P0/P1)

`i2s_out_set_gain()` declares `nvs_handle_t h`, calls `nvs_open()`, and calls `nvs_close(h)` unconditionally. If `nvs_open()` fails, `h` is uninitialized.

### I2S-012 — Gain is changed in RAM before persistence succeeds (P1)

The runtime value changes even when NVS save fails, producing reboot-dependent behavior. Either document runtime-first semantics or persist a candidate and publish only after successful commit. The recommended contract is transactional publication.

### I2S-013 — Gain uses `volatile int` instead of an atomic or mutex (P1)

The feeder reads while HTTP/console code can write. Use `_Atomic int` for a single scalar or a config mutex.

### I2S-014 — PSRAM request silently falls back to internal heap (P1)

`pcm_ring.c:28-40` falls back to internal RAM for a 256 KiB ring. This can consume most internal heap and cause unrelated task/driver failures. A required-PSRAM allocation must fail clearly.

### I2S-015 — `capacity + 1` can overflow (P2)

`pcm_ring_create()` does not reject `capacity == SIZE_MAX` before adding one.

### I2S-016 — Ring reset concurrency contract is unclear (P2)

The SPSC ring is safe for one producer and one consumer, but `pcm_ring_reset()` changes indices outside a clearly assigned owner. Reset concurrent with producer writes can discard data unpredictably.

### I2S-017 — Wire-format documentation contradicts implementation (P2)

Comments alternate between “16-bit data in 32-bit slots” and actual `I2S_DATA_BIT_WIDTH_32BIT` samples with signed 16-bit values shifted into the top half. The latter is a valid physical representation, but the contract must be stated consistently.

### I2S-018 — Block-size comments are inconsistent (P2)

At stereo 32-bit slots, 512 bytes are 64 frames, not 128 frames. Incorrect timing comments make DMA and latency tuning error-prone.

---

## 5.3 `components/signal_gen` and `components/tone`

### SIG-001 — Public fill/reset functions lack defensive null checks (P2)

Several pure APIs dereference state/output pointers unconditionally. Tests should define whether null is rejected, ignored, or asserted.

### SIG-002 — Phase wrapping can become pathologically slow (P1)

Repeated subtraction for large phase increments can execute a huge number of iterations. Non-finite frequency can propagate NaN indefinitely. Validate finite input and use `fmod()` or bounded fixed-point phase.

### SIG-003 — Frequency is not constrained to a meaningful range (P1)

Negative values and values above Nyquist are accepted. They produce reversed/aliased or unstable output rather than a clear validation error.

### SIG-004 — Piano phase wrap subtracts one only once (P1)

For a sufficiently large phase increment, the result can remain outside `[0,1)`, invalidating oscillator and PolyBLEP assumptions.

### SIG-005 — Piano elapsed-sample counter wraps (P2)

A 32-bit counter wraps after about 27 hours at 44.1 kHz, restarting the envelope. Use a 64-bit counter or saturating elapsed time.

### SIG-006 — “Glitch-free” control changes can click (P2)

Tone on/off, amplitude, frequency, and voice changes are abrupt at block boundaries. Add a short ramp/crossfade or remove the claim.

### TONE-001 — Tone configuration is read as separate unsynchronized fields (P1)

The audio task can observe a mixed snapshot: new frequency with old voice/amplitude or vice versa. Publish a single immutable config snapshot under one mutex or packed atomic.

### TONE-002 — Status API is incomplete (P2)

There are getters for only part of the active configuration. The web status cannot accurately report amplitude, voice, ramp state, or validation errors.

---

## 5.4 `components/bt_link`

### BTLINK-002 — Initialization is neither idempotent nor cleanly rejectable (P1)

There is no module state guard or deinit. A duplicate call installs the UART before discovering other state, as proven by the boot log.

### BTLINK-003 — Task handle is discarded (P1)

`xTaskCreate()` passes a null output handle. The component cannot stop or join its owner task.

### BTLINK-004 — `xSemaphoreTake()` result is ignored (P2)

The send mutex currently uses `portMAX_DELAY`, but the return should still be checked, especially in host mocks and future timeout changes.

### BTLINK-005 — Command truncation is silent (P1)

`strncpy(req->cmd, cmd, BT_LINK_LINE_MAX - 1)` truncates overlong commands and sends the truncated command as though valid. It also permits embedded `\r`/`\n`, allowing command injection/multiple commands through the raw web terminal.

### BTLINK-006 — Empty commands are accepted (P2)

An empty line can enter the session parser and produce ambiguous terminal matching.

### BTLINK-007 — UART write result is ignored (P1)

A failed or partial `uart_write_bytes()` becomes a later protocol timeout, hiding the true transport failure.

### BTLINK-008 — Queue depth and serialization produce long head-of-line blocking (P2)

A global send mutex serializes callers across both queueing and the full response wait. A slow command blocks status polling, web handlers, and control actions.

### BTLINK-009 — Subscriber registration is a C data race (P1)

The comment says overlap is “benign in practice.” It is not data-race-free. Subscription state is mutated while the task can iterate it.

### BTLINK-010 — No unsubscribe API (P1)

`web_ui_start()` can subscribe repeatedly after restart/retry, leaving stale callback contexts and duplicate events.

### BTLINK-011 — Callbacks run on the UART owner task (P1)

A callback that calls `bt_link_send()` will block waiting for the same task that is executing the callback. This is a self-deadlock. Event callbacks should be dispatched on a separate queue/task or be explicitly nonblocking with runtime enforcement.

### BTLINK-012 — Parser APIs are not fully defensive (P2)

Null output/context/callback inputs can crash. Unknown or malformed lines are often treated as valid parsed messages with weak semantics.

### BTLINK-013 — Parsed fields point into a mutable line buffer (P2)

Callbacks must not retain those pointers. The lifetime contract is not explicit and event fan-out can accidentally persist dangling pointers.

### BTLINK-014 — Session timeout accumulator can overflow (P2)

Long-running tick accumulation in a 32-bit millisecond counter eventually wraps. Saturate or compare tick timestamps.

### BTLINK-015 — Terminal `ERR` still returns `ESP_OK` (P1)

`bt_link_send()` returns `ESP_OK` for protocol-level `DONE_ERR`, forcing every caller to inspect two channels of status. Several callers do not do this consistently. Define a clear mapping or use a structured result.

---

## 5.5 `components/wifi_mgr`

### WIFI-001 — Initialization is global-state unsafe (P0/P1)

No `s_initialized` guard exists. It creates default netifs and registers global event handlers every call.

### WIFI-002 — Function claims to return errors but aborts internally (P1)

`wifi_mgr_init()` uses `ESP_ERROR_CHECK()` for netif, Wi-Fi, power-save, and handler registration. Callers cannot handle the returned error because the process aborts first.

### WIFI-003 — Default netif creation is not checked before use (P1)

Both returned handles can be null. The current duplicate call demonstrates that this condition becomes an assertion before the component can report an error.

### WIFI-004 — Event handler instance handles are discarded (P1)

The component cannot unregister handlers during deinit/recovery.

### WIFI-005 — Shared state has no mutex (P1)

The event loop, HTTP handlers, console, status polling, and provisioning task access `s_sm`, credentials, netif handles, AP config, and started flags concurrently.

### WIFI-006 — Many ESP-IDF return values are ignored (P1)

Mode changes, config updates, starts, disconnects, reconnects, AP client list reads, mDNS calls, and live AP reconfiguration can fail silently.

### WIFI-007 — Exact maximum SSID is truncated (P1)

A Wi-Fi SSID can be 32 bytes. `cfg.sta.ssid` is 32 bytes and not a normal C-string field. `strlcpy()` copies at most 31 bytes. The same problem exists for the AP SSID.

### WIFI-008 — Exact maximum passphrase is truncated (P1)

A 64-character WPA PSK is accepted by UI comments but copied with `strlcpy()` into a 64-byte protocol field, producing at most 63 characters.

### WIFI-009 — AP `ssid_len` can disagree with copied SSID (P1)

`ssid_len` is computed from the full source string after `strlcpy()` may have truncated the destination.

### WIFI-010 — Credential erase mishandles missing keys (P1)

If `ssid` is missing, the function may not erase `pass` or commit. RAM is cleared and AP mode starts even though old credentials may remain and return after reboot.

### WIFI-011 — Runtime state and NVS can diverge (P1)

Several setters publish live/RAM values even if NVS persistence fails, or persist before a live apply whose result is ignored. Define transactional semantics.

### WIFI-012 — AP fallback does not robustly isolate stale STA events (P1)

A late `GOT_IP` event can transition the pure state machine back toward connected state unless generation/state checks reject stale events. The state machine should attach an attempt generation to events.

### WIFI-013 — mDNS is marked up after unchecked sub-operations (P1)

Only `mdns_init()` is checked. Hostname, instance name, and service creation failures are ignored while `s_mdns_up = true`.

### WIFI-014 — Public status structure contains the AP password (P1/security)

`wifi_mgr_info_t` includes `ap_pass`. Even if one current JSON handler omits it, a generic status getter should never carry a secret. Split public status from private configuration.

### WIFI-015 — Default password is globally known (P1/security)

The code deliberately sets the default to `password`. This may be acceptable for a development build because that was the desired default, but it must be explicitly marked insecure. Release builds need either a unique credential, forced first-run change, or a separate API token for mutations.

### WIFI-016 — “MAC-derived password” comments are stale (P2)

The implementation uses a fixed string while comments and documentation still claim derivation.

### WIFI-017 — Provisioning can be started concurrently (P1)

The web layer uses shared global SSID/password buffers and a task. Multiple requests can overwrite the credentials used by an already-started provisioning job.

### WIFI-018 — SSID is logged in clear text (P2/privacy)

This is common during development but should be controlled by log level; passwords must never be logged.

### WIFI-019 — No deinit or restart contract (P2)

Netifs, event handlers, mDNS, and Wi-Fi driver resources cannot be cleanly reclaimed or reinitialized.

---

## 5.6 `components/cmd_console`

### CONSOLE-001 — Start is not idempotent and has no stop/deinit (P1)

Repeated start can install drivers/tasks again. Task creation failure can leave the USB serial driver installed.

### CONSOLE-002 — Input overflow executes a truncated command (P1)

An overlong line should be discarded until newline and reported as `ERR LINE_TOO_LONG`. Executing the prefix can turn one intended command into another valid command.

### CONSOLE-003 — Parser cannot represent credentials containing spaces (P1)

Simple token splitting conflicts with valid SSID/passphrase input. Quoting/escaping or JSON input is required.

### CONSOLE-004 — Negative read errors are ignored (P2)

Driver errors should be surfaced and rate-limited rather than producing an endless silent loop.

### CONSOLE-005 — USB-Serial-JTAG ownership may conflict with the configured console (P2)

The component must state whether it owns the peripheral or uses the existing VFS console. Double ownership can break logging and CLI input.

---

## 5.7 `components/ctrl`

### CTRL-001 — `ctrl_start()` is not idempotent (P1)

It does not check `s_task`. Multiple orchestrators can act on the same sink/radio and overwrite `s_task`.

### CTRL-002 — Orchestrator reads `s_cfg` outside its mutex (P1)

The task repeatedly reads fields while HTTP setters can mutate them. A struct copy must be taken under the lock and then used locally.

### CTRL-003 — Post-save logging reads shared config after unlocking (P1)

`ctrl_set_sink()` releases the mutex and then logs fields from `s_cfg`, allowing an inconsistent log or data race.

### CTRL-004 — Scan start check has a race (P1)

`ctrl_scan()` checks `s_scan_task`, creates a task, and the task later sets `s_scan_active`. Concurrent callers can both pass before the handle is reliably published by the scheduler/mock semantics. Protect scan state with the control mutex and set state before task creation.

### CTRL-005 — `volatile bool s_scan_active` is not synchronization (P1)

Use the existing mutex or an atomic with a defined ownership protocol.

### CTRL-006 — Scan restoration is claimed even when operations fail (P1)

Disconnect, scan, connect, volume, and radio play results are mostly ignored. The log always says “A2DP restored.”

### CTRL-007 — Fixed sleeps replace actual operation completion (P1)

The scan workflow waits 1.5 s, 15 s, and 4 s regardless of actual link/inquiry events. This is slow and unreliable.

### CTRL-008 — Radio async stop/play is not confirmed (P1)

The workflow may reconnect/resume while the previous stream is still stopping, or claim resumed before play was accepted.

### CTRL-009 — Config is changed before save success (P1)

`ctrl_set_sink()` and `ctrl_note_station()` mutate live state, then save. A save failure leaves runtime and reboot state different.

### CTRL-010 — `int` station index is narrowed to `int16_t` unchecked (P1)

Large input from query parsing can wrap into a valid-looking negative/positive station value.

### CTRL-011 — Station index is not a stable identifier (P1)

Removing or reordering stations changes what `last_station` means. Persist a stable station ID, not an array index.

### CTRL-012 — Config load validation is incomplete (P1)

`autostart` is not normalized to 0/1, station existence is not checked, and an invalid MAC can remain in the loaded blob.

### CTRL-013 — Retry limit is off by one (P2)

`retries > max_retries` allows one more retry than the name implies. Define whether `max_retries` means total attempts or retries after the first attempt, then test exact boundaries.

### CTRL-014 — State-machine timing does not match wall time (P1)

The loop increments timers by a fixed 500 ms, but actions can synchronously block for seconds. Backoff and timeout behavior can be much longer than configured.

### CTRL-015 — Resume completion is reported even if volume/play fails (P1)

`CTRL_EV_RESUME_DONE` is emitted unconditionally after attempting operations.

### CTRL-016 — RUN=1 is an insufficient connection predicate (P1)

A generic run bit does not prove the expected MAC is connected and receiving this source. Status parsing should verify connected sink identity and stream state.

### CTRL-017 — Wi-Fi loss after steady state is not comprehensively handled (P1)

The orchestrator primarily focuses on the Bluetooth side. Radio/network and Wi-Fi generation changes need explicit transitions.

### CTRL-018 — Updating config does not reconfigure/restart orchestration (P2)

The live task may continue using its startup snapshot or racing shared values rather than applying the new sink/autostart policy deterministically.

---

## 5.8 `components/radio`: buffering, HTTP, decoding, and lifecycle

### RADIO-005 — `radio_init()` partial failures leak resources (P1)

Allocation and primitive creation span compressed ring, PCM ring, mutexes, queue, and worker task. Several failure exits do not unwind every previously created resource.

### RADIO-006 — `radio_init()` is not safely idempotent (P1)

Documentation and callers imply lifecycle reuse, but repeated initialization is rejected or can encounter stale globals. There is no complete deinit/reset path.

### RADIO-007 — Zero ring capacity is not rejected before modulo operations (P0/P1)

If `s_cap == 0`, ring functions use modulo zero. Validate configuration before publication.

### RADIO-008 — Large radio buffers silently fall back to internal RAM (P1)

Like the I2S ring, compressed and decoded buffers should require PSRAM on this target. Internal fallback can exhaust scarce DMA/task heap.

### RADIO-009 — Command shutdown flag is not clearly reset on reinit (P1)

After a shutdown/deinit path, a new command task can immediately exit if the atomic remains true.

### RADIO-010 — Event-bit macros are defined twice (P2)

Lines 137-141 and 185-190 duplicate the same macros, indicating merge debris and increasing the chance of future divergence.

### RADIO-011 — Public sync methods and command worker can race (P1)

Both direct synchronous calls and queued async calls mutate the same lifecycle. All lifecycle transitions should be owned by one control task.

### RADIO-012 — Stop responsiveness is poor (P1)

HTTP reads can block for seconds and reconnect backoff reaches 8 seconds, equal to the full stop timeout. A worker may not observe stop before the owner gives up.

### RADIO-013 — HTTP status is recorded but not required to be 2xx (P1)

Error pages can be fed into the codec detector/decoder and reported as codec failures instead of `HTTP_STATUS` errors.

### RADIO-014 — Playlist resolution silently falls back to the playlist URL (P1)

A failed parse/fetch is treated as a direct audio stream. The decoder then consumes playlist text or an error page, hiding the root cause.

### RADIO-015 — Playlist fetch/parse support is too permissive and incomplete (P2)

Relative URLs, BOMs, redirects, ordered PLS entries, MIME types, malformed content, and size limits need explicit handling.

### RADIO-016 — Compressed-ring overflow drops arbitrary bytes (P1)

`on_audio()` records dropped bytes but continues. Arbitrary loss inside MP3/AAC frames causes decoder corruption. Prefer backpressure, bounded connection read, or an explicit codec resync strategy.

### RADIO-017 — Telemetry is updated without one consistent lock (P1)

Some counters are written directly from stream/decoder tasks while `radio_get_status()` assumes they are protected by `s_mtx`.

### RADIO-018 — `s_prebuffered` mixes volatile and mutex access (P1)

Some reads occur outside `s_pcm_mtx`, including status composition. `volatile` does not provide coherence.

### RADIO-019 — `radio_get_status()` lock order is complex and fragile (P1)

It takes control → telemetry → PCM locks. Other paths need to follow exactly the same order. A snapshot/message-passing model would be safer.

### RADIO-020 — Status generation reports `s_next_generation`, not necessarily active generation (P2)

The public status can show a generation that does not identify the current session.

### RADIO-021 — `playing` includes STARTING while `radio_is_playing()` does not (P2)

Two APIs expose different meanings for “playing.” Define explicit `state`, `stream_active`, `audio_ready`, and `buffering` fields.

### RADIO-022 — Prebuffer byte rate uses 176 instead of 176.4 bytes/ms (P2)

The error is small but cumulative. Compute from sample-rate/channels/bytes using integer arithmetic and round deliberately.

### RADIO-023 — Prebuffer setter closes an uninitialized NVS handle (P0/P1)

`radio_set_prebuffer_ms()` unconditionally calls `nvs_close(h)` even when `nvs_open()` fails.

### RADIO-024 — Prebuffer runtime value changes before save success (P1)

Same transactional divergence as I2S gain.

### RADIO-025 — Decoder info return value is not always checked (P1)

Using uninitialized sample-rate/channel information can size output incorrectly or invoke the resampler with invalid parameters.

### RADIO-026 — Decoder channel count validation is weak (P1)

Unexpected mono/multichannel values can be interpreted as stereo or produce invalid buffer math.

### RADIO-027 — `raw.consumed` must be range-checked (P0/P1)

A buggy decoder or bad API assumption can return more consumed bytes than available, underflowing `raw.len` and moving the pointer out of bounds.

### RADIO-028 — Resampler/output stalls can silently drop decoded PCM (P1)

The code uses bounded writes and delays, but failure to make progress lacks a clear error/fault transition.

### RADIO-029 — Static decoder/header buffers prevent safe concurrent or overlapping sessions (P1)

Even accidental overlap during a lifecycle race can corrupt shared buffers.

### RADIO-030 — Error taxonomy is richer than actual assignments (P2)

Several defined error values are never set, while multiple distinct root causes collapse to decode/unknown failures.

---

## 5.9 `radio_parse`, `radio_resampler`, `station_store`, and `stations`

### PARSE-001 — Several parser loops are O(n²) (P2)

Repeated `strlen()` in loop conditions is avoidable and makes behavior worse on maximum bodies.

### PARSE-002 — PLS matching is not strict enough (P2)

The parser can accept `File` text in unintended positions and does not reliably choose `File1`, then ordered alternatives.

### PARSE-003 — ICY metadata handling is incomplete (P2)

Quoted values, escapes, malformed length blocks, and duplicate keys need explicit behavior.

### PARSE-004 — Numeric parsing uses weak conversions (P1/P2)

`atoi`-style parsing accepts prefixes, overflow, and trailing junk. Use `strtol` with full-range/end-pointer checks.

### RESAMPLE-002 — Invalid rates/channels are silently defaulted (P1)

A resampler should reject zero, negative, unsupported channel counts, and extreme ratios rather than substituting values.

### RESAMPLE-003 — Chunk-boundary equivalence is not guaranteed (P1)

Processing one buffer versus the same samples split across arbitrary chunks should produce bit-identical output. Current state handling does not meet that contract.

### STATION-001 — `stations_init()` is not idempotent (P1)

Repeated calls create/leak a mutex and can reseed/reload global state.

### STATION-002 — Any NVS load error can seed defaults and overwrite user data (P1)

Transient read errors, schema mismatch, and corruption are all treated similarly. The component then writes seed data, potentially destroying recoverable user configuration.

### STATION-003 — Seed save error is ignored (P1)

The function can return success with defaults only in RAM, then reseed again on reboot.

### STATION-004 — Blob validation only checks magic/size/count (P1)

It does not ensure every name and URL is NUL-terminated, URLs are valid, or entries are unique. Corrupt strings can cause overread in later `strlen`/compare operations.

### STATION-005 — `stations_count()` is unlocked (P1)

It reads shared mutable store state concurrently with CRUD operations.

### STATION-006 — Add maps invalid URL, duplicate, and full store to `ESP_ERR_NO_MEM` (P1)

This is actively misleading to the API/UI. Return distinct domain errors.

### STATION-007 — Names and URLs are silently truncated (P1)

Reject overlong inputs rather than saving a different value from what the user submitted.

### STATION-008 — URL validation checks only the prefix (P1/security)

It accepts missing hosts, whitespace/control characters, credentials, malformed ports, and internal/local targets. Because the web API is unauthenticated, this also creates an SSRF-style primitive from any client on the AP/LAN.

### STATION-009 — Station IDs are array indices (P1)

Reorder/removal changes identity and breaks `last_station` persistence.

### STATION-010 — Raw struct blobs have weak migration guarantees (P2)

Compiler padding/layout and schema evolution should not define persistent storage. Add an explicit version, length, checksum, and field-by-field encoding or carefully packed schema.

---

## 5.10 `components/web_ui` backend

### WEB-002 — No authentication or CSRF protection on mutating endpoints (P1/security)

Any AP/LAN client can change Wi-Fi, AP credentials, Bluetooth pairing, volume, radio URL, stations, and control config. The raw console endpoint is especially powerful.

### WEB-003 — HTTP server start is not idempotent and has no stop path (P1)

Repeated start can duplicate subscriptions/mutexes/server resources.

### WEB-004 — Unknown GET paths can fall through to the SPA (P1/P2)

An unknown `/api/...` GET should return JSON 404, not the frontend document. This masks API typos.

### WEB-005 — cJSON allocation/add failures are largely unchecked (P1)

Under memory pressure, null object/item pointers can be dereferenced or partial JSON returned as success.

### WEB-006 — Status polling performs blocking Bluetooth commands (P1)

A status endpoint can wait up to the link timeout, tying up the HTTP server task and causing overlapping frontend polls.

### WEB-007 — WROOM status cache behavior does not match comments (P2)

A “lazy refresh” comment is not implemented as a bounded cache with timestamp/generation.

### WEB-008 — Malformed tone POST can turn a tone on with defaults (P1)

Missing/invalid JSON fields should return HTTP 400 and leave current state unchanged.

### WEB-009 — Provisioning uses shared mutable global request buffers (P1)

Concurrent requests can overwrite the credentials consumed by a task.

### WEB-010 — AP enable/config handlers ignore manager apply errors (P1)

The response can report success even if ESP-IDF rejected the live change.

### WEB-011 — Station query IDs use permissive integer parsing (P1)

`atoi` accepts `1junk` as 1 and does not detect overflow.

### WEB-012 — Request body truncation is not consistently rejected (P1)

A body larger than the fixed buffer may be parsed as a valid prefix.

### WEB-013 — Bluetooth UI state is mutated before command success (P1)

Local paired/discovered/connected representations can claim an operation happened when the WROOM rejected or timed out.

### WEB-014 — Bluetooth endpoints block the HTTP task for up to seconds (P1)

Connect, scan, status, and console commands should enqueue work and expose an operation ID/state, or the HTTP server needs sufficient independent workers and strict timeouts.

### WEB-015 — Connection helper uses RUN=1 rather than expected MAC (P1)

It can claim the wrong sink is connected.

### WEB-016 — Raw console capture is global and not request-isolated (P1)

Concurrent console calls can reset, mix, or steal each other’s lines.

### WEB-017 — HTTP semantics are inconsistent (P1)

Some errors use non-2xx status, others return 200 with `{ok:false}`, and some return 200 after ignored failures. This directly contributes to frontend silent failures.

### WEB-018 — Secret/public API model mismatch (P1)

The frontend requires `ap.pass`, while the backend omits it. The right fix is not to expose the password; the frontend type and workflow should stop expecting it.

---

## 5.11 Web frontend

### UI-001 — Most mutating API functions do not check `response.ok` (P1)

Only `getJSON()` checks HTTP status. POST/PUT/DELETE helpers immediately call `r.json()`. HTML error bodies, 401/403, 404, 500, and network-proxy responses become vague JSON parse errors.

### UI-002 — Several errors are silently swallowed (P1)

Examples include station refresh, Bluetooth refresh/scan/pairing, piano note-off, arpeggio notes, and status updates using `.catch(() => {})`.

### UI-003 — Polling permits overlapping requests (P1)

`setInterval()` starts a new request even if the prior request is still running. Slow Bluetooth/status calls can pile up and resolve out of order, allowing stale state to overwrite fresh state.

### UI-004 — No AbortController/generation guard for many component requests (P1)

Requests and timers can update state after unmount or after a newer operation supersedes them.

### UI-005 — Arpeggio note requests can reorder (P1)

The interval sends tone changes without awaiting completion. Network jitter can play notes out of sequence or turn off the wrong note.

### UI-006 — Error fallback sometimes treats transport failure as success (P1)

Provisioning/AP changes can disconnect the browser legitimately, but the current pattern does not distinguish expected disconnect-after-commit from failure-before-commit. A blind catch-success policy hides real errors.

### UI-007 — Type definitions do not match backend status (P1/P2)

`ApStatus.pass` is required even though the backend should not return a secret. I2S status only models gain and omits health counters/state.

### UI-008 — No centralized API error type (P2)

The UI needs status code, endpoint, structured device error code, message, and optional retryability.

### UI-009 — Raw terminal has no client-side command constraints (P1/security)

This is not a substitute for server enforcement, but the UI should still reject embedded newlines and oversized input.

---

# 6. Test-suite findings

## TEST-001 — “Offline” host tests require a network fetch (P1)

**Files:** `tools/run_host_tests.sh:1-22`, `test/host_test/CMakeLists.txt:28-41`

The script says “No hardware, no ESP-IDF needed,” and the CMake comment says offline builds work, but a clean machine without a preinstalled Unity package invokes FetchContent from GitHub. Offline configuration fails before any test compiles.

**Required fix:** Vendor the pinned Unity source or require a user-provided local path and fail with a direct message. CI/tests must never fetch implicitly.

---

## TEST-002 — UART mock has the wrong ESP-IDF signature (P1)

**Files:** `test/host_test/mocks/include/driver/uart.h:35`, `test/host_test/mocks/fake_uart.c:13`

The mock uses six integer-like parameters instead of the real queue-size, `QueueHandle_t *`, and interrupt-flags signature. This can hide call-site mistakes and fails under strict warnings.

---

## TEST-003 — Host declaration for `ctrl_cfg_save()` is hidden (P1)

**Files:** `components/ctrl/include/ctrl_cfg.h:41-45`, `test/host_test/mocks/stubs/ctrl_cfg_host.c:16-20`

`ctrl.c` calls `ctrl_cfg_save()` in a host test, but the prototype is visible only under `ESP_PLATFORM`. The host stub defines it anyway, causing an implicit declaration under strict compilation.

---

## TEST-004 — Tautological assertion in `test_ctrl_init` (P2)

**File:** `test/host_test/test_ctrl_init.c:94`

`strlen(cfg.sink_mac) >= 0` is always true because `strlen()` returns an unsigned type. It tests nothing.

---

## TEST-005 — Current BT lifecycle tests do not reproduce the true interleaving (P0/P1)

The fake task/semaphore model does not run the worker and caller with a preemption exactly after `xSemaphoreGive()`. Therefore, the real use-after-free passes ASan host tests.

**Required test:** A pthread-backed or deterministic scheduler test that pauses the worker immediately after give, lets the caller free, then resumes the worker. The fixed implementation must pass ASan/TSan.

---

## TEST-006 — Resampler tests validate counts/DC more than signal correctness (P0/P1)

The wrong algorithm passes. Required tests must use ramps, impulses, known sine frequency/phase, arbitrary chunk splits, and a trusted reference implementation.

---

## TEST-007 — No boot-sequence host test (P0/P1)

Nothing asserts that every singleton is initialized exactly once, in order, and that I2S/audio task startup occurs.

---

## TEST-008 — No web-handler ownership/lifetime tests (P0/P1)

There is no test that poisons/frees the cJSON input before an asynchronous consumer reads the copied URL.

---

## TEST-009 — Device sine test fails by design (P1)

**File:** `test/test_device/main/test_device.c:25-34`

A sine oscillator reset at phase zero produces `sin(0) == 0`, so asserting `buf[0] != 0` is incorrect. Test the RMS/nonzero count or a later sample.

---

## TEST-010 — Device NVS round-trip may do no round-trip (P1)

**File:** `test/test_device/main/test_device.c:93-117`

If the namespace already exists, the test opens and closes it without writing or reading the key. It passes without exercising NVS.

---

## TEST-011 — Device Wi-Fi “connectivity” test never configures credentials (P0/P1)

**File:** `test/test_device/main/test_device.c:169-239`

It reads credentials but never fills/applies `wifi_config_t`, does not explicitly create the default STA netif, waits only two seconds, and passes when no IP exists. It is not a connectivity test.

---

## TEST-012 — Wi-Fi test bypasses Unity accounting (P2)

`device_test_main()` calls `test_wifi_connectivity()` directly rather than `RUN_TEST()`.

---

## TEST-013 — Heap test assumes PSRAM unconditionally (P1)

The device test fails on a valid no-PSRAM test target/config rather than conditionally asserting based on build capabilities.

---

## TEST-014 — Task-handle assertion can race task self-deletion (P2)

The dummy task can run and delete itself before the test checks the returned handle/state. The intended property should use a completion primitive.

---

## TEST-015 — `wifi_simple` does not test `wifi_mgr` (P1)

It is an independent scan-only program. It cannot detect duplicate netif creation, credential handling, state transitions, AP fallback, or provisioning races in the production component.

---

## TEST-016 — Device gate permits missing core functions by default (P1)

I2S/link/Wi-Fi requirements are optional flags. The current firmware can omit I2S startup entirely and still produce a nominal gate pass unless the caller remembers `--require-i2s`.

---

## TEST-017 — Gate can match stale markers across reboot/crash boundaries (P1)

The parser should scope assertions to the last boot generation and ensure required markers occur after boot and before any later crash signature.

---

## TEST-018 — Crash signature list is incomplete (P1)

It should include assertions, aborts, watchdogs, stack overflow, heap corruption, Guru Meditation variants, brownout, repeated reset loops, and component-specific fatal markers.

---

## TEST-019 — E2E tests depend on a live, preconfigured device (P1)

**File:** `web/playwright.config.ts:3-18`

The default target is a fixed private IP. Tests are nondeterministic and cannot run in normal CI. Several scenarios assume particular Bluetooth devices/stations and contain fixed MAC-like data.

---

## TEST-020 — Web package has no test script (P2)

`package.json` defines `dev`, `build`, and `preview`, but no `test`, `test:unit`, or `test:e2e` entry point.

---

## TEST-021 — No frontend API/polling unit tests (P1)

There are no deterministic tests for non-2xx handling, malformed JSON, overlapping polls, cancellation, stale response suppression, or expected reboot/disconnect workflows.

---

## TEST-022 — No fuzz/property testing for parsers and rings (P2)

Playlist/ICY/UART parsers, query parsing, ring wraparound, and station blobs are good fuzz targets. These components consume untrusted network/UART/NVS data.

---

# 7. Silent failures and unsafe fallbacks

These are specifically important because they create false confidence:

| Location | Behavior | Why unsafe |
|---|---|---|
| `pcm_ring.c` | PSRAM allocation silently falls back to internal RAM | Can exhaust internal heap and fail elsewhere. |
| `radio.c` URL resolution | Failed playlist resolution uses original URL | Turns root-cause parse/network failure into misleading decoder errors. |
| `radio.c` compressed overflow | Drops arbitrary bytes and continues | Corrupts codec framing. |
| `radio.c` decoder | Forces one-byte consume | Corrupts input to “make progress.” |
| `wifi_mgr.c` | Ignores many live apply errors | Reports settings that are not active. |
| `ctrl.c` | Logs restored/resumed after ignored failures | User sees success when sink/radio was not restored. |
| Web backend | Frequently returns 200 with `{ok:false}` or after ignored errors | Frontend cannot rely on HTTP semantics. |
| Web frontend | `.catch(() => {})` in core workflows | Hides outages, malformed responses, and rejected commands. |
| Device Wi-Fi test | Passes when no credentials/no IP | Labels absence of functionality as success. |
| Gate defaults | I2S/link are not mandatory | Allows a non-audio firmware to pass. |

The fix specification forbids silent fallback unless it is explicitly named, observable in status, bounded, and tested.

---

# 8. Recommended repair order

Do not start with UI polish or broad refactoring. The dependency order should be:

1. **Repair the test harness** so every local check is deterministic and strict.
2. **Fix boot order** and add a boot-sequence test.
3. **Start I2S and the audio producer** with a safe lifecycle.
4. **Fix BT request ownership** before any HTTP/control code can rely on it.
5. **Fix I2S blocking/commit semantics** and add sink-failure tests.
6. **Rewrite and verify the resampler** against a reference.
7. **Make radio lifecycle single-owner and join-safe.**
8. **Make Wi-Fi initialization idempotent and state synchronized.**
9. **Fix control/station identity and transactional persistence.**
10. **Fix backend request lifetime, status codes, authentication, and async operations.**
11. **Fix frontend error handling/poll cancellation.**
12. **Run device acceptance with I2S, BT link, Wi-Fi, radio decode, and audio output all required.**

The companion files provide the normative specification and a code-heavy implementation checklist.

---

# 9. Release-blocking acceptance summary

A build is not ready until all of the following are true:

- Clean boot for at least 100 consecutive resets with no assertion, abort, watchdog, or duplicate initialization.
- `wifi_mgr_init`, `bt_link_init`, `radio_init`, `stations_init`, `ctrl_init`, `i2s_out_init`, and task starts are each called exactly once per lifecycle.
- I2S reaches a defined `RUNNING` or `WAITING_FOR_CLOCK` state; it is never simply omitted.
- With WROOM clock present, measured WS is 44.1 kHz, BCLK is approximately 2.8224 MHz, ratio is approximately 64, and `bytes_written` increases.
- Tone produces clean audio through the WROOM32/A2DP path.
- Radio plays at least MP3 and AAC stations for a two-hour soak with bounded reconnects and no heap decline.
- No request/session object is freed before all owners/tasks release it.
- Host tests pass with `-Wall -Wextra -Werror`, ASan, UBSan, and a concurrency/TSan suite where supported.
- Web API returns structured non-2xx errors and the UI displays them rather than swallowing them.
- Mutating/raw endpoints are authenticated in release configuration.
- The device gate requires boot, I2S, BT link, Wi-Fi, and radio/audio evidence by default.

