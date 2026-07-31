# ESP32 Bluetooth Audio Full-Duplex Specification

**Repository:** `ekkus93/esp32_btaudio`  
**Branch:** `feature/esp-bt-audio-duplex`  
**Target project:** `esp_bt_audio_source/`  
**Target hardware:** ESP32-WROOM-32  
**Target ESP-IDF:** v5.5.1  
**Companion TODO:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`  
**Status:** Authoritative implementation specification

## 1. Purpose

Extend `esp_bt_audio_source` from a one-way Bluetooth A2DP source into a controlled full-duplex Bluetooth audio endpoint for one paired set of earbuds.

The completed firmware must support both directions at the same time:

1. Existing playback audio is sent from the ESP32-WROOM-32 to the earbud speakers.
2. Microphone audio is received from the earbuds through Bluetooth HFP.
3. The received microphone PCM is transmitted from the ESP32-WROOM-32 through a new I2S0 TX interface.

The implementation must preserve the existing A2DP, audio-source, pairing, reconnection, command, and diagnostic behavior whenever HFP audio is inactive.

This work is not complete merely because the firmware builds or a microphone produces samples once. Completion requires correct state ownership, bounded real-time behavior, visible failures, resource measurements, regression testing, and hardware validation.

## 2. Normative language

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative.

- **MUST/MUST NOT:** required for acceptance.
- **SHOULD:** expected unless a documented technical reason is recorded.
- **MAY:** optional.

A warning log is not a substitute for returning an error when an operation fails. A stream of zero samples is not proof that an audio path is healthy.

## 3. Locked decisions

The following decisions are approved and must not be reopened during implementation unless hardware evidence proves them unworkable.

### 3.1 Bluetooth topology

- The ESP32-WROOM-32 remains an **A2DP Source**.
- The ESP32-WROOM-32 additionally becomes an **HFP Audio Gateway (AG)**.
- The earbuds operate as the HFP Hands-Free unit.
- A2DP and HFP must target the same Bluetooth device address.
- Only one remote headset/earbud device is supported at a time.
- Only one SCO/eSCO synchronous audio connection is supported.
- Separate Bluetooth devices for playback and microphone capture are out of scope.

### 3.2 SCO data path

- HFP SCO/eSCO audio MUST use the **HCI application data path**.
- The Bluetooth controller external PCM data path MUST NOT be used for this feature.
- Incoming microphone PCM MUST be visible to application code before being sent to I2S.
- Codec encode/decode ownership must follow the ESP-IDF v5.5.1 HFP HCI API contract.

### 3.3 New I2S microphone-output contract

The new microphone output is fixed as follows:

| Property | Required value |
|---|---|
| Peripheral | I2S0 |
| Direction | TX only |
| ESP32 role | Master |
| Sample rate | 16,000 samples/second |
| Sample format | Signed 16-bit PCM |
| Channels | Mono |
| Framing | Philips I2S |
| MCLK | Not used |
| Clock source | Default I2S clock source, not APLL |
| BCLK | GPIO32 |
| WS/LRCLK | GPIO33 |
| DOUT | GPIO27 |
| DIN | Unused |

The receiving device must be configured as an I2S slave and share ground with the WROOM32.

The firmware MUST keep the I2S0 wire format fixed at 16 kHz regardless of the negotiated HFP codec:

- CVSD 8 kHz PCM is converted to 16 kHz using deterministic 2x sample duplication for the first implementation.
- mSBC 16 kHz PCM is forwarded at its native sample rate.

A later higher-quality CVSD interpolation filter MAY replace sample duplication only if tests prove that it does not compromise deadlines or introduce state bugs.

### 3.4 Existing I2S playback-input contract

The existing I2S1 RX path remains unchanged:

- I2S1 RX master.
- BCLK GPIO18.
- WS GPIO19.
- DIN GPIO22.
- Existing sample rate, slot width, framing, DMA, and APLL behavior remain unchanged.

The new feature MUST NOT reuse or reconfigure I2S1.

### 3.5 Existing playback-source priority

The current source code is authoritative. The full-duplex work MUST preserve the existing behavior:

- Beep has highest-priority overlay/preemption behavior.
- Base-source priority is:
  1. UARTAUDIO
  2. I2S
  3. Synth
  4. Silence

HFP microphone capture is not a playback source and MUST NOT be inserted into this priority list.

## 4. Goals

The implementation MUST provide:

1. HFP AG profile initialization and shutdown under `components/bt_manager`.
2. HFP service-level connection state independent from A2DP state.
3. Explicit SCO/eSCO audio-link start and stop operations.
4. CVSD microphone reception and I2S0 output.
5. mSBC support after CVSD is stable.
6. Full-duplex output to the earbuds while the microphone is active.
7. A bounded microphone PCM buffer and a dedicated I2S writer task.
8. No blocking I2S calls or dynamic memory allocation in Bluetooth audio callbacks.
9. Visible codec, profile, ring-buffer, timing, heap, stack, and error telemetry.
10. Explicit fallback and compatibility behavior when earbuds suspend A2DP during HFP.
11. Host tests, device builds, device tests, and hardware acceptance tests.
12. Clean cooperative shutdown and bounded recovery.

## 5. Non-goals

The first implementation MUST NOT include:

- BLE Audio.
- Multiple active headsets.
- Separate A2DP and HFP peer devices.
- Acoustic echo cancellation.
- Noise suppression.
- Automatic gain control beyond capabilities already provided by the earbuds or ESP-IDF profile.
- Voice recognition.
- Recording microphone audio to flash.
- Mixing the microphone back into its own earbud playback.
- Dynamic 44.1 or 48 kHz microphone I2S output.
- MCLK generation.
- Unbounded automatic retry loops.
- Broad unrelated refactors.
- Changes to `archive/`.

## 6. Required layering

The root and project `CLAUDE.md` rules remain authoritative except where they conflict with current source code; current source code and this approved specification control this feature.

### 6.1 `main/main.c`

`main/main.c` MUST remain a clean bootstrap.

It MUST NOT directly call HFP, A2DP, AVRCP, GAP, Bluedroid, or SCO APIs. HFP initialization must be reached through the public `bt_manager` API.

### 6.2 `components/bt_manager`

`bt_manager` owns:

- HFP AG profile lifecycle.
- HFP events and state.
- SCO/eSCO connection control.
- HFP incoming and outgoing callbacks.
- Peer-address correlation between A2DP and HFP.
- Codec negotiation state.
- Duplex-mode policy.
- Bluetooth-side statistics and faults.

Recommended new files:

```text
components/bt_manager/
    bt_hfp_ag.c
    bt_hfp_audio.c
    bt_duplex_policy.c
    include/bt_hfp_ag.h
    include/bt_hfp_audio.h
    include/bt_duplex_policy.h
