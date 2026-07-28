# ESP32 Bluetooth Audio Full-Duplex Implementation Progress

**Branch:** `feature/esp-bt-audio-duplex`
**Draft PR:** #2
**Baseline merge commit:** `cb58d0b47cfc683542cae62efce2a1e66365c3a9`
**HFP configuration commit:** `cfa3c5f35fe81ed82bc0869578da0b84db8e6f70`
**Latest validated code head:** `172f8c5ccc3db1c48d5affd6ab116d065a2ed310`
**FD-07 documentation closeout commit:** `985fdbd3990d50234fdb3ba3ed3aa96b340b04b4`

## FD-00 — Branch and scope baseline

- Confirmed the feature branch contains current `master`.
- Confirmed the companion spec and TODO exist at their documented paths.
- Confirmed the intended scope is limited to `esp_bt_audio_source` and maintained project documentation.
- Ran `esp_bt_audio_source/tools/ci_check_main_layering.sh esp_bt_audio_source/main/main.c`: PASS.
- Opened draft PR #2 so host and ESP-IDF device builds validate each commit.
- No hardware was flashed.

## FD-01 — Pre-HFP compile baseline

Baseline commit `cb58d0b47cfc683542cae62efce2a1e66365c3a9`:

- Host CI run 515: PASS.
- ESP-IDF v5.5.1 device-build run 418: PASS.
- Application image: 927,392 bytes.
- Factory app partition: 1,769,472 bytes (`0x1B0000`).
- App-partition headroom: 842,080 bytes.
- `.dram0.data`: 21,824 bytes.
- `.dram0.bss`: 44,432 bytes.
- Static DRAM total for those sections: 66,256 bytes.

Runtime heap, stack, A2DP 10-minute, and hardware counters remain hardware-gated and are not claimed complete.

## FD-02 — HFP link-time configuration

Configured the tracked `sdkconfig` and `sdkconfig.defaults` for:

- HFP enabled.
- Audio Gateway role enabled.
- Hands-Free client role disabled.
- HCI SCO data path selected in host and controller.
- One synchronous SCO/eSCO connection.
- Internal codec path retained.
- WBS/mSBC disabled for initial CVSD bring-up.

Validated clean head `7f2eb7a78cb558f187eda6c7097a5792eb09066e`:

- Host CI run 529: PASS.
- ESP-IDF v5.5.1 device-build run 432: PASS.
- A2DP remains enabled.
- No HFP profile or SCO API is initialized by application code yet.
- Application image: 969,856 bytes, an increase of 42,464 bytes.
- App-partition headroom: 799,616 bytes (about 781 KiB), above the 256 KiB gate.
- `.dram0.data`: 21,840 bytes, an increase of 16 bytes.
- `.dram0.bss`: 50,536 bytes, an increase of 6,104 bytes.
- Static DRAM total: 72,376 bytes, an increase of 6,120 bytes.

The FD-02 link-time stop condition passes.

## FD-03 — Authoritative duplex state and snapshot

Implemented on `feature/esp-bt-audio-duplex`:

- Public typed states for duplex mode, A2DP, HFP profile/audio, negotiated codec, I2S output, and health.
- One mutex-protected snapshot containing the peer, session generation, requested/effective modes, all profile/audio states, last error, and protected 64-bit counters.
- Case-insensitive same-peer enforcement and stale-generation rejection with explicit counters.
- Checked legal transition matrices for A2DP, HFP, and I2S state.
- Codec state derived only from confirmed HFP audio state.
- Fault/quarantine state cannot be silently downgraded through an ordinary setter.
- Explicit recovery requires a fault and proof that transient audio/I2S resources are stopped.
- Same-peer session restart is rejected while transient resources remain active.
- Stable and exhaustively tested state-to-string contracts.
- No callbacks are invoked while the state lock is held; no `volatile` synchronization is used.

Validation for head `56d6deae200333adfc460419daf6696386ecb9e9`:

