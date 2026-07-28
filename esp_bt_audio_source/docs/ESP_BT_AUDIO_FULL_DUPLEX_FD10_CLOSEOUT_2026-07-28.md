# ESP32 Bluetooth Audio Full-Duplex — FD-10 Closeout

**Branch:** `feature/esp-bt-audio-duplex`  
**Draft PR:** #2  
**Validated implementation head:** `7d607afd3d1aedfc5a95061dad434fc36e076d8a`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Hardware flashing:** Not performed

## 1. Phase result

FD-10 is software-complete. The HFP Audio Gateway now exposes bounded, generation-bound `bt_hfp_audio_start()` and `bt_hfp_audio_stop()` orchestration that coordinates the authoritative duplex state, FD-08 I2S0 microphone output, FD-09 incoming HCI callback gate, BtAppTask ownership, and callback-confirmed HFP audio-link events.

A successful API return means the lower-layer request was accepted, the matching HFP audio event was confirmed, and the local I2S lifecycle reached the required terminal state. Immediate request acceptance is never reported as completed audio startup or shutdown.

FD-10 establishes and tears down the HFP SCO/eSCO audio link. It does **not** implement the later outgoing Audio Gateway PCM-send path to the headset.

## 2. Public and internal contracts

Added or extended:

- `bt_hfp_audio_start()`;
- `bt_hfp_audio_stop()`;
- `bt_hfp_audio_control_init()`;
- `bt_hfp_audio_control_handle_event()`;
- `bt_hfp_audio_control_get_snapshot()`;
- `bt_hfp_audio_control_profile_stopping()`;
- `bt_hfp_audio_control_cleanup_after_stack_shutdown()`;
- `bt_duplex_audio_session_begin()`.

The control path is intentionally separate from the FD-09 fast incoming callback module. The callback remains allocation-free, nonblocking, mutex-free, resampling-free, I2S-driver-free, and free of per-frame logging. FD-10 control operations run outside callback context and may use mutexes, bounded semaphores, BtAppTask dispatch, rollback, and lifecycle telemetry.

## 3. Audio-session generation boundary

The prior session-begin API resets the whole peer/profile state and therefore cannot be reused while HFP SLC remains connected. FD-10 adds a narrow audio-only generation transition.

`bt_duplex_audio_session_begin()`:

- requires the same authoritative peer;
- requires HFP SLC connected;
- requires HFP audio disconnected;
- requires I2S stopped;
- requires requested and effective modes to be enabled;
- rejects faulted or quarantined health;
- allocates a new nonzero generation;
- preserves peer identity, SLC state, A2DP profile/audio state, requested/effective modes, health, last error, diagnostic text, and historical counters.

A new HFP audio start can therefore invalidate stale callback traffic without fabricating an SLC reconnect or erasing unrelated A2DP/session telemetry.

## 4. Start orchestration

`bt_hfp_audio_start()` performs the following ordered transaction:

1. verifies that the FD-09 callback is registered;
2. verifies an enabled duplex mode, authoritative peer, SLC connection, disconnected HFP audio, stopped I2S, and non-faulted health;
3. reserves one serialized FD-10 operation;
4. allocates a new audio-session generation;
5. initializes FD-08 from the validated project/default configuration and runtime pin ownership when needed;
6. starts FD-08 I2S0 and confirms authoritative `I2S_RUNNING`;
7. changes HFP audio to `CONNECTING`;
8. dispatches `esp_hf_ag_audio_connect()` through BtAppTask;
9. waits a bounded time for immediate lower-layer request completion;
10. waits a bounded time for the matching HFP audio-connected event;
11. accepts only confirmed CVSD;
12. binds the FD-09 fast callback gate to the same peer, audio generation, synchronous connection handle, codec, SLC state, and running I2S state;
13. reports completion only after all of those stages succeed.

The focused ordering test proves that the I2S start has completed before the SCO/eSCO connect request reaches the lower layer.

## 5. Start failure and rollback integrity

The start transaction explicitly handles:

- missing SLC or callback registration;
- disabled mode;
- duplicate/transient start;
- I2S initialization or start failure;
- BtAppTask dispatch failure;
- immediate SCO/eSCO request rejection;
- bounded request timeout;
- bounded audio-connected event timeout;
- callback-route activation failure;
- unexpected mSBC confirmation;
- stale, wrong-peer, or late terminal events;
- I2S rollback failure or quarantine.

