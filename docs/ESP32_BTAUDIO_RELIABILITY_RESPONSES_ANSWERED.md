# Responses: ESP32 Bluetooth Audio Reliability Hardening (Spec v1.0 + TODO v1.0)

## Questions from Spec Review


1. **Q:** `volatile` vs `_Atomic` in `radio_session_t`: Should the spec §5.2 use `_Atomic bool stop_requested` instead of `volatile bool`? The TODO already uses `_Atomic bool` which matches engineering rules §4.4 (volatile must not be used as synchronization).
   **A:** Yes. Change the spec to `_Atomic bool stop_requested` and include `<stdatomic.h>`. `volatile` must not be used for task-to-task synchronization. The control path should store with `memory_order_release`; workers should load with `memory_order_acquire`. A relaxed load would probably be sufficient for a cancellation flag by itself, but acquire/release makes the ownership contract explicit and matches the TODO.

2. **Q:** `RADIO_EVT_STARTED` / `RADIO_EVT_FAILED`: Are these event bits used anywhere in the implementation patterns, or should they be removed from the spec?
   **A:** Remove `RADIO_EVT_STARTED` and `RADIO_EVT_FAILED` from the **required** event bits in v1. The current design only needs:

```c
#define RADIO_EVT_STREAM_EXITED  BIT0
#define RADIO_EVT_DECODER_EXITED BIT1
#define RADIO_EVT_ALL_EXITED \
    (RADIO_EVT_STREAM_EXITED | RADIO_EVT_DECODER_EXITED)
```

Task-creation success is handled synchronously by `radio_play()`. Runtime network/decoder failures belong in the session lifecycle state plus `last_error`, not in one-shot event bits. `STARTED`/`FAILED` may be added later only if `radio_play()` is deliberately changed to wait for asynchronous HTTP/decoder startup acknowledgement.

3. **Q:** RH-S3-03 (decoder-task creation failure): Should this be implemented as part of RH-S3-02 (same commit) or as a separate task?
   **A:** Implement RH-S3-03 in the same commit/PR as RH-S3-02. Safe rollback of a partially created stream requires the new session object, stop flag, and exit acknowledgement. Keep RH-S3-03 as a separately named acceptance item with its own failure-injection test and evidence, but do not build an interim rollback mechanism around the old `s_playing` globals.

4. **Q:** RH-WR-01 (BT context synchronization): Should this massive audit be split into sub-tasks (mutex abstraction → audit → migration → cleanup) or kept as a single task?
   **A:** Split it into explicit sub-tasks under one top-level RH-WR-01 issue:

1. **RH-WR-01a — Mutex abstraction and lifecycle:** add the platform mutex, create it before any `bt_ctx` access, and make init/deinit retry-safe.
2. **RH-WR-01b — Access inventory:** produce a table of every production read/write of `bt_ctx`, including callbacks, scan, pairing, connect/disconnect, command handlers, and status helpers.
3. **RH-WR-01c — Migration:** convert every access to locked helpers or locked snapshots. No direct field access remains outside the owning module.
4. **RH-WR-01d — Remove obsolete queue ownership:** remove the unused/unsafe `BtAppTask` status-request path, stack request objects, and documentation claiming callbacks execute on that owner task.
5. **RH-WR-01e — Tests and documentation:** concurrency tests, timeout/error behavior, and updated ownership contract.

These may be separate commits, but the branch is not complete until the full audit proves there are no unprotected production accesses.

5. **Q:** RH-WR-05 (Bluetooth init rollback): Which ESP-IDF v5.5.1 APIs are available for Bluedroid/profile teardown during partial-init rollback? Specifically, can `esp_bt_ivs_disable()`, `esp_bluedroid_disable()`, `esp_bt_controller_disable()` be called during rollback?
   **A:** `esp_bt_ivs_disable()` is not an ESP-IDF v5.5.1 API; treat that name as a typo and do not add it.

The relevant APIs are available:

- `esp_avrc_ct_deinit()` for the AVRCP controller profile currently initialized by this project.
- `esp_a2d_source_deinit()` for the A2DP source profile.
- `esp_bluedroid_disable()` followed by `esp_bluedroid_deinit()`.
- `esp_bt_controller_disable()` followed by `esp_bt_controller_deinit()`.
- `esp_avrc_tg_deinit()` only if the project later initializes the AVRCP target role; it does not currently do so.

Required rollback policy:

1. Stop scanning and request disconnect where applicable.
2. If profile initialization was accepted, deinitialize initialized profiles while Bluedroid is still enabled. ESP-IDF specifically requires AVRCP to be deinitialized before A2DP when they are used together.
3. Because A2DP/AVRCP init and deinit report profile-state events asynchronously, register the profile callbacks before issuing the corresponding init request and use bounded acknowledgement if retryable cleanup is required.
4. Call `esp_bluedroid_disable()` only when Bluedroid reached enabled state; then call `esp_bluedroid_deinit()`.
5. Call `esp_bt_controller_disable()` only when the controller reached enabled state; then call `esp_bt_controller_deinit()`.
6. Preserve the original initialization failure as the function return value and log cleanup failures separately.
7. Do **not** call `esp_bt_mem_release()` or `esp_bt_controller_mem_release()` in rollback. Memory release is irreversible for that boot and conflicts with the requirement that initialization be retryable.

Use `esp_bluedroid_get_status()` and `esp_bt_controller_get_status()` as defensive state checks in addition to locally tracked completed stages.

6. **Q:** RH-S3-10 files: Which component files contain the "controller" state that needs to be initialized before the web server?
   **A:** The runtime controller state is primarily in:

- `esp_i2s_source/components/ctrl/ctrl.c`: `s_cfg`, `s_sm`, `s_task`, `s_mtx`, `s_scan_task`, and `s_scan_active`.
- `esp_i2s_source/components/ctrl/ctrl_cfg.c`: persistent configuration load/save used during controller initialization.
- `esp_i2s_source/components/ctrl/ctrl_sm.c`: pure FSM logic; it has no FreeRTOS initialization race but its state is instantiated by `ctrl.c`.
- `esp_i2s_source/components/ctrl/include/ctrl.h`: public lifecycle/API contract.

The handlers that currently reach controller state before `ctrl_start()` are in:

- `components/web_ui/web_ui_bt.c`: `/api/scan`, `/api/ctrl`, BT volume persistence, and the connect-volume follow-up.
- `components/web_ui/web_ui_radio.c`: `ctrl_note_station()`.

Implement `ctrl_init()` in `ctrl.c` to create the mutex, load `s_cfg`, initialize flags/state, and become safely queryable. Make `ctrl_start()` require successful initialization and only spawn the orchestrator. Update `main/main.c` to call `ctrl_init()` before `web_ui_start()`.

7. **Q:** RH-SEC-01 build option: Where is the raw console endpoint registered, and should it be gated behind a Kconfig option?
   **A:** The endpoint is declared and registered in:

- `esp_i2s_source/components/web_ui/web_ui.c`: `console_post_uri` and the `httpd_register_uri_handler()` call.
- Its implementation is `console_post_h()` in `components/web_ui/web_ui_bt.c`.

Gate it behind a component Kconfig option:

```kconfig
menu "ESP32 Bluetooth Audio Web UI"

config WEB_UI_ENABLE_RAW_CONSOLE
    bool "Enable raw WROOM32 console endpoint"
    default n
    help
        Exposes POST /api/console and permits arbitrary supported commands to
        be forwarded to the WROOM32. Enable only for trusted bench builds.

endmenu
```

Use `CONFIG_WEB_UI_ENABLE_RAW_CONSOLE` to gate the route registration and any frontend terminal controls. The secure default is `n`; a bench `sdkconfig.defaults` may explicitly set it to `y`. When disabled, the route should not be registered, rather than returning a nominal success or a soft warning.

8. **Q:** Hardware tests (RH-TEST-05/06): Should these be implemented as automated tests, or are they manual acceptance criteria verified on hardware after implementation?
   **A:** They are hardware acceptance criteria, not ordinary host unit tests.

- **RH-TEST-05:** execute on the two real boards and real I2S/UART wiring after implementation. Automate command repetition, serial capture, and log assertions where practical, but tone purity, missing-clock behavior, power cycling, and actual audio continuity remain hardware observations.
- **RH-TEST-06:** run as a real hardware soak. Claude Code should add a serial/HTTP collector and an offline analyzer so task counts, generations, counters, heap, and faults are checked automatically, but the user must initiate the run and approve flashing.

Host tests and fault injection should cover the underlying state machines first. They do not replace these hardware gates. Preserve the instruction to ask before flashing.

9. **Q:** Station store size (RH-S3-17): Does the station store fit in a single NVS entry, or does it span multiple keys (affecting the transaction helper design)?
   **A:** It is one logical NVS key, not a multi-key store:

- Namespace: `radio`
- Key: `stations`
- Value: one `stations_blob_t` written by one `nvs_set_blob()` call.

With the current constants, the blob is approximately 12,168 bytes (`40 * (48 + 256)`, plus `count` and `magic`, subject to normal alignment). NVS may store that blob across multiple internal entries/pages, but the application-level transaction is still one key and one `nvs_commit()`.

Refactor `save_locked()` into something like `save_store_locked(const station_store_t *candidate)` returning `esp_err_t`. Mutate a temporary `station_store_t`, persist that candidate blob, commit it, and only then assign it to `s_store`.

10. **Q:** `bt_manager_is_connected()`: Should this behavior change be a sub-task of RH-WR-01 or a standalone task?
    **A:** Make it a clearly named sub-task and acceptance criterion of RH-WR-01, not a separate architectural task. Once `bt_ctx` has one canonical mutex-protected snapshot path, `bt_manager_is_connected()` should call that path directly. It must no longer depend on the unused `BtAppTask` queue.

The Boolean convenience API may return false if the snapshot lock cannot be obtained, but it must emit a rate-limited diagnostic. APIs capable of returning `esp_err_t` must preserve the actual error.

---

## Questions from TODO Review


11. **Q:** RH-S3-01: What is the caller context for `request_complete()` — can the caller also be inside a critical section when the worker calls it? (relates to nested critical section safety on `s_req_mux`)
    **A:** `request_complete()` must be called from normal task context with `s_req_mux` **not already held**. It is not an ISR API and it is not a nested-lock API.

The caller may concurrently be inside the same critical section while the worker reaches `request_complete()`; the worker simply waits for the caller to leave. That is expected and safe. The implementation must:

- copy result fields outside the critical section;
- enter `s_req_mux` only to decide/transition ownership;
- leave the critical section before `xSemaphoreGive()`, `vSemaphoreDelete()`, or `free()`;
- never call `request_complete()` from a helper that already owns `s_req_mux`.

If an already-locked path is needed, create a small `request_transition_locked()` helper and keep completion/destruction outside the lock.

12. **Q:** RH-S3-02: Is the intended design that `stop_requested` (atomic) is the ONLY synchronization for the worker exit path (no mutex in worker), while the control path holds the mutex for state transitions?
    **A:** The atomic `stop_requested` flag is the only **cancellation signal** that workers poll. It is not the only synchronization used by the session.

The intended division is:

- control mutex: protects `s_active_session`, generation publication, and lifecycle transitions;
- atomic stop flag: lets stream/decoder workers exit without taking the control mutex in their hot or blocking loops;
- event bits: workers acknowledge exit;
- telemetry mutex/atomics: protect status fields;
- ring-specific synchronization: protects compressed and PCM storage.

The control path must set `STOPPING` and the atomic flag, release the control mutex, and only then wait for exit bits. It must never hold the control mutex across HTTP operations, decoder work, delays, or `xEventGroupWaitBits()`. Prefer making exit bits authoritative and clearing task handles only after join; if a worker writes its own handle, that write must also be synchronized.

13. **Q:** RH-S3-04 (resampler): Should resampler stall (`used == 0`) be treated as `ESP_ERR_INVALID_STATE` unconditionally, or should it first check `session_should_run()` to distinguish "stopping" from "truly stalled"?
    **A:** First check `session_should_run()` so an in-progress stop exits normally without recording a fault.

Also correct the stall definition: `used == 0` by itself is not always a failure. During upsampling, a small output capacity can legitimately produce one or more output frames before an input frame is consumed. Treat the call as making progress when either `produced > 0` or `used > 0`.

A true stall is:

```c
produced == 0 && used == 0 && remaining_input_frames > 0
```

while the session is still expected to run. That condition should record `resampler_stalled`, return an explicit error, and terminate or fault the session rather than spinning or dropping input. Add a bounded no-consume iteration guard for the unusual `produced > 0 && used == 0` case so a broken resampler cannot loop forever.

14. **Q:** RH-S3-05: Does `record_decoder_resync()` exist today? If not, what should it do (log? increment counter?)?
    **A:** It does not exist today. Add it as a small synchronized telemetry helper.