- Focused state suite: 13 tests, PASS under AddressSanitizer and UndefinedBehaviorSanitizer.
- Strict host CI run 573: PASS.
- Python test and CTest failures now propagate instead of being converted to success.
- ESP-IDF v5.5.1 device-build run 474: PASS.
- No hardware was flashed.

## FD-04 — Bounded SPSC HFP PCM ring

Implemented on `feature/esp-bt-audio-duplex`:

- Separate HFP microphone ring; the existing A2DP playback ring is not reused.
- Caller-owned fixed storage with no allocation, PSRAM fallback, or memory substitution.
- Single-producer/single-consumer nonblocking byte ring using lock-free 32-bit atomics.
- Whole-frame all-or-nothing writes; unread bytes are never overwritten.
- Wraparound-safe reads and writes with monotonic 32-bit positions.
- Generation-checked producer and consumer operations reject stale sessions visibly.
- Exact current/capacity/peak/total/overflow/underflow/stale/invalid statistics.
- Sequence-protected split 64-bit counters avoid torn reads on the 32-bit target.
- Snapshot stabilization and counter reads use bounded retries and return `ESP_ERR_TIMEOUT`; no unbounded retry loop remains.
- Reset requires explicit proof that both endpoints are stopped and a new nonzero generation.

Validation for head `318609fcf34500f0cfa90efd757ad43e0efbffb2`:

- Focused ring suite: 8 tests, PASS under AddressSanitizer and UndefinedBehaviorSanitizer.
- Includes a 50,000-frame pthread producer/consumer order-preservation stress test.
- Strict host CI run 605: PASS.
- ESP-IDF v5.5.1 device-build run 505: PASS.
- No hardware was flashed.

## FD-05 — HFP voice conversion helpers

Implemented on `feature/esp-bt-audio-duplex`:

- Exact all-or-nothing CVSD 8 kHz to 16 kHz sample duplication.
- Signed 16-bit stereo-to-mono conversion using widened arithmetic and explicit narrowing safety.
- Stateful input-driven conversion from canonical mono PCM to 8 kHz or 16 kHz.
- Partial averaging windows persist across arbitrary input chunk boundaries.
- Explicit consumed/produced counts prevent hidden input loss when destination capacity is exhausted.
- The converter never zero-pads or fabricates output when insufficient input exists.
- Equal-rate 16 kHz input is an exact passthrough.
- Explicit reset removes prior-session phase and partial-window state.

Validation for head `2d2d9d40a73127ed0f485ba7db79d9b5a492a7c7`:

- Focused conversion suite: 11 tests, PASS under AddressSanitizer and UndefinedBehaviorSanitizer.
- Includes 44.1 kHz one-shot versus irregular-chunk equivalence testing.
- Strict host CI run 621: PASS.
- ESP-IDF v5.5.1 device-build run 520: PASS.
- No hardware was flashed.

## FD-06 — HFP Audio Gateway profile lifecycle

Implemented on `feature/esp-bt-audio-duplex`:

- Registers the ESP-IDF v5.5.1 HFP Audio Gateway callback only after Bluedroid is enabled.
- Treats `esp_hf_ag_init()` and `esp_hf_ag_deinit()` as asynchronous requests; profile readiness changes only after `ESP_HF_PROF_STATE_EVT` confirms completion.
- Keeps HFP audio disconnected by default and registers no SCO data callbacks in this phase.
- Normalizes profile, SLC, audio-link, codec, volume, unknown-AT, wrong-peer, and remote-disconnect events into the authoritative duplex state.
- Records whether the lower-layer HFP initialization request was accepted. Callback-registration failure and immediate init-request rejection do not call an invalid lower-layer deinit; callback-reported failure and timeout receive bounded rollback.
- Preserves the original initialization failure even if rollback also fails.
- Rolls HFP, A2DP, and AVRCP back in reverse order.
- Refuses to destroy HFP callback-owned synchronization state unless Bluedroid is confirmed `UNINITIALIZED`; incomplete teardown quarantines manager reinitialization instead of risking a late-callback use-after-free.
- The focused manager rollback target compiles only the actual FD-06 dependency graph and retains compiler/sanitizer logs.
- Python lint now strictly gates Python files changed by the branch. The pre-existing full-tree lint backlog remains explicitly reported in `flake8-full-tree.log` rather than being hidden or made an unrelated feature blocker.
- Operation-generation binding/rejection is assigned to FD-07 because ESP-IDF HFP callbacks carry peer/state but no connect-operation generation token.

Validation for code head `70d55d57c5df7fc9ba96a174d96260f660191378`:

- HFP AG lifecycle/event sanitizer suite: PASS.
- Manager HFP profile-init/rollback sanitizer suite: PASS.
- Full-duplex state, HFP PCM ring, and voice-conversion sanitizer suites: PASS.
- Strict host CI run 679: PASS, including changed-Python lint, Python unit tests, and full CTest.
- ESP-IDF v5.5.1 device-build run 574: PASS.
- No hardware was flashed.

On-device confirmation of real `ESP_HF_PROF_STATE_EVT` delivery remains hardware-gated and is not claimed complete.

## FD-07 — Generation-bound HFP SLC connect/disconnect APIs

Implemented on `feature/esp-bt-audio-duplex`:

- Public `bt_hfp_connect()` and `bt_hfp_disconnect()` declarations are exported through `bt_manager.h`; `bt_duplex_get_snapshot()` remains the authoritative completion/status view.
- MAC addresses are parsed with the existing strict helper, and HFP operations require the same active peer already owned by the A2DP/ACL manager state.
- A different remote peer is rejected and counted; no second headset/session is silently accepted.
- Lower-layer `esp_hf_ag_slc_connect()` and `esp_hf_ag_slc_disconnect()` calls run through the existing BtAppTask work-dispatch ownership model.
- The synchronous API result means the queued request reached the lower layer and was accepted or rejected immediately. SLC completion remains asynchronous and is confirmed only by HFP connection-state callbacks.
- Every accepted operation records the active session generation, peer, serial number, operation type, deadline, immediate result, completion state, and explicit failure counters.
- Late same-peer events after timeout or generation change are ignored and counted instead of mutating the current session.
- A bounded BtAppTask request wait and a bounded SLC watchdog replace unbounded waits. Timeout leaves the profile state unchanged and degraded/visible; it never fabricates disconnection.
- Remote connect rejection is reported asynchronously as a rejected operation with an explicit counter/error.
- Already-connected connect and already-disconnected disconnect are deterministic and idempotent.
- FD-07 never starts SCO audio or registers SCO data callbacks.
- FD-07 timer, semaphore, and mutex cleanup is chained to `bt_hfp_ag_force_cleanup_after_stack_shutdown()`, after Bluedroid is confirmed unable to deliver callbacks.
- Standalone AG/manager host suites use explicit test-only cleanup stubs; production behavior is not replaced by a permissive fallback.

Validation for code head `172f8c5ccc3db1c48d5affd6ab116d065a2ed310`:

- Focused HFP SLC operation sanitizer suite: PASS.
- HFP AG lifecycle/event and manager profile rollback sanitizer suites: PASS.
- Full-duplex state, HFP PCM ring, and HFP voice conversion sanitizer suites: PASS.
- Strict host CI run 721: PASS, including changed-Python lint, Python unit tests, and full CTest.
- ESP-IDF v5.5.1 device-build run 615: PASS.
- No hardware was flashed.

Real WROOM32 SLC event delivery and earbud behavior remain hardware-gated and are not claimed complete.

## Next phase

FD-08 adds the I2S0 TX microphone-output component. It must preserve the selected GPIO32/GPIO33/GPIO27 master-output contract, reject pin conflicts visibly, and provide bounded writer-task shutdown and quarantine behavior.