```

The implementer MAY choose different names, but HFP logic must not be scattered through unrelated files.

### 6.3 `components/audio_processor`

`audio_processor` owns:

- I2S0 TX channel creation and deletion.
- I2S0 DMA configuration.
- The microphone-output ring buffer.
- The dedicated I2S writer task.
- CVSD 8-to-16 kHz conversion.
- The optional HFP downlink playback tap and voice-rate conversion.
- Audio-side statistics and faults.

Recommended new files:

```text
components/audio_processor/
    hfp_i2s_output.c
    hfp_voice_tap.c
    include/hfp_i2s_output.h
    include/hfp_voice_tap.h
```

### 6.4 `components/command_interface`

The command layer owns parsing, response formatting, and event presentation only. It MUST call public `bt_manager` APIs rather than HFP APIs directly.

### 6.5 Tests and mocks

Production headers, host mocks, and device-test mocks must expose the same public API and state semantics. A permissive mock that accepts invalid production behavior is a test defect.

## 7. Target architecture

### 7.1 Preferred simultaneous mode

```text
Existing playback source
        |
        v
Audio engine / canonical playback PCM
        |
        v
A2DP Source SBC path --------------------> Earbud speakers

Earbud microphone
        |
        v
HFP SCO/eSCO HCI incoming callback
        |
        v
Bounded microphone PCM ring
        |
        v
Dedicated I2S0 writer task
        |
        v
GPIO32 BCLK / GPIO33 WS / GPIO27 DOUT
        |
        v
External I2S slave
```

This mode is preferred when the earbuds allow A2DP playback while the HFP microphone link is active.

### 7.2 HFP compatibility mode

Some Bluetooth Classic earbuds suspend A2DP when SCO/eSCO becomes active. The firmware must support a compatibility path:

```text
Existing playback source
        |
        v
Audio engine canonical playback PCM
        |
        v
Voice downmix/resampler
        |
        v
Bounded HFP downlink ring
        |
        v
HFP outgoing callback -------------------> Earbud speakers

Earbud microphone
        |
        v
HFP incoming callback
        |
        v
I2S0 output
```

Compatibility mode is voice quality:

- CVSD: 8 kHz mono.
- mSBC: 16 kHz mono.

The switch from A2DP playback to HFP downlink MUST be explicit in events and status. It MUST NOT be represented as continued high-quality A2DP playback.

## 8. Duplex operating modes

The firmware must implement an explicit policy enum similar to:

```c
typedef enum {
    BT_DUPLEX_MODE_DISABLED = 0,
    BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC,
    BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
    BT_DUPLEX_MODE_AUTO,
} bt_duplex_mode_t;
```

Required behavior:

- `DISABLED`: existing firmware behavior; HFP audio link is not opened.
- `A2DP_PLUS_HFP_MIC`: require A2DP playback plus HFP microphone. If A2DP becomes unavailable, report failure; do not silently change modes.
- `HFP_FULL_DUPLEX`: use HFP for both microphone uplink and speaker downlink.
- `AUTO`: prefer A2DP plus HFP microphone. If the remote device suspends A2DP, switch to HFP full duplex and emit a reasoned mode-change event.

Default after boot MUST be `DISABLED` unless a later user decision changes it.

HFP service-level connection MAY be established during ordinary connection setup, but the SCO/eSCO audio link MUST NOT open automatically merely because HFP SLC exists. Concretely: HFP microphone audio does not start at boot, on SLC connect, or on A2DP connect/autostart — it starts only in response to an explicit `HFP AUDIO START` command, every session, with no persisted "resume last session" behavior.

## 9. Bluetooth state model

A single `connected` Boolean is insufficient. State must be represented independently.

At minimum, track:

```c
typedef enum {
    BT_HFP_PROFILE_UNINITIALIZED = 0,
    BT_HFP_PROFILE_DISCONNECTED,
    BT_HFP_PROFILE_CONNECTING,
    BT_HFP_PROFILE_SLC_CONNECTED,
    BT_HFP_PROFILE_DISCONNECTING,
    BT_HFP_PROFILE_FAULTED,
} bt_hfp_profile_state_t;

typedef enum {
    BT_HFP_AUDIO_DISCONNECTED = 0,
    BT_HFP_AUDIO_CONNECTING,
    BT_HFP_AUDIO_CONNECTED_CVSD,
    BT_HFP_AUDIO_CONNECTED_MSBC,
    BT_HFP_AUDIO_DISCONNECTING,
    BT_HFP_AUDIO_FAULTED,
} bt_hfp_audio_state_t;
```

The authoritative snapshot must also include:

- ACL peer address.
- A2DP connection state.
- A2DP audio state.
- HFP service-level state.
- HFP audio-link state.
- Negotiated HFP codec.
- Requested duplex mode.
- Effective duplex mode.
- I2S0 output state.
- Last error and error timestamp/counter.
- Generation/session identifier.

All multi-field snapshots MUST be copied under one lock or validated sequence-counter design.

## 10. Peer identity rules

- A2DP and HFP must bind to the same six-byte Bluetooth address.
- A profile event from another address must not mutate the active session.
- A stale event from a previous generation must not alter a newer session.
- Every new connection generation must receive a monotonically increasing session ID.
- Disconnecting one profile must not erase the identity needed to shut down the other profile.
- Reconnection logic must distinguish ACL, A2DP, HFP SLC, and SCO recovery.

## 11. HFP initialization and Kconfig

The implementation must enable the ESP-IDF v5.5.1 HFP AG and HCI audio path.

Expected configuration includes:

```text
CONFIG_BT_HFP_ENABLE=y
CONFIG_BT_HFP_AG_ENABLE=y
CONFIG_BT_HFP_CLIENT_ENABLE=n
CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y
CONFIG_BT_HFP_AUDIO_DATA_PATH_PCM=n
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_PCM=n
```

For initial CVSD bring-up:

```text
CONFIG_BT_HFP_WBS_ENABLE=n
```

For the later mSBC phase:

```text
CONFIG_BT_HFP_WBS_ENABLE=y
```

The implementer MUST verify every exact symbol against the installed ESP-IDF v5.5.1 Kconfig before committing. Generated `sdkconfig` changes must be reviewed; do not assume menuconfig changed only the intended options.

The application uses only the AG role. The HFP client role MUST remain disabled to avoid unused code and ambiguous ownership.

## 12. Codec policy

### 12.1 CVSD phase

CVSD is the first required codec.

- Input PCM rate: 8 kHz.
- Input channels: mono.
- Input sample width: 16-bit signed PCM as delivered by the ESP-IDF HFP HCI API.
- I2S0 conversion: duplicate each sample once to create 16 kHz mono PCM.
- HFP downlink in compatibility mode: downmix playback to mono and resample to 8 kHz.

CVSD hardware validation must pass before mSBC is enabled.

### 12.2 mSBC phase

- Input PCM rate: 16 kHz.
- Input channels: mono.
- Input sample width: 16-bit signed PCM.
- I2S0 conversion: no sample-rate conversion.
- HFP downlink: downmix playback to mono and resample to 16 kHz.

The negotiated codec must be reported truthfully. Failure to negotiate mSBC may fall back to CVSD only when the requested policy permits fallback, and the fallback reason must be emitted.

### 12.3 Codec changes during a session

A codec change must:

1. Increment a codec-change counter.
2. Flush or generation-tag incompatible buffered PCM.
3. Reconfigure only the software converter, not the fixed 16 kHz I2S wire rate.
4. Emit an event with old codec, new codec, and session ID.
5. Never mix 8 kHz and 16 kHz interpretations in one buffer generation.

## 13. Real-time callback requirements

Bluetooth HFP audio callbacks MUST:

- Never block on a mutex with unbounded wait.
- Never call `i2s_channel_write()`.
- Never allocate or free heap memory.
- Never log per audio frame.
- Never perform general-purpose resampling.
- Validate pointers and sizes.
- Record callback duration using low-overhead timing instrumentation.
- Perform only bounded copy, accounting, and notification work.

Incoming callback behavior:

1. Validate profile/session/audio state.
2. Validate PCM length against the active codec contract.
3. Attempt one whole-frame write to the microphone ring.
4. If insufficient space exists, reject the whole frame, increment drop counters, and trigger thresholded fault reporting.
5. Wake the I2S writer task using a task notification or equivalent nonblocking primitive.

Outgoing callback behavior:

1. Read at most the requested amount from the HFP downlink ring.
2. If insufficient audio exists, provide intentional silence or return the API-prescribed no-data result.
3. Increment explicit underrun counters.
4. Emit a thresholded health event rather than logging every callback.

## 14. Buffering and ownership

### 14.1 Microphone ring

- Single producer: HFP incoming callback.
- Single consumer: I2S0 writer task.
- Initial capacity: 4096 bytes, configurable at build time.
- Storage allocated during initialization, not during streaming.
- Writes are all-or-nothing per callback frame.
- The ring MUST NOT overwrite unread data.
- Overflow policy: drop the new frame, count it, and preserve existing queued order.

At 16 kHz mono 16-bit PCM, 4096 bytes represents 128 ms. This is sufficient bring-up headroom without intentionally creating high latency.

### 14.2 HFP downlink ring

- Single producer: audio engine task or a dedicated voice-tap worker.
- Single consumer: HFP outgoing callback.
- Initial capacity: 4096 bytes.
- Data stored at the active HFP codec PCM rate.
- Old-codec data must be flushed on codec transition.

### 14.3 Concurrency

Ring operations may use:

- A proven SPSC lock-free design.
- A very short ESP32 critical section.

They MUST NOT use a blocking FreeRTOS queue operation in a Bluetooth callback.

### 14.4 Buffer health thresholds

Track:

- Current bytes.
- Capacity.
- Peak bytes.
- Total written/read.
- Overflow frames/bytes.
- Underflow callbacks/bytes.
- High-water threshold crossings.
- Time since last successful write/read.

Sustained overflow or underflow must transition audio health to degraded or faulted according to configured thresholds.

## 15. I2S0 output component

The I2S0 output component must expose a narrow API similar to:

```c
typedef enum {
    HFP_I2S_OUT_UNINITIALIZED = 0,
    HFP_I2S_OUT_STOPPED,
    HFP_I2S_OUT_STARTING,
    HFP_I2S_OUT_RUNNING,
    HFP_I2S_OUT_STOPPING,
    HFP_I2S_OUT_FAULTED,
} hfp_i2s_out_state_t;

typedef struct {
    int port;
    int bclk_gpio;
    int ws_gpio;
    int dout_gpio;
    uint32_t sample_rate_hz;
    size_t ring_capacity_bytes;
} hfp_i2s_out_config_t;

esp_err_t hfp_i2s_out_init(const hfp_i2s_out_config_t *cfg);
esp_err_t hfp_i2s_out_start(uint32_t session_id, bool input_is_8khz);
size_t hfp_i2s_out_push(const int16_t *pcm, size_t samples,
                        uint32_t session_id);
esp_err_t hfp_i2s_out_stop(TickType_t timeout);
esp_err_t hfp_i2s_out_deinit(void);
esp_err_t hfp_i2s_out_get_snapshot(hfp_i2s_out_snapshot_t *out);
```

Exact signatures may differ, but ownership and error semantics must remain.

### 15.1 I2S writer task

The writer task must:

- Be the sole caller of blocking I2S writes.
- Use bounded I2S write timeouts.
- Output fixed 16 kHz mono 16-bit Philips I2S.
- Maintain the clock while an HFP audio session is active.
- Insert deliberate zero samples only when necessary to keep the bus clocking.
- Count every inserted silence sample and underrun interval.
- Stop cooperatively.
- Confirm task exit before its handle or buffers are released.

A shutdown timeout must return `ESP_ERR_TIMEOUT` and leave the component faulted/quarantined. The code MUST NOT forget a task that may still be running.

### 15.2 I2S startup rollback

If any channel allocation, mode initialization, task creation, ring allocation, or channel enable step fails:

- Return a specific error.
- Undo every completed earlier step.
- Leave no live channel, task, allocation, or stale `RUNNING` state.

## 16. Playback voice tap for HFP downlink

Compatibility mode requires voice-rate playback without doing heavy work in the HFP outgoing callback.

The audio engine or a dedicated worker must:

1. Observe the same canonical playback PCM used for A2DP.
2. Downmix stereo to mono using saturating arithmetic.
3. Resample from the canonical playback rate to 8 kHz or 16 kHz.
4. Write voice PCM into the HFP downlink ring.
5. Preserve the existing A2DP ring behavior when A2DP remains active.

The implementation MUST avoid processing the same source independently in two unrelated tasks, because that would create timing and source-state divergence.

The voice tap must be disabled when HFP downlink is inactive so normal A2DP CPU cost remains effectively unchanged.

## 17. A2DP and HFP coexistence policy

### 17.1 Preferred behavior

In `A2DP_PLUS_HFP_MIC` or `AUTO` mode:

- Keep A2DP connected and streaming.
- Establish HFP SLC to the same peer.
- Open SCO/eSCO for microphone audio.
- Continue A2DP only if the remote device does so successfully.

### 17.2 Detecting A2DP suspension

The firmware must use actual A2DP profile/audio events and delivery statistics. It MUST NOT infer success solely from the ACL connection.

If A2DP transitions to remote suspend/stopped after SCO opens:

- `A2DP_PLUS_HFP_MIC` mode reports a mode failure and remains explicit.
- `AUTO` mode may activate HFP downlink compatibility mode.
- The transition must include a reason code such as `REMOTE_SUSPENDED_A2DP_DURING_SCO`.

### 17.3 No hidden mode changes

Every effective-mode change must produce:

- A command event.
- Updated `STATUS` output.
- A counter.
- The previous mode.
- The new mode.
- The reason.
- The session ID.

## 18. Public API requirements

`bt_manager.h` should gain typed APIs similar to:

```c
esp_err_t bt_duplex_set_mode(bt_duplex_mode_t mode);
esp_err_t bt_hfp_connect(const char *mac);
esp_err_t bt_hfp_disconnect(void);
esp_err_t bt_hfp_audio_start(void);
esp_err_t bt_hfp_audio_stop(void);
esp_err_t bt_duplex_get_snapshot(bt_duplex_snapshot_t *out);
esp_err_t bt_duplex_reset_stats(void);
```

Requirements:

- New APIs return `esp_err_t`/`bt_err_t`, not ambiguous booleans.
- Invalid state returns `ESP_ERR_INVALID_STATE`.
- Invalid arguments return `ESP_ERR_INVALID_ARG`.
- Allocation failure returns `ESP_ERR_NO_MEM`.
- Bounded shutdown failure returns `ESP_ERR_TIMEOUT`.
- APIs must not return success before a required synchronous setup step has failed.
- Asynchronous accepted operations must expose eventual completion/failure through events and state.

## 19. Command protocol

Required commands:

```text
HFP STATUS
HFP CONNECT <mac>
HFP DISCONNECT
HFP AUDIO START
HFP AUDIO STOP
HFP MODE DISABLED
HFP MODE A2DP_MIC
HFP MODE HFP_FULL
HFP MODE AUTO
HFP CODEC
HFP STATS
HFP RESETSTATS
```

The exact command parser structure may adapt to existing conventions.

Responses must distinguish:

- Command accepted.
- Operation completed.
- Already in requested state.
- Invalid state.
- Profile unavailable.
- Remote rejection.
- Timeout.
- Resource failure.

Required events include:

```text
EVENT|HFP|PROFILE|<state>|<mac>|<session>
EVENT|HFP|AUDIO|<state>|<codec>|<session>
EVENT|HFP|MODE|<old>|<new>|<reason>|<session>
EVENT|HFP|I2S|<state>|<error>|<session>
EVENT|HFP|HEALTH|<severity>|<reason>|<count>|<session>
```

Do not expose raw pointers, unstable enum integers, or fabricated success.

## 20. Statistics and observability

A thread-safe snapshot must expose at least:

### 20.1 Bluetooth/HFP

- Profile connect attempts/successes/failures.
- SCO connect attempts/successes/failures.
- Current and last negotiated codec.
- Codec changes and fallbacks.
- Incoming callbacks, frames, samples, and bytes.
- Incoming invalid frames/bytes.
- Incoming dropped frames/bytes.
- Outgoing callbacks, requested bytes, supplied bytes, and silence bytes.
- Callback duration current/max and histogram or threshold counts.
- Remote suspend events.
- Effective-mode changes.
- Last Bluetooth error.

### 20.2 I2S output

- Ring current/capacity/peak.
- Ring writes/reads.
- Overflow frames/bytes.
- Underrun events/silence samples.
- I2S write calls/bytes/timeouts/errors.
- Task wakeups.
- Current task stack high-water mark.
- State transitions.
- Last error.

### 20.3 System resources

Capture at defined checkpoints:

- Free internal heap.
- Minimum-ever free heap.
- Largest internal free block.
- Relevant task stack high-water marks.
- Firmware image size.
- Application partition free bytes.

Checkpoints:

1. Normal boot.
2. A2DP connected.
3. A2DP streaming.
4. HFP SLC connected.
5. SCO active with CVSD.
6. SCO active with mSBC.
7. Simultaneous playback and microphone stress.
8. After stop/disconnect.

## 21. Failure visibility and health policy

The following are prohibited:

- Quietly dropping microphone frames.
- Quietly switching from A2DP to HFP downlink.
- Reporting microphone active when SCO/eSCO is absent.
- Clearing a fault because a later unrelated callback arrived.
- Returning success after I2S task creation failed.
- Continuing over a writer task that missed its stop deadline.
- Resetting counters during reconnect without an explicit reset operation.
- Infinite reconnect loops.
- Perpetual silence insertion that keeps status green.

Health states should be explicit:

```c
typedef enum {
    BT_AUDIO_HEALTH_OK = 0,
    BT_AUDIO_HEALTH_DEGRADED,
    BT_AUDIO_HEALTH_FAULTED,
    BT_AUDIO_HEALTH_QUARANTINED,
} bt_audio_health_t;
```

Suggested transitions:

- One isolated underflow: count only.
- Repeated underflows over a short window: degraded event.
- Sustained overflow, I2S timeout, callback contract violation, or stop timeout: faulted.
- Unproven worker shutdown or unsafe resource ownership: quarantined until explicit recovery/reinit.

Thresholds must be constants or Kconfig values and covered by pure host tests.

## 22. Lifecycle and shutdown

Required start sequence:

1. Validate requested mode and peer identity.
2. Confirm Bluetooth manager initialized.
3. Confirm HFP profile initialized.
4. Establish or verify HFP SLC.
5. Initialize/start I2S0 output resources.
6. Reset the new session generation and relevant transient rings.
7. Open SCO/eSCO.
8. On confirmed audio connection, register/activate HFP audio callbacks as required by ESP-IDF.
9. Mark microphone active only after the profile event confirms audio connection.

Required stop sequence:

1. Reject duplicate start operations.
2. Stop accepting new PCM into the old session generation.
3. Request SCO/eSCO disconnect.
4. Wait for bounded profile confirmation where applicable.
5. Stop I2S writer cooperatively.
6. Confirm task exit.
7. Disable/delete I2S0 channel.
8. Reset transient rings.
9. Preserve diagnostic counters and last error.
10. Return a specific error if any required step failed.

Every partial-start failure must run rollback in reverse ownership order.

## 23. Memory and performance acceptance gates

### 23.1 Flash

The final application image must fit the configured application partition with at least 256 KiB remaining. Any reduction below that margin requires explicit review before acceptance.

### 23.2 Heap

During simultaneous A2DP, HFP, and I2S operation:

- Minimum-ever free internal heap SHOULD remain at least 32 KiB.
- Largest free internal block SHOULD remain at least 16 KiB.
- Any lower result requires documented fragmentation analysis and user approval.

### 23.3 Task stacks

Every new task and affected existing task must retain the larger of:

- 512 bytes stack margin.
- 20 percent of its configured stack.

Stack sizing must be based on measured high-water marks, not guesses.

### 23.4 Callback timing

HFP callbacks should meet:

- p99 execution below 500 microseconds.
- Maximum execution below 2 milliseconds during the acceptance soak.

Any violation must be investigated and reported.

### 23.5 Audio integrity

After startup stabilization, a 30-minute simultaneous test must show:

- Zero microphone ring overflows.
- Zero sustained I2S underruns.
- Zero I2S write timeouts.
- Zero invalid HFP frame-size events.
- No regression in existing A2DP underrun/drop counters attributable to HFP.
- No task watchdog, brownout, panic, or reboot.

## 24. Testing requirements

### 24.1 Host tests

Host tests must cover pure logic and failure injection for:

- Ring wraparound and all-or-nothing frame writes.
- Overflow and underflow accounting.
- CVSD 8-to-16 kHz duplication.
- Stereo-to-mono saturating downmix.
- Playback-to-8/16 kHz resampler continuity across chunks.
- Codec transitions and generation flushes.
- State-machine legal and illegal transitions.
- Mode-policy transitions.
- Same-peer address enforcement.
- Stale-session event rejection.
- Task-create failure rollback.
- I2S-init failure rollback.
- Stop timeout/quarantine behavior.
- Thresholded health events.
- Command parser and response semantics.
- Snapshot consistency.

### 24.2 Device compilation

After every change to ESP-only code, run a clean or reliable incremental:

```bash
. $HOME/esp/v5.5.1/esp-idf/export.sh
cd esp_bt_audio_source
idf.py build
```

Host tests are not sufficient because they do not compile every `ESP_PLATFORM` path.

### 24.3 Existing regression suites

All existing host and device tests must pass. Existing A2DP-only behavior must be tested with HFP disabled and with HFP compiled but inactive.

### 24.4 Hardware tests

Hardware acceptance requires:

1. CVSD microphone capture from real earbuds.
2. I2S0 BCLK/WS/DOUT verification using logic analyzer or receiving device counters.
3. PCM content verification using a known spoken/tone signal.
4. A2DP playback while HFP microphone is active.
5. Detection of remote A2DP suspension.
6. HFP full-duplex compatibility playback.
7. mSBC negotiation and microphone capture.
8. Repeated start/stop cycles.
9. Disconnect during active audio.
10. Earbud power loss during active audio.
11. I2S consumer absent or stalled.
12. 30-minute simultaneous soak.

At least one earbud model that supports mSBC and one CVSD-only or forced-CVSD run should be recorded.

## 25. Backward compatibility

With HFP audio inactive:

- Existing pairing commands behave the same.
- Existing connect/disconnect behavior remains usable.
- Existing autoconnect remains A2DP-compatible.
- Existing playback-source priority remains unchanged.
- Existing I2S1 capture remains unchanged.
- Existing UARTAUDIO remains unchanged.
- Existing synth and beep behavior remain unchanged.
- Existing diagnostic commands remain valid.
- Existing tests remain green.

HFP compile-time support must not silently cause the existing device to open its microphone on boot.

## 26. Documentation requirements

Implementation must update:

- `esp_bt_audio_source/README.md` with wiring and user commands.
- Relevant architecture documentation.
- Command protocol documentation.
- Kconfig descriptions.
- Hardware test checklist.
- `memory.md` after each meaningful phase using the repository rules.

Every file referenced by the spec or TODO must exist in the repository at the named path before handoff. Do not reference an assistant-generated review or response document that is not committed.

## 27. Completion criteria

The feature is complete only when all of the following are true:

1. CVSD and mSBC microphone PCM reach I2S0 using the fixed wire contract.
2. Earbud playback continues through A2DP where supported.
3. HFP full-duplex compatibility playback works when A2DP is suspended.
4. Mode changes and fallbacks are explicit and observable.
5. No Bluetooth callback blocks, allocates, performs I2S writes, or performs heavy resampling.
6. Buffer overflow, underflow, silence insertion, and invalid data are counted and threshold-reported.
7. Start, stop, partial failure, disconnect, and stale-event paths are safe.
8. Resource gates are measured and pass or receive explicit user approval.
9. Existing host/device tests pass.
10. New host/device tests pass.
11. Hardware validation and the 30-minute soak pass.
12. Documentation and `memory.md` are current.
13. No hardware flash is performed without explicit user authorization.
