# ESP32 Bluetooth Audio Full-Duplex Implementation Progress

**Branch:** `feature/esp-bt-audio-duplex`
**Draft PR:** #2
**Baseline merge commit:** `cb58d0b47cfc683542cae62efce2a1e66365c3a9`
**HFP configuration commit:** `cfa3c5f35fe81ed82bc0869578da0b84db8e6f70`
**Latest validated head:** `2d2d9d40a73127ed0f485ba7db79d9b5a492a7c7`

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
