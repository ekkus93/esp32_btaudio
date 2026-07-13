# Responses: ESP32 Bluetooth Audio Reliability Hardening (Spec v1.0 + TODO v1.0)

## Questions from Spec Review

1. **Q:** `volatile` vs `_Atomic` in `radio_session_t`: Should the spec §5.2 use `_Atomic bool stop_requested` instead of `volatile bool`? The TODO already uses `_Atomic bool` which matches engineering rules §4.4 (volatile must not be used as synchronization).
   **A:**

2. **Q:** `RADIO_EVT_STARTED` / `RADIO_EVT_FAILED`: Are these event bits used anywhere in the implementation patterns, or should they be removed from the spec?
   **A:**

3. **Q:** RH-S3-03 (decoder-task creation failure): Should this be implemented as part of RH-S3-02 (same commit) or as a separate task?
   **A:**

4. **Q:** RH-WR-01 (BT context synchronization): Should this massive audit be split into sub-tasks (mutex abstraction → audit → migration → cleanup) or kept as a single task?
   **A:**

5. **Q:** RH-WR-05 (Bluetooth init rollback): Which ESP-IDF v5.5.1 APIs are available for Bluedroid/profile teardown during partial-init rollback? Specifically, can `esp_bt_ivs_disable()`, `esp_bluedroid_disable()`, `esp_bt_controller_disable()` be called during rollback?
   **A:**

6. **Q:** RH-S3-10 files: Which component files contain the "controller" state that needs to be initialized before the web server?
   **A:**

7. **Q:** RH-SEC-01 build option: Where is the raw console endpoint registered, and should it be gated behind a Kconfig option?
   **A:**

8. **Q:** Hardware tests (RH-TEST-05/06): Should these be implemented as automated tests, or are they manual acceptance criteria verified on hardware after implementation?
   **A:**

9. **Q:** Station store size (RH-S3-17): Does the station store fit in a single NVS entry, or does it span multiple keys (affecting the transaction helper design)?
   **A:**

10. **Q:** `bt_manager_is_connected()`: Should this behavior change be a sub-task of RH-WR-01 or a standalone task?
    **A:**

---

## Questions from TODO Review

11. **Q:** RH-S3-01: What is the caller context for `request_complete()` — can the caller also be inside a critical section when the worker calls it? (relates to nested critical section safety on `s_req_mux`)
    **A:**

12. **Q:** RH-S3-02: Is the intended design that `stop_requested` (atomic) is the ONLY synchronization for the worker exit path (no mutex in worker), while the control path holds the mutex for state transitions?
    **A:**

13. **Q:** RH-S3-04 (resampler): Should resampler stall (`used == 0`) be treated as `ESP_ERR_INVALID_STATE` unconditionally, or should it first check `session_should_run()` to distinguish "stopping" from "truly stalled"?
    **A:**

14. **Q:** RH-S3-05: Does `record_decoder_resync()` exist today? If not, what should it do (log? increment counter?)?
    **A:**

15. **Q:** RH-S3-07: Is relaxed ordering sufficient for the initial load of `head`/`tail` before acquiring in the CAS loop for the peak counter?
    **A:**

16. **Q:** RH-S3-08: What is the current contract of `i2s_out_pump_once()` — does it already distinguish error types (timeout vs other I2S errors)?
    **A:**

17. **Q:** RH-WR-03: Is the engine task the same as the audio processor task, or a separate worker? Does this depend on RH-WR-02 being done first?
    **A:**

18. **Q:** RH-S3-09: Which HTTP handlers currently write to `s_radio_url`?
    **A:**

19. **Q:** RH-S3-10: What is the current boot order, and which components must be reordered?
    **A:**

20. **Q:** RH-S3-12: Does `i2s_out_pump_once()` currently update stats directly?
    **A:**

21. **Q:** RH-S3-13: Is there an existing mutex acquisition order documented, or should one be defined (e.g., control → telemetry → ring) to prevent deadlock in the multi-mutex design?
    **A:**

22. **Q:** RH-S3-14: What constitutes "safe shutdown" during partial init — is it sufficient to just delete the queue (which causes the worker to exit)?
    **A:**

23. **Q:** RH-WR-04: Are `beep_manager_deinit()` and `i2s_manager_deinit()` currently safe to call when those subsystems were never initialized?
    **A:**

24. **Q:** RH-S3-19: How many HTTP handlers create tasks or queue work (to scope the audit)?
    **A:**

25. **Q:** RH-S3-21: Should the default behavior for unsupported codec sessions be terminate (safe) and document the reconnect exception?
    **A:**

---

## Questions from Cross-Cutting Issues

26. **Q:** Dependency graph: Should implicit task dependencies be explicitly documented? Specifically: RH-S3-03 depends on RH-S3-02, RH-WR-03 depends on RH-WR-02, RH-S3-09 depends on RH-S3-02.
    **A:**

27. **Q:** Phase ordering: Phase 4 (serialize control-plane) blocks Phase 5 (synchronization). Should Phase 4 be elevated to Phase 1 priority, or should Phases 4 and 5 be implemented together?
    **A:**

28. **Q:** `bt_manager_is_connected()` behavior change (spec §6.5): Should this be a sub-task of RH-WR-01 or a standalone task?
    **A:**

29. **Q:** Station store transaction semantics (RH-S3-17): If the store spans multiple NVS keys, the "copy to temp, persist, swap" approach is not actually atomic. Should the spec acknowledge this limitation or define a write-ahead log approach?
    **A:**

---

_Instructions: Fill in the `A:` lines above with your decisions, then share the file back or paste the answers._