If I2S startup fails, no SCO/eSCO request is sent. If a lower-layer connect request fails, I2S is cooperatively stopped. If a request times out, FD-10 conservatively treats the lower request as potentially live and issues a bounded disconnect cleanup rather than assuming the request never reached the controller.

Callback-route activation failure closes the callback gate, sets authoritative HFP audio and health to faulted, requests lower disconnect when appropriate, and stops I2S. A rollback cleanup failure takes precedence over the original start error so live or quarantined resources are not hidden behind an earlier cause.

## 6. Stop orchestration

`bt_hfp_audio_stop()`:

1. preserves the authoritative peer and historical counters;
2. immediately closes the FD-09 callback acceptance gate for the old generation;
3. handles both connecting and connected HFP audio states;
4. changes HFP audio to `DISCONNECTING`;
5. dispatches `esp_hf_ag_audio_disconnect()` through BtAppTask;
6. waits bounded time for immediate request completion;
7. waits bounded time for the matching disconnected event;
8. records explicit fault state on disconnect request or event timeout;
9. still attempts cooperative FD-08 I2S shutdown after a Bluetooth-side failure;
10. synchronizes the authoritative I2S state with the actual FD-08 terminal state;
11. returns I2S stop/quarantine failure in preference to a less severe earlier error;
12. reports success only after the audio event and I2S shutdown are both confirmed.

Stopping an already disconnected/stopped session is deterministic and idempotent. A faulted local I2S writer may transition to authoritative `I2S_STOPPED` only after FD-08 proves cleanup; overall health remains faulted until explicit recovery.

## 7. Event ownership and fail-closed behavior

Production HFP audio events are routed to the FD-10 operation tracker before the general HFP event mapper. A tracked operation consumes its event exactly once. Only `ESP_ERR_NOT_FOUND` falls back to the general mapper; a tracked failure is never remapped into success.

FD-10 correlates events using:

- authoritative peer;
- operation serial;
- audio-session generation;
- operation type and state;
- synchronous connection handle for confirmed CVSD activation.

Additional fail-closed behavior:

- wrong-peer events do not complete or disrupt the valid operation;
- late events after timeout/rejection/fault are counted as stale and ignored;
- unsolicited same-peer connected-audio events are marked degraded and explicitly disconnected;
- unsolicited foreign-peer connected-audio events are explicitly disconnected without mutating the active peer state;
- unsolicited connected events never reach the generic mapper and never activate the FD-09 fast route;
- profile teardown closes the FD-09 callback gate even when no start/stop call has initialized the control context.

## 8. Lifecycle and cleanup

FD-10 control resources are allocated only after callback-confirmed HFP profile initialization and successful FD-09 callback registration. Control initialization failure is a visible HFP profile initialization failure and drives manager rollback.

Profile deinitialization:

- closes the FD-09 callback gate;
- faults/releases bounded FD-10 waiters;
- requests lower HFP profile teardown;
- does not free callback/control resources while Bluedroid may still deliver events.

Final cleanup order after Bluedroid is confirmed unable to deliver callbacks is:

1. FD-07 SLC operation state;
2. FD-10 audio-control state;
3. FD-09 incoming-callback state;
4. HFP AG lifecycle resources.

Any incomplete cleanup preserves live state and prevents later stages from being falsely declared clean.

## 9. Telemetry

The FD-10 control snapshot exposes:

- operation type, state, serial, generation, peer, and pending/API-active flags;
- lower-request acceptance;
- immediate, completion, cleanup, and last errors;
- start/stop calls and successful completions;
- start/stop failures;
- dispatch and immediate-request failures;
- request and event timeouts;
- stale and wrong-peer events;
- unexpected connected events;
- rollback attempts and failures;
- cleanup disconnect requests and failures;
- I2S start and stop failures.

No failure path returns success merely because a cleanup attempt was issued.

## 10. Tests

### 10.1 Authoritative duplex-state suite

The ASan/UBSan duplex-state suite contains 16 cases, including:

- audio-generation rotation preserving peer/SLC/A2DP/modes/health/errors/counters;
- rotation rejection while SLC is absent, HFP audio is transient, or I2S is live;
- proven `I2S_FAULTED -> I2S_STOPPED` cleanup while health remains faulted;
- all prior peer, generation, transition, recovery, counter-snapshot, enum, and string contracts.

### 10.2 FD-10 audio-control suite

The dedicated ASan/UBSan suite contains 15 cases covering:

1. SLC and callback-registration preconditions;
2. I2S-before-SCO ordering and callback-confirmed successful start;
3. duplicate start rejection;
4. I2S startup failure preventing any SCO request;
5. immediate SCO connect rejection rolling I2S back;
6. confirmed-CVSD route activation failure faulting state and disconnecting/cleaning up;
7. unsolicited connected-audio rejection without fast-route activation;
8. stop while connecting and while connected;
9. disconnect event timeout faulting state, stopping I2S, and preserving peer identity;
10. I2S stop timeout/quarantine taking error precedence;
11. late connected event after timeout ignored and counted stale;
12. BtAppTask dispatch failure rollback;
13. visible mSBC rejection;
14. wrong-peer event non-completion;
15. profile teardown closing the fast callback gate before control initialization.

### 10.3 HFP AG lifecycle regression

The HFP AG sanitizer suite additionally proves:

- FD-10 control initializes only after profile confirmation and callback registration;
- control initialization failure is visible and faults profile initialization;
- existing profile, callback-registration, deinit, rollback, and cleanup contracts remain intact.

Test dependencies are explicit strongly linked fixtures. Production behavior is not replaced by weak or permissive fallback symbols.

## 11. Validation

Validation for implementation head `7d607afd3d1aedfc5a95061dad434fc36e076d8a`:

- Strict host CI run **906**: PASS.
  - 16-case full-duplex state ASan/UBSan suite;
  - HFP AG lifecycle/event ASan/UBSan suite with FD-10 initialization cases;
  - HFP SLC operation ASan/UBSan suite;
  - manager HFP profile rollback ASan/UBSan suite;
  - HFP PCM ring ASan/UBSan suite;
  - HFP voice conversion ASan/UBSan suite;
  - FD-08 I2S output ASan/UBSan suite;
  - FD-09 incoming audio ASan/UBSan suite and allocation-symbol gate;
  - FD-10 audio-control 15-case ASan/UBSan suite;
  - changed-Python lint gate;
  - Python unit tests;
  - complete CTest suite.
- ESP-IDF v5.5.1 device-build run **798**: PASS.
- Application image: **988,000 bytes**.
- Factory app partition: **1,769,472 bytes**.
- Factory app-partition headroom: **781,472 bytes**.
- Image delta from FD-09: **+6,240 bytes**.
- `.dram0.data`: **21,840 bytes**.
- `.dram0.bss`: **52,032 bytes**.
- Static `.dram0.data + .dram0.bss`: **73,872 bytes**.
- Static DRAM delta from FD-09: **+232 bytes**.
- The FD-10 diff is limited to HFP audio control/state/lifecycle integration, focused tests, CI wiring, and this closeout document.
- No hardware was flashed.

## 12. Hardware-gated work not claimed complete

Software validation does not prove:

- real SCO/eSCO establishment or teardown with the target earbuds;
- actual WROOM32 delivery and timing of connecting/connected/disconnected events;
- real lower-request and event latency relative to the production timeout bounds;
- real synchronous-connection handle behavior;
- real incoming CVSD callback cadence and PCM frame sizes;
- microphone PCM delivery through GPIO27 or I2S clock/timing compatibility;
- coexistence with I2S1 RX, UART2, active A2DP playback, or prolonged earbud soak;
- outgoing AG-to-HF PCM transmission.

These remain explicit later hardware or audio-path phases. No hardware-only checkbox is marked complete from a software build.

## 13. Next phase

FD-11 adds public command handlers and diagnostics:

- `HFP STATUS`;
- `HFP CONNECT <mac>` / `HFP DISCONNECT`;
- `HFP AUDIO START` / `HFP AUDIO STOP`;
- exact mode selection and codec/status reporting;
- stable statistics and explicit safe reset semantics;
- exact accepted-versus-completed operation wording;
- public `bt_manager` API routing with no direct ESP HFP API use in command code.