Required behavior:

- increment a new `decoder_resyncs` counter;
- also increment the existing aggregate `decode_errors` counter for backward-compatible error totals;
- emit a rate-limited warning containing generation, codec, pending-byte count, and consecutive no-progress count;
- do not overwrite a terminal `last_error` merely for one recoverable resync;
- reset the consecutive no-progress counter after successful consumption;
- if a defined threshold is exceeded, reset/reopen the decoder or terminate the session with `decoder_stalled`.

Do not log every discarded byte; that can flood the UART and make the timing problem worse.

15. **Q:** RH-S3-07: Is relaxed ordering sufficient for the initial load of `head`/`tail` before acquiring in the CAS loop for the peak counter?
    **A:** Yes. Relaxed ordering is sufficient for the `peak` atomic load and compare/exchange because `peak` is an independent monotonic statistic; it does not publish or consume ring payload data. The head/tail acquire/release operations provide the actual SPSC data ordering.

Use relaxed ordering for both CAS success and failure. `pcm_ring_reset()` remains legal only while producer and consumer are quiesced. If concurrent reset is ever supported, it needs a separate lifecycle lock/protocol rather than stronger ordering on `peak` alone.

16. **Q:** RH-S3-08: What is the current contract of `i2s_out_pump_once()` — does it already distinguish error types (timeout vs other I2S errors)?
    **A:** No. The current function returns only the number of real bytes drained from the ring. The sink callback returns `0` or nonzero internally, but `i2s_out_pump_once()` does not expose timeout versus fatal error to its caller. The TODO snippet that assigns its `size_t` return to `int rc` and tests `rc < 0` is therefore incorrect.

Use this design instead:

1. Keep a pure helper that **prepares** one block: drain the ring, zero-fill, and return a local stats delta.
2. In the device writer, call `i2s_channel_write()` with a finite timeout.
3. On `ESP_ERR_TIMEOUT` while still running, retry the **same scratch block**; do not drain another block and silently lose the timed-out audio.
4. On stop request, discard the retained block and exit.
5. On another I2S error or a partial write, record a device error and enter the defined retry/fault policy.
6. Commit the local stats delta under the stats lock; count `bytes_written` only after a complete successful write.

A suitable pure API is:

```c
typedef struct {
    size_t real_bytes;
    size_t underrun_bytes;
    uint32_t underrun_events;
    size_t ring_peak;
} i2s_out_stats_delta_t;

size_t i2s_out_prepare_block(
    pcm_ring_t *ring,
    uint8_t *scratch,
    size_t block_len,
    i2s_out_stats_delta_t *delta);
```

This separation preserves host testability and gives the device layer enough information to distinguish timeout from fatal I2S errors.

17. **Q:** RH-WR-03: Is the engine task the same as the audio processor task, or a separate worker? Does this depend on RH-WR-02 being done first?
    **A:** It is a separate worker task named `audio_engine`, owned by the `audio_processor` component. It is not the task calling `audio_processor_start()` and it is not a second public component.

RH-WR-03 depends on the lifecycle foundation in RH-WR-02 because both modify the same task handle, event bits, and state enum. Implement RH-WR-02 first or implement both in one coherent commit. Do not add startup acknowledgement while retaining the old stop path that can forget a live task.

18. **Q:** RH-S3-09: Which HTTP handlers currently write to `s_radio_url`?
    **A:** Only `radio_post()` in `components/web_ui/web_ui_radio.c` writes `s_radio_url` (`strlcpy` at the current line 55). `radio_play_task()` reads it. `radio_delete()` does not write it; it only creates the stop task. The controller/orchestrator calls `radio_play()` with its own local URL and does not use `s_radio_url`.

19. **Q:** RH-S3-10: What is the current boot order, and which components must be reordered?
    **A:** Current relevant boot order in `main/main.c` is:

1. NVS.
2. I2S init, audio feeder task, I2S start.
3. `bt_link_init()` indirectly through `link_selftest()`.
4. Wi-Fi manager.
5. Radio.
6. Stations.
7. Console.
8. Web server.
9. `ctrl_start()` — which currently creates the controller mutex, loads config, and starts the orchestrator.

Required order:

1. Keep NVS and the audio/link/Wi-Fi/radio/station prerequisites.
2. Call new `ctrl_init()` after NVS-backed dependencies are ready and **before** the web server.
3. Call `web_ui_start()`.
4. Call `ctrl_start()` or `ctrl_start_orchestrator()` to create only the long-running orchestrator task.

