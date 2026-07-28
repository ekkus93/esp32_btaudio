# ESP32 Bluetooth Audio Full-Duplex — FD-09 Closeout

**Branch:** `feature/esp-bt-audio-duplex`  
**Draft PR:** #2  
**Validated implementation head:** `7224acbdeb3c531265a6a7852c323a9b1c6ffe44`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Hardware flashing:** Not performed

## 1. Phase result

FD-09 is software-complete. The HFP Audio Gateway now has a bounded, allocation-free incoming HCI audio callback path that validates the active peer/session/codec contract and routes one complete CVSD PCM callback frame into the generation-bound FD-08 I2S output ring.

FD-09 does **not** start or stop SCO/eSCO. Explicit SCO and I2S session orchestration remains FD-10.

## 2. ESP-IDF v5.5.1 API correction

The installed ESP-IDF v5.5.1 non-legacy Audio Gateway API does not use an incoming/outgoing pull-callback pair. Its HCI contract is:

- register one incoming callback with `esp_hf_ag_register_audio_data_callback()`;
- receive `esp_hf_sync_conn_hdl_t`, `esp_hf_audio_buff_t *`, and `bad_frame`;
- release every non-null stack-owned buffer with `esp_hf_ag_audio_buff_free()`;
- send outgoing audio explicitly through the separate ESP-IDF send API in a later phase.

FD-09 uses this current API rather than the deprecated legacy callback interface. Calling `esp_hf_ag_audio_buff_free()` is the mandatory return of a Bluetooth-stack-owned buffer; it is not application heap allocation or a fallback allocation path.

## 3. Implementation

### 3.1 New callback component

Added:

- `components/bt_manager/include/bt_hfp_audio.h`
- `components/bt_manager/bt_hfp_audio.c`

The component owns:

- callback registration state;
- an atomic fast-state binding for session generation, synchronous connection handle, negotiated codec, and acceptance gate;
- callback current/max timing;
- protected 64-bit frame/byte/error counters;
- lifecycle-safe cleanup state.

### 3.2 Registration and lifecycle

- The incoming HCI callback is registered only after the asynchronous HFP profile-initialized event confirms that the AG profile is ready.
- Callback registration failure is returned as a real HFP profile initialization failure and drives the existing manager rollback path.
- Profile teardown closes the callback acceptance gate before requesting HFP deinitialization.
- Callback-owned state is destroyed only after Bluedroid is confirmed unable to deliver callbacks.
- Cleanup refuses destruction while any callback remains active.
- The in-flight callback count covers the full callback lifetime: validation, FD-08 ring push, mandatory ESP-IDF buffer release, and timing-stat update.
- No live callback state is freed as a permissive cleanup fallback.

### 3.3 Peer, session, and codec binding

The ESP-IDF incoming data callback does not carry a peer address. FD-09 therefore binds each accepted audio session using:

- the authoritative duplex peer and session generation;
- the audio event's synchronous connection handle;
- the callback's synchronous connection handle;
- confirmed HFP SLC state;
- confirmed HFP audio state and negotiated codec;
- confirmed FD-08 I2S output state.

A frame is accepted only when all of these identify the same active CVSD session. Wrong-peer audio events are rejected without disabling an already valid same-peer session.

### 3.4 Callback constraints

The incoming callback:

- performs no application allocation;
- performs no blocking wait or mutex acquisition;
- performs no I2S driver call;
- emits no per-frame log;
- validates null data, zero length, buffer capacity, even sample-byte length, maximum bounded frame size, active gate, nonzero generation, synchronous handle, codec, and `bad_frame`;
- makes an alignment-safe bounded stack copy before interpreting signed 16-bit PCM;
- pushes exactly one whole callback frame through `hfp_i2s_output_push_cvsd()`;
- never exposes a partial callback frame;
- visibly rejects mSBC until the later mSBC phase.

The focused build also checks the callback object for reachable `malloc`, `calloc`, `realloc`, `heap_caps_malloc`, or `heap_caps_calloc` symbols.

### 3.5 Telemetry and failure visibility

The snapshot exposes exact frame and byte counters for:

- incoming callbacks;
- accepted frames/bytes;
- total dropped frames/bytes;
- invalid input;
- inactive/stopping sessions;
- stale/wrong synchronous handles;
- baseband `bad_frame` reports;
- unsupported codec state;
- FD-08 ring rejection;
- callback timing budget crossings;
- registration and activation failures.