Also initialize the web UI’s own BT mutex/subscription before its routes can be invoked. `web_ui_bt_init()` currently happens after the HTTP server has started and handlers have been registered; split local-state initialization from the optional `PAIRED` priming command if necessary.

20. **Q:** RH-S3-12: Does `i2s_out_pump_once()` currently update stats directly?
    **A:** Yes. The current `i2s_out_pump_once()` directly mutates the `i2s_out_stats_t *stats` supplied by `i2s_out.c`: underrun bytes/events, successful bytes written, and ring peak.

Do not protect the entire current pump call with a critical section because the sink can block. Apply the RH-S3-08 split described above: the pure block-preparation helper returns a local stats delta, and the device writer commits that delta to `s_stats` under `s_stats_mux` after the write outcome is known.

21. **Q:** RH-S3-13: Is there an existing mutex acquisition order documented, or should one be defined (e.g., control → telemetry → ring) to prevent deadlock in the multi-mutex design?
    **A:** There is no existing documented global mutex order. Define one in `radio.c` and in the component documentation.

Preferred rule: avoid nested locks. In particular, never hold any radio mutex during HTTP calls, decoder calls, delays, event waits, NVS commits, or queue sends.

When two state locks are unavoidable, the order is:

```text
control/lifecycle mutex -> telemetry mutex
```

Never acquire them in the reverse order. Compressed-ring and PCM-ring locks are leaf locks: do not hold the control or telemetry mutex while acquiring them, and never nest the two ring locks with each other. Snapshot lifecycle/generation, release the state locks, then sample ring occupancy separately. Include the generation in telemetry so callers can detect a snapshot that crossed a session transition.

22. **Q:** RH-S3-14: What constitutes "safe shutdown" during partial init — is it sufficient to just delete the queue (which causes the worker to exit)?
    **A:** No. Deleting the queue does not cause the current `bt_link_task()` to exit; it loops forever and repeatedly accesses the queue. Deleting a queue while that task is alive is unsafe.

For partial initialization, acquire resources in this order:

1. install/configure UART;
2. create queue and synchronization objects;
3. initialize pure parser/session state;
4. create the worker task **last**;
5. publish the component as ready only after task creation succeeds.

With task creation last, every earlier failure has no live worker and cleanup may safely delete the passive objects and UART driver. There should be no fallible initialization step after successful task creation. If a future design adds such a step, add a stop flag plus worker-exited acknowledgement and join the worker before deleting its queue, semaphores, or UART driver.

23. **Q:** RH-WR-04: Are `beep_manager_deinit()` and `i2s_manager_deinit()` currently safe to call when those subsystems were never initialized?
    **A:** In the current implementation:

- `i2s_manager_deinit()` explicitly checks `s_mgr.initialized` and returns, so it is idempotent before successful init.
- `beep_manager_deinit()` does not check `s_initialized`, but it only clears static overlay state under its static lock and sets the flag false, so it is currently safe to call before init.

Still track successful subordinate initialization with booleans and clean up in reverse order. Do not rely on accidental idempotence as the ownership contract. While touching `beep_manager_deinit()`, clear any stored callback/context so a failed init followed by retry cannot retain stale ownership.

24. **Q:** RH-S3-19: How many HTTP handlers create tasks or queue work (to scope the audit)?
    **A:** There are four direct `xTaskCreate()` scheduling sites in HTTP handler code:

1. `/api/wifi` provisioning: `provision_task`.
2. `POST /api/radio`: `radio_play_task`.
3. `DELETE /api/radio`: `radio_stop_task`.
4. `POST /api/bt` connect follow-up: `connect_volume_task`.

There is one additional indirect scheduling path:

5. `POST /api/scan` calls `ctrl_scan()`, which creates `scan_task`. This path already checks the return value, but it belongs in the audit.

There are currently no direct `xQueueSend()` calls in `components/web_ui`. `bt_link_send()` queues internally but is synchronous from the handler’s perspective and is covered by RH-S3-01. After RH-S3-09, the two radio handlers will become checked queue submissions instead of task creation. Audit these five scheduling paths plus the new radio queue initialization.

25. **Q:** RH-S3-21: Should the default behavior for unsupported codec sessions be terminate (safe) and document the reconnect exception?
    **A:** Yes. The default for a known unsupported codec/content type is a terminal session error:

- set `last_error = unsupported_content_type`;
- record the received content type;
- close/cleanup the HTTP client;
- stop both workers;
- expose the fault through status;
- do not reconnect indefinitely.

Reconnect remains appropriate for transport failures, timeouts, connection resets, and retryable HTTP statuses. For an absent or ambiguous content type, a bounded byte-sniff/decoder probe is acceptable. Any exception must have a fixed retry/probe limit; deterministic unsupported content must never fill the compressed ring forever.

---

## Questions from Cross-Cutting Issues


26. **Q:** Dependency graph: Should implicit task dependencies be explicitly documented? Specifically: RH-S3-03 depends on RH-S3-02, RH-WR-03 depends on RH-WR-02, RH-S3-09 depends on RH-S3-02.
    **A:** Yes. Add an explicit dependency graph near the top of the TODO and repeat dependencies in each task header.

Required relationships:

```text
RH-S3-02 -> RH-S3-03
RH-S3-02 -> RH-S3-09
RH-S3-02 + RH-S3-09 -> RH-S3-13
RH-S3-02 -> RH-S3-21
RH-S3-08 -> RH-S3-12   (prefer one coherent change)
RH-WR-02 -> RH-WR-03
RH-WR-01 -> RH-WR-05   (rollback owns/deletes the new context mutex)
```

Also remove duplication: RH-S3-01 already requires complete `bt_link_init()` rollback, so RH-S3-14 should be marked **merged into RH-S3-01** rather than implemented later as a second independent cleanup design.

Phase headings are organizational groupings, not permission to violate these dependencies.

27. **Q:** Phase ordering: Phase 4 (serialize control-plane) blocks Phase 5 (synchronization). Should Phase 4 be elevated to Phase 1 priority, or should Phases 4 and 5 be implemented together?
    **A:** Do not elevate every Phase 4 task to P0. Reorder implementation by subsystem instead of treating phase numbers as strict barriers:

- Radio sequence: RH-S3-02/03, then RH-S3-09, then RH-S3-13.
- I2S sequence: RH-S3-08 and RH-S3-12 in the same commit or back-to-back before validation.
- WROOM sequence: RH-WR-01, RH-WR-02, RH-WR-03, then RH-WR-05 as dependencies permit.

RH-S3-09 remains P1 after RH-S3-02 removes the duplicate-generation memory hazard, but it should be executed immediately afterward because it removes URL/control races and provides one owner for later telemetry. Phases 4 and 5 should therefore be interleaved by dependency, not implemented as two isolated bulk phases.

28. **Q:** `bt_manager_is_connected()` behavior change (spec §6.5): Should this be a sub-task of RH-WR-01 or a standalone task?
    **A:** It is a sub-task of RH-WR-01, with its own explicit acceptance test. Use the same decision as answer 10. Do not create a separate competing status mechanism.

29. **Q:** Station store transaction semantics (RH-S3-17): If the store spans multiple NVS keys, the "copy to temp, persist, swap" approach is not actually atomic. Should the spec acknowledge this limitation or define a write-ahead log approach?
    **A:** Acknowledge the general limitation, but the current store does **not** span multiple application keys. It is one blob under `radio/stations`, so no write-ahead log is required for RH-S3-17.

For the current implementation:

1. copy live store to a temporary candidate;
2. mutate the candidate;
3. write the complete candidate to the single blob key;
4. call `nvs_commit()`;
5. only on success copy/swap the candidate into live RAM.

If a future schema is split across multiple keys, a simple “write all keys, then swap RAM” sequence is not an atomic multi-key transaction. At that point use a dual-slot design: write a complete inactive generation (`stations_a` or `stations_b`) with generation and CRC, commit it, then atomically switch a small single-key manifest to the new generation. A power loss before the manifest switch leaves the previous generation active.

---

## Version-sensitive reference note

Answer 5 was checked against the ESP-IDF Bluetooth Main, Controller/HCI, A2DP, and AVRCP API documentation available for the ESP-IDF 5.5 generation. Claude Code must still compile against the project’s installed ESP-IDF v5.5.1 headers and treat those headers as the final authority for exact symbols and event fields.

_All questions above have been answered. Implement the decisions rather than reopening architectural alternatives unless the repository or ESP-IDF compiler proves a stated API assumption false._