Callback current/max duration and the over-budget count are maintained without per-frame logging.

When teardown begins while a callback is already in flight, the component:

1. closes acceptance immediately;
2. refuses callback-state destruction while the callback count is nonzero;
3. counts the paused frame as an inactive dropped frame;
4. performs no FD-08 ring push;
5. permits cleanup only after callback completion.

No frame loss, stale-session rejection, or teardown refusal is silent.

## 4. Tests

The dedicated FD-09 ASan/UBSan suite contains 13 cases:

1. callback registration success, idempotence, and visible failure;
2. activation requires same peer, SLC, CVSD, and running I2S;
3. wrong-peer event cannot disrupt the valid active session;
4. null, zero, odd-byte, oversize, and capacity-mismatch rejection;
5. inactive and explicitly stopped callback rejection;
6. wrong synchronous-handle rejection;
7. `bad_frame` rejection;
8. visible mSBC rejection;
9. unaligned valid CVSD input copied and accepted as one whole frame;
10. exact ring-rejection frame and byte accounting;
11. stale generation rejected by the generation-bound FD-08 interface;
12. callback last/max/budget timing accounting;
13. pthread teardown race proving full callback-lifetime protection and fail-closed frame dropping.

Existing HFP AG lifecycle and manager rollback suites were extended to prove:

- audio callback registration failure faults profile initialization;
- profile deinit closes the audio callback gate before lower-layer teardown;
- callback cleanup failure preserves callback-owned state rather than freeing it.

Test-only lifecycle and I2S stubs are explicit, strongly linked host-test dependencies. Production behavior is not replaced by weak or permissive fallbacks.

## 5. Validation

Validation for code head `7224acbdeb3c531265a6a7852c323a9b1c6ffe44`:

- Strict host CI run **828**: PASS.
  - full-duplex state ASan/UBSan suite;
  - HFP AG lifecycle/event ASan/UBSan suite;
  - HFP SLC operation ASan/UBSan suite;
  - manager HFP profile rollback ASan/UBSan suite;
  - HFP PCM ring ASan/UBSan suite;
  - HFP voice conversion ASan/UBSan suite;
  - FD-08 I2S output ASan/UBSan suite;
  - FD-09 incoming audio 13-case ASan/UBSan suite;
  - allocation-symbol gate;
  - changed-Python lint gate;
  - Python unit tests;
  - complete CTest suite: 74/74 targets passed.
- ESP-IDF v5.5.1 device-build run **720**: PASS.
- Application image: **981,760 bytes**.
- Factory app partition: **1,769,472 bytes**.
- Factory app-partition headroom: **787,712 bytes**.
- Image delta from the validated FD-08 image: **+3,680 bytes**.
- `.dram0.data`: **21,840 bytes**.
- `.dram0.bss`: **51,800 bytes**.
- Static `.dram0.data + .dram0.bss`: **73,640 bytes**.
- Static DRAM delta from the FD-08 measurement: **+744 bytes**.
- Diff from the FD-08 head is limited to HFP callback/lifecycle integration, focused tests, and the CI gate.
- No hardware was flashed.

## 6. Hardware-gated work not claimed complete

FD-09 software validation does not prove:

- real WROOM32 delivery of the modern HCI incoming callback;
- real synchronous-connection handle behavior with the target earbuds;
- negotiated CVSD PCM frame sizes or callback cadence on hardware;
- callback duration on the ESP32 under real Bluetooth load;
- live SCO/eSCO establishment;
- microphone PCM arrival at GPIO27 through the FD-08 I2S0 transmitter;
- coexistence with I2S1 RX, UART2, A2DP streaming, or prolonged soak conditions.

These remain in the explicit hardware acceptance phases. No hardware-only checkbox should be marked complete from the software build.

## 7. Next phase

FD-10 adds explicit HFP audio start/stop orchestration:

- validate mode, peer, SLC, and transient-resource state;
- allocate a new audio-session generation;
- start FD-08 I2S0 before requesting SCO/eSCO;
- bind confirmed audio events to the FD-09 callback gate;
- roll I2S back on immediate SCO request failure;
- reject old-generation callbacks during stop;
- wait bounded time for SCO disconnect;
- stop FD-08 cooperatively without clearing peer identity prematurely.
