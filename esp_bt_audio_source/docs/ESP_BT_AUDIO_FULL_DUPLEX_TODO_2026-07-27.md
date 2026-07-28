# ESP32 Bluetooth Audio Full-Duplex TODO

**Companion specification:** `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`  
**Implementation branch:** `feature/esp-bt-audio-duplex`  
**Target:** ESP32-WROOM-32, ESP-IDF v5.5.1  
**Primary project:** `esp_bt_audio_source/`  
**Rule:** Implement one task at a time. Do not combine unrelated refactors.

## 0. Instructions for Claude Code or another implementation agent

Before editing:

- [ ] Read root `CLAUDE.md` completely.
- [ ] Read `esp_bt_audio_source/CLAUDE.md` completely.
- [ ] Read the companion specification completely.
- [ ] Search recent `memory.md` entries for `HFP`, `A2DP`, `I2S`, `WROOM32`, `UARTAUDIO`, and `audio engine`.
- [ ] Do not read `memory.md` as if every old statement is current; reconcile it with source code.
- [ ] Do not inspect or modify `archive/`.
- [ ] Keep `main/main.c` as a clean bootstrap.
- [ ] Put Bluetooth profile logic in `components/bt_manager`.
- [ ] Put I2S0 TX and audio conversion logic in `components/audio_processor`.
- [ ] Do not flash `/dev/ttyUSB0` without explicit user confirmation.
- [ ] Use TDD or failure injection before each behavioral fix.
- [ ] Run `idf.py build` after changing any `ESP_PLATFORM` code.
- [ ] Do not mark a task complete because code compiles.
- [ ] Do not add quiet fallback, silent data loss, fake success, or unbounded retries.
- [ ] Update `memory.md` after every meaningful completed phase using an actual UTC timestamp.
- [ ] Ensure every assistant-created document referenced in a handoff exists at the exact repository path.

Recommended commit pattern:

```text
docs(duplex): add implementation baseline measurements
feat(hfp): FD-03 add HFP AG profile lifecycle
feat(audio): FD-05 add bounded HFP microphone ring
feat(i2s): FD-06 add I2S0 microphone output
feat(duplex): FD-09 add explicit A2DP/HFP policy
feat(hfp): FD-11 add mSBC support
fix(duplex): FD-12 harden failure and shutdown paths
test(duplex): FD-13 complete hardware/resource acceptance
```

Do not use one large commit for the entire implementation.

---

# Phase 0 — Baseline, branch integrity, and resource measurements

## FD-00 — Confirm branch and clean scope [P0]

### Required work

- [x] Confirm the current branch is `feature/esp-bt-audio-duplex`.
- [x] Record its base commit and current head in the implementation notes.
- [x] Confirm no unrelated local changes will be included.
- [x] Confirm both documentation files exist:
  - [x] `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_SPEC_2026-07-27.md`
  - [x] `esp_bt_audio_source/docs/ESP_BT_AUDIO_FULL_DUPLEX_TODO_2026-07-27.md`
- [x] Run the existing main-layering check.
- [x] Do not touch `esp_i2s_source` during this feature unless a separately approved integration task is created.

### Acceptance

- [x] Branch and baseline are written into `memory.md`.
- [x] No unrelated source or generated files are staged.

## FD-01 — Capture the pre-HFP build and runtime baseline [P0]

### Files/results

- `esp_bt_audio_source/sdkconfig`
- `esp_bt_audio_source/partitions.csv`
- build output from `idf.py size` and `idf.py size-components`
- runtime status and memory logs from existing firmware

### Required work

- [x] Build the unmodified branch using ESP-IDF v5.5.1.
- [x] Record application binary size.
- [x] Record application partition size and free bytes.
- [ ] Record component-level flash/DRAM contributions.
- [ ] Record free internal heap after boot.
- [ ] Record minimum-ever free heap after normal A2DP streaming.
- [ ] Record largest internal free block.
- [ ] Record stack high-water marks for existing Bluetooth and audio tasks.
- [ ] Record A2DP underrun/drop counters during a 10-minute existing-path test.
- [x] Save measurements in `memory.md`; do not commit raw scratch logs unless intentionally converted into a maintained test artifact.

### Commands

```bash
. $HOME/esp/v5.5.1/esp-idf/export.sh
cd esp_bt_audio_source
idf.py build
idf.py size
idf.py size-components
```

### Acceptance

- [x] Existing host suite passes.
- [x] Existing device application builds.
- [ ] Baseline numbers are available for before/after comparison.

---

# Phase 1 — Kconfig and link-time feasibility

## FD-02 — Enable HFP AG/HCI configuration without opening audio [P0]

### Files

- `esp_bt_audio_source/sdkconfig.defaults`
- `esp_bt_audio_source/sdkconfig`
- relevant test `sdkconfig.defaults` files
- possibly `esp_bt_audio_source/Kconfig.projbuild`

### Required work

- [x] Verify exact ESP-IDF v5.5.1 symbols in the installed source tree.
- [x] Enable HFP.
- [x] Enable Audio Gateway role.
- [x] Disable Hands-Free client role.
- [x] Select HCI SCO data path.
- [x] Set maximum synchronous BR/EDR connections to one.
- [x] Start with WBS/mSBC disabled.
- [x] Keep BLE disabled and preserve existing BLE-memory release behavior.
- [x] Review the entire generated Bluetooth Kconfig diff.
- [x] Do not initialize HFP APIs yet in this task.

Expected intent:

```text
CONFIG_BT_HFP_ENABLE=y
CONFIG_BT_HFP_AG_ENABLE=y
CONFIG_BT_HFP_CLIENT_ENABLE=n
CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI=y
CONFIG_BT_HFP_AUDIO_DATA_PATH_PCM=n
CONFIG_BT_HFP_WBS_ENABLE=n
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_HCI=y
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_PCM=n
```

### Tests

- [x] Clean `idf.py build` passes.
- [x] Existing host tests pass after mock/config updates.
- [x] Existing A2DP symbols remain enabled.
- [x] No HFP audio is opened at runtime.

### Measurements

- [x] Record binary-size increase.
- [x] Record static DRAM increase.
- [x] Confirm at least 256 KiB app-partition headroom remains.

### Stop condition

If the link-time configuration alone violates the flash margin or prevents existing A2DP from building, stop and document the exact component-size delta before continuing.

---

# Phase 2 — Pure data structures, converters, and state logic

## FD-03 — Add authoritative duplex types and state snapshot [P0]

### Files

- `components/bt_manager/include/bt_duplex_state.h`
- `components/bt_manager/include/bt_duplex_state_internal.h`
- `components/bt_manager/bt_duplex_state_core.c`
- `components/bt_manager/bt_duplex_state_profile.c`
- `components/bt_manager/bt_duplex_state_transitions.c`
- `components/bt_manager/bt_duplex_state_strings.c`
- `components/bt_manager/include/bt_manager.h`
- host tests

### Required types

Implement typed states equivalent to:

```c
typedef enum {
    BT_DUPLEX_MODE_DISABLED = 0,
    BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC,
    BT_DUPLEX_MODE_HFP_FULL_DUPLEX,
    BT_DUPLEX_MODE_AUTO,
} bt_duplex_mode_t;

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

typedef enum {
    BT_AUDIO_HEALTH_OK = 0,
    BT_AUDIO_HEALTH_DEGRADED,
    BT_AUDIO_HEALTH_FAULTED,
    BT_AUDIO_HEALTH_QUARANTINED,
} bt_audio_health_t;
```

Create one thread-safe snapshot containing:

- [x] Peer address and validity flag.
- [x] Session generation ID.
- [x] Requested duplex mode.
- [x] Effective duplex mode.
- [x] A2DP connection/audio state.
- [x] HFP profile state.
- [x] HFP audio state.
- [x] Negotiated codec.
- [x] I2S output state.
- [x] Health state.
- [x] Last error.
- [x] Counters required by the spec.

### Synchronization

- [x] Use one documented lock or validated sequence counter for snapshot copies.
- [x] Protect all 64-bit counters on the 32-bit ESP32.
- [x] Never invoke external callbacks while holding the state lock.
- [x] Do not use `volatile` as synchronization.

### Host tests

- [x] Initial state is deterministic.
- [x] Legal transitions succeed.
- [x] Illegal transitions return `ESP_ERR_INVALID_STATE`.
- [x] Same-peer enforcement works.
- [x] Stale generation events are ignored and counted.
- [x] Snapshot fields cannot be observed partially updated.
- [x] Mode strings and event strings are stable and exhaustive.

### Validation

- [x] Focused state suite: 13 tests pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- [x] Strict host CI run 573 passes.
- [x] ESP-IDF v5.5.1 device-build run 474 passes.
- [x] No hardware was flashed.

## FD-04 — Add reusable bounded SPSC PCM ring [P0]

### Placement

Prefer a new audio-owned pure module if the existing playback ring cannot be reused without violating ownership:

```text
components/audio_processor/hfp_pcm_ring.c
components/audio_processor/include/hfp_pcm_ring.h
```

### Required semantics

- [x] Fixed-capacity storage.
- [x] No allocation after initialization.
- [x] Single producer and single consumer.
- [x] Whole-frame all-or-nothing write API.
- [x] No overwrite of unread bytes.
- [x] Nonblocking read/write.
- [x] Wraparound-safe copies.
- [x] Current, capacity, peak, total read/write, overflow, and underflow counters.
- [x] Explicit reset that requires the caller to prove producers/consumers are stopped or generation-isolated.

Suggested API:

```c
typedef struct hfp_pcm_ring hfp_pcm_ring_t;

esp_err_t hfp_pcm_ring_init(hfp_pcm_ring_t *ring,
                            uint8_t *storage,
                            size_t capacity);

bool hfp_pcm_ring_write_frame(hfp_pcm_ring_t *ring,
                              const void *src,
                              size_t frame_bytes);

size_t hfp_pcm_ring_read(hfp_pcm_ring_t *ring,
                         void *dst,
                         size_t requested_bytes);

void hfp_pcm_ring_reset(hfp_pcm_ring_t *ring);
```

Reference all-or-nothing pattern:

```c
bool hfp_pcm_ring_write_frame(hfp_pcm_ring_t *rb,
                              const void *src,
                              size_t len)
{
    if (!rb || !src || len == 0 || len > rb->capacity) {
        return false;
    }

    portENTER_CRITICAL(&rb->mux);
    if ((rb->capacity - rb->used) < len) {
        rb->overflow_frames++;
        rb->overflow_bytes += len;
        portEXIT_CRITICAL(&rb->mux);
        return false;
    }

    size_t first = MIN(len, rb->capacity - rb->head);
    memcpy(rb->storage + rb->head, src, first);
    memcpy(rb->storage, (const uint8_t *)src + first, len - first);
    rb->head = (rb->head + len) % rb->capacity;
    rb->used += len;
    rb->total_written += len;
    if (rb->used > rb->peak_used) rb->peak_used = rb->used;
    portEXIT_CRITICAL(&rb->mux);
    return true;
}
```

Adapt counter protection for 64-bit correctness; do not copy this blindly if it creates long critical sections.

### Host tests

- [x] Initialize/invalid arguments.
- [x] Simple write/read.
- [x] Exact-full state.
- [x] Wraparound write/read.
- [x] Whole-frame rejection when insufficient space exists.
- [x] No partial frame is visible after rejection.
- [x] Peak and totals are correct.
- [x] Reset behavior is correct.
- [x] Producer/consumer stress test.
- [x] Sanitizer/Valgrind clean where supported.

### Validation

- [x] Focused ring suite: 8 tests pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- [x] Includes a 50,000-frame producer/consumer order-preservation stress test.
- [x] Strict host CI run 605 passes.
- [x] ESP-IDF v5.5.1 device-build run 505 passes.
- [x] No hardware was flashed.

## FD-05 — Add voice PCM conversion helpers [P0]

### Files

```text
components/audio_processor/hfp_voice_convert.c
components/audio_processor/include/hfp_voice_convert.h
```

### Required helpers

- [x] CVSD PCM 8 kHz to I2S PCM 16 kHz using sample duplication.
- [x] Stereo signed PCM to mono using saturating arithmetic.
- [x] Stateful canonical-rate to 8 kHz conversion.
- [x] Stateful canonical-rate to 16 kHz conversion.
- [x] Chunk-boundary continuity.
- [x] Explicit reset on session/codec generation changes.

Reference CVSD upsample helper:

```c
size_t hfp_cvsd_8k_to_16k(const int16_t *src,
                          size_t src_samples,
                          int16_t *dst,
                          size_t dst_capacity_samples)
{
    if (!src || !dst || dst_capacity_samples < src_samples * 2U) {
        return 0;
    }

    for (size_t i = 0; i < src_samples; ++i) {
        dst[2U * i] = src[i];
        dst[2U * i + 1U] = src[i];
    }
    return src_samples * 2U;
}
```

Reference saturating downmix intent:

```c
static int16_t stereo_to_mono(int16_t left, int16_t right)
{
    int32_t sum = (int32_t)left + (int32_t)right;
    return (int16_t)(sum / 2);
}
```

If source samples are wider than 16 bits, perform explicit scaling and saturation before narrowing.

### Host tests

- [x] Empty/invalid input.
- [x] Exact sample duplication.
- [x] Positive/negative extremes.
- [x] Stereo cancellation and saturation cases.
- [x] Resampler phase continuity over arbitrary chunk boundaries.
- [x] Output count never exceeds capacity.
- [x] Reset removes previous-session phase.

### Validation

- [x] Focused conversion suite: 11 tests pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- [x] Strict host CI run 621 passes.
- [x] ESP-IDF v5.5.1 device-build run 520 passes.
- [x] No hardware was flashed.

---

# Phase 3 — HFP AG profile lifecycle without SCO data flow

## FD-06 — Add HFP AG profile module [P0]

### Files

```text
components/bt_manager/bt_hfp_ag_lifecycle.c
components/bt_manager/bt_hfp_ag_events.c
components/bt_manager/include/bt_hfp_ag.h
components/bt_manager/include/bt_hfp_ag_internal.h
components/bt_manager/CMakeLists.txt
components/bt_manager/bt_manager.c
```

### Required work

- [x] Initialize HFP AG after Bluedroid is enabled and existing Bluetooth core initialization is ready.
- [x] Register the HFP AG event callback.
- [x] Deinitialize in reverse order.
- [x] Propagate initialization failure to `bt_manager_init()`.
- [x] Add complete rollback when HFP initialization fails after A2DP setup.
- [x] Do not report Bluetooth manager ready if required HFP initialization failed while full-duplex support is enabled.
- [x] Keep HFP audio disconnected by default.
- [x] Correlate profile events with the active peer address and generation.

Conceptual initialization sequence; verify exact ESP-IDF v5.5.1 signatures:

```c
esp_err_t bt_hfp_ag_profile_init(void)
{
    esp_err_t err = esp_hf_ag_register_callback(bt_hfp_ag_event_cb);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_hf_ag_init();
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}
```

### HFP event handling

Handle and test at least:

- [x] Profile init/deinit complete.
- [x] Service-level connection state.
- [x] Audio-link state.
- [x] Codec negotiation/WBS events.
- [x] Volume events.
- [x] Unknown AT command events without crashing.
- [x] Remote disconnect.
- [x] Events for the wrong address.
- Operation-generation rejection is implemented in FD-07, where connect/disconnect requests own the generation token; the ESP-IDF HFP callback itself carries only peer and state.

### Host/device tests

- [x] Callback registration failure rolls back.
- [x] HFP init failure rolls back.
- [x] Repeated init returns deterministic state/error.
- [x] Deinit while disconnected succeeds.
- [x] Deinit failure remains visible.
- [x] Wrong-peer events are ignored and counted.
- [x] Existing A2DP tests remain green.

### Validation

- [x] HFP AG lifecycle/event suite passes under AddressSanitizer and UndefinedBehaviorSanitizer.
- [x] Manager profile-init/rollback suite passes under AddressSanitizer and UndefinedBehaviorSanitizer.
- [x] Strict host CI run 679 passes, including changed-Python lint, Python unit tests, and full CTest.
- [x] ESP-IDF v5.5.1 device-build run 574 passes.
- [x] HFP audio remains disconnected by default; no SCO data callbacks are registered in this phase.
- [x] No hardware was flashed. On-device profile-event confirmation remains hardware-gated.

## FD-07 — Add HFP connect/disconnect public APIs [P0]

### Public API

Add typed functions similar to:

```c
esp_err_t bt_hfp_connect(const char *mac);
esp_err_t bt_hfp_disconnect(void);
esp_err_t bt_duplex_get_snapshot(bt_duplex_snapshot_t *out);
```

### Required work

- [ ] Parse and validate the Bluetooth address using existing helpers.
- [ ] Require the HFP peer to match the active A2DP/ACL peer.
- [ ] Reject a second remote peer.
- [ ] Use the existing BtAppTask/event-dispatch ownership model where required.
- [ ] Do not call profile APIs from arbitrary command task context if the existing architecture requires dispatch.
- [ ] Separate accepted operation from confirmed completion.
- [ ] Bind each accepted connect/disconnect operation to the active session generation.
- [ ] Reject and count completion/events associated with a stale operation generation.
- [ ] Add bounded connect/disconnect watchdogs.
- [ ] Do not start SCO in this task.

### Tests

- [ ] Invalid MAC.
- [ ] Manager not initialized.
- [ ] Same peer accepted.
- [ ] Different peer rejected.
- [ ] Already connected is idempotent or specifically reported.
- [ ] Connect API immediate failure is returned.
- [ ] Remote rejection is delivered asynchronously as failure event.
- [ ] Late same-peer completion/event for an old generation is ignored and counted.
- [ ] Timeout does not fabricate disconnected state.

---

# Phase 4 — I2S0 microphone output and CVSD data flow

## FD-08 — Add I2S0 TX output component [P0]

### Files

```text
components/audio_processor/hfp_i2s_output.c
components/audio_processor/include/hfp_i2s_output.h
components/audio_processor/CMakeLists.txt
components/audio_processor/Kconfig.projbuild
```

### Kconfig defaults

```text
CONFIG_HFP_I2S_PORT=0
CONFIG_HFP_I2S_BCLK_GPIO=32
CONFIG_HFP_I2S_WS_GPIO=33
CONFIG_HFP_I2S_DOUT_GPIO=27
CONFIG_HFP_I2S_SAMPLE_RATE=16000
CONFIG_HFP_I2S_RING_BYTES=4096
```

- [ ] Validate port and pins at startup.
- [ ] Reject GPIO conflicts with existing I2S1 and UART2 defaults.
- [ ] Reject input-only or flash-connected pins.
- [ ] Warn or reject boot-strapping pins according to explicit policy.
- [ ] Do not silently replace invalid configured pins with defaults.

### I2S configuration intent

```c
i2s_chan_config_t chan_cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

chan_cfg.dma_desc_num = CONFIG_HFP_I2S_DMA_DESC_NUM;
chan_cfg.dma_frame_num = CONFIG_HFP_I2S_DMA_FRAME_NUM;
chan_cfg.auto_clear = true;

i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = GPIO_NUM_32,
        .ws = GPIO_NUM_33,
        .dout = GPIO_NUM_27,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};
```

Verify mono slot mask and wire behavior using the installed driver and logic analyzer. Do not assume the receiver interprets mono exactly as the host tests do.

### Lifecycle

- [ ] Allocate ring and conversion scratch storage during init/start.
- [ ] Create one writer task.
- [ ] Enable I2S only after complete initialization.
- [ ] Roll back channel/task/buffer failures completely.
- [ ] Stop writer cooperatively.
- [ ] Wait for explicit stopped confirmation.
- [ ] On stop timeout, return `ESP_ERR_TIMEOUT` and quarantine the component.
- [ ] Never externally delete a live task as a normal shutdown shortcut.

### Writer loop pattern

```c
for (;;) {
    if (stop_requested()) {
        break;
    }

    size_t samples = hfp_pcm_ring_read(&s_ring,
                                       local_pcm,
                                       ARRAY_SIZE(local_pcm) * sizeof(local_pcm[0]))
                     / sizeof(local_pcm[0]);

    if (samples == 0) {
        memset(local_pcm, 0, sizeof(local_pcm));
        samples = ARRAY_SIZE(local_pcm);
        stats_record_silence(samples);
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx,
                                      local_pcm,
                                      samples * sizeof(local_pcm[0]),
                                      &bytes_written,
                                      pdMS_TO_TICKS(HFP_I2S_WRITE_TIMEOUT_MS));
    if (err != ESP_OK || bytes_written != samples * sizeof(local_pcm[0])) {
        record_i2s_failure(err, bytes_written);
        if (failure_threshold_reached()) {
            enter_faulted_state(err);
            break;
        }
    }
}
```

This is an ownership pattern, not permission to hide sustained underflow with silence. Every inserted silence interval must be counted and threshold-reported.

### Host tests

- [ ] Invalid config.
- [ ] Ring allocation failure.
- [ ] Channel allocation failure.
- [ ] Mode-init failure.
- [ ] Task-create failure.
- [ ] Channel-enable failure.
- [ ] Full rollback after each injected failure.
- [ ] Start/stop idempotence.
- [ ] Stop timeout -> quarantined.
- [ ] Session generation rejects stale pushes.
- [ ] CVSD data is duplicated to 16 kHz correctly.
- [ ] No partial callback frame is accepted.

### Device build

- [ ] Run `idf.py build` immediately after ESP driver code is added.

## FD-09 — Add HFP HCI incoming callback and CVSD routing [P0]

### Files

```text
components/bt_manager/bt_hfp_audio.c
components/bt_manager/include/bt_hfp_audio.h
components/bt_manager/bt_hfp_ag.c
```

### Required work

- [ ] Register HFP incoming/outgoing audio callbacks only in valid profile/audio states.
- [ ] Incoming callback validates pointer, size, peer/session, and active codec.
- [ ] Incoming callback performs no allocation.
- [ ] Incoming callback performs no blocking wait.
- [ ] Incoming callback performs no I2S calls.
- [ ] Incoming callback copies one whole PCM frame into the I2S output ring.
- [ ] Ring-full rejection increments frame and byte counters.
- [ ] Callback timing is measured without per-frame logs.
- [ ] CVSD is the only accepted codec in this phase.
- [ ] Unexpected mSBC state is rejected visibly until Phase 8.

Reference pattern:

```c
static void hfp_incoming_cb(const uint8_t *data, uint32_t bytes)
{
    int64_t start_us = esp_timer_get_time();

    if (!data || bytes == 0 || (bytes % sizeof(int16_t)) != 0) {
        duplex_stats_record_invalid_incoming(bytes);
        return;
    }

    bt_duplex_fast_state_t fast = bt_duplex_fast_state_snapshot();
    if (!fast.audio_active || fast.codec != BT_HFP_CODEC_CVSD) {
        duplex_stats_record_unexpected_incoming(bytes);
        return;
    }

    bool accepted = hfp_i2s_out_push_cvsd(
        (const int16_t *)data,
        bytes / sizeof(int16_t),
        fast.session_id);

    if (!accepted) {
        duplex_stats_record_incoming_drop(bytes);
    }

    duplex_stats_record_callback_time(esp_timer_get_time() - start_us);
}
```

Make alignment-safe copies if the ESP-IDF callback does not guarantee `int16_t` alignment.

### Tests

- [ ] Null/zero/odd-byte input.
- [ ] Inactive session.
- [ ] Wrong codec.
- [ ] Stale session.
- [ ] Ring full.
- [ ] Valid frame accepted.
- [ ] No allocation function is reachable from callback path.
- [ ] Callback maximum-work bounds are testable.

## FD-10 — Add explicit HFP audio start/stop [P0]

### Public API

```c
esp_err_t bt_hfp_audio_start(void);
esp_err_t bt_hfp_audio_stop(void);
```

### Start requirements

- [ ] Duplex mode is not disabled.
- [ ] HFP SLC is connected to the active peer.
- [ ] No old audio generation is starting/stopping/running.
- [ ] I2S0 output starts before requesting SCO/eSCO.
- [ ] A new session generation is allocated.
- [ ] Transient rings/converters reset for that generation.
- [ ] SCO connect request immediate errors trigger I2S rollback.
- [ ] `AUDIO START` is not reported complete until the HFP audio-connected event arrives.

### Stop requirements

- [ ] Reject new callback data for the old generation.
- [ ] Request SCO/eSCO disconnect.
- [ ] Wait bounded time for profile event or transition to explicit fault.
- [ ] Stop I2S0 writer cooperatively.
- [ ] Preserve counters and last error.
- [ ] Do not clear peer identity prematurely.

### Tests

- [ ] Start without SLC.
- [ ] Duplicate start.
- [ ] I2S start failure prevents SCO request.
- [ ] SCO request immediate failure rolls back I2S.
- [ ] Confirmed audio-connected marks active.
- [ ] Stop while connecting.
- [ ] Stop while connected.
- [ ] SCO disconnect timeout.
- [ ] I2S stop timeout.
- [ ] Late old-session audio-connected event ignored.

---

# Phase 5 — Commands, events, and diagnostics

## FD-11 — Add HFP command handlers [P1]

### Files

- `components/command_interface/commands.c` or current command table
- new or existing HFP command handler file
- command mocks/tests

### Required commands

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

### Required work

- [ ] Follow existing response conventions.
- [ ] Route all Bluetooth work through public `bt_manager` APIs.
- [ ] No direct ESP HFP API includes in command component.
- [ ] Distinguish accepted from completed asynchronous operations.
- [ ] Return exact errors.
- [ ] Reject unknown mode strings.
- [ ] Make `HFP STATUS` a consistent single snapshot.
- [ ] Make `HFP RESETSTATS` explicit and safe while streaming.

### Host tests

- [ ] Every command valid form.
- [ ] Every command invalid form.
- [ ] Exact mode parsing.
- [ ] Exact error mapping.
- [ ] Async accepted response does not claim connected.
- [ ] Status snapshot cannot mix generations.

## FD-12 — Add HFP event contract [P1]

### Events

```text
EVENT|HFP|PROFILE|<state>|<mac>|<session>
EVENT|HFP|AUDIO|<state>|<codec>|<session>
EVENT|HFP|MODE|<old>|<new>|<reason>|<session>
EVENT|HFP|I2S|<state>|<error>|<session>
EVENT|HFP|HEALTH|<severity>|<reason>|<count>|<session>
```

### Required work

- [ ] Emit only on meaningful state transitions or threshold crossings.
- [ ] Do not log one event per PCM frame.
- [ ] Escape/sanitize string fields according to command protocol rules.
- [ ] Include stable reason strings, not raw enum integers.
- [ ] Include session generation.
- [ ] Avoid duplicate transition events.
- [ ] Test UART0 and UART2 event delivery according to existing broadcast rules.

## FD-13 — Add memory, timing, and stack diagnostics [P1]

### Required work

- [ ] Capture free internal heap.
- [ ] Capture minimum-ever free heap.
- [ ] Capture largest internal free block.
- [ ] Capture HFP/I2S task stack high-water marks.
- [ ] Capture callback current/max and threshold counts.
- [ ] Capture firmware size in validation notes.
- [ ] Add diagnostics to `HFP STATS` or a stable status command.
- [ ] Do not reset minimum-ever heap or historical counters implicitly.

Reference snapshot helper:

```c
snapshot.free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
snapshot.largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
snapshot.minimum_free = esp_get_minimum_free_heap_size();
```

---

# Phase 6 — CVSD hardware bring-up

## FD-14 — Validate GPIO32/33/27 I2S0 output [P0 hardware gate]

### Prerequisites

- [ ] Obtain explicit user approval before flashing.
- [ ] Connect common ground.
- [ ] Connect GPIO32 BCLK.
- [ ] Connect GPIO33 WS/LRCLK.
- [ ] Connect GPIO27 DOUT.
- [ ] Configure receiver as 16 kHz, 16-bit mono Philips I2S slave.

### Tests

- [ ] Verify BCLK frequency and duty cycle.
- [ ] Verify WS/LRCLK is 16 kHz.
- [ ] Verify DOUT changes with microphone input.
- [ ] Confirm no output appears on the existing I2S1 pins due to accidental reconfiguration.
- [ ] Confirm I2S0 uses default clock source and does not disturb I2S1 APLL.
- [ ] Capture received PCM and verify intelligible microphone audio.
- [ ] Verify CVSD 8-to-16 kHz duplication in captured samples.
- [ ] Test 20 start/stop cycles.
- [ ] Test receiver disconnected.
- [ ] Test receiver stalled if possible.

### Acceptance

- [ ] No I2S write timeouts in normal operation.
- [ ] No microphone ring overflow after stabilization.
- [ ] Underflow/silence insertion is visible in counters.
- [ ] Stop always confirms writer exit.

## FD-15 — Validate real HFP SLC and SCO with earbuds [P0 hardware gate]

### Tests

- [ ] Pair earbuds using existing pairing flow.
- [ ] Establish A2DP connection.
- [ ] Establish HFP SLC to same MAC.
- [ ] Confirm profile status is independent.
- [ ] Start CVSD HFP audio explicitly.
- [ ] Confirm negotiated codec reports CVSD.
- [ ] Speak into microphone and verify I2S receiver PCM.
- [ ] Stop HFP audio without disconnecting A2DP.
- [ ] Repeat after earbud power cycle.
- [ ] Disconnect during active SCO.
- [ ] Reject events from a different paired device.

### Measurements

- [ ] Binary size.
- [ ] Free/minimum/largest heap.
- [ ] Callback max duration.
- [ ] New task stack high-water marks.
- [ ] Microphone frames/bytes/drops.
- [ ] I2S underflows/timeouts.
- [ ] Existing A2DP counters.

---

# Phase 7 — A2DP plus HFP microphone coexistence

## FD-16 — Add explicit duplex policy engine [P0]

### Files

- `components/bt_manager/bt_duplex_policy.c`
- `components/bt_manager/bt_events_a2dp.c`
- `components/bt_manager/bt_hfp_ag.c`

### Required work

- [ ] `DISABLED` preserves existing behavior.
- [ ] `A2DP_MIC` requires A2DP playback and HFP microphone.
- [ ] `HFP_FULL` reserves HFP for both directions.
- [ ] `AUTO` begins in A2DP plus HFP microphone mode.
- [ ] A2DP remote suspend/stopped after SCO start is processed as an explicit policy event.
- [ ] `A2DP_MIC` mode reports incompatibility rather than changing mode.
- [ ] `AUTO` may request HFP compatibility downlink and emits a mode-change event.
- [ ] Mode transitions are serialized.
- [ ] No two modes own the same downlink ring simultaneously.

Suggested pure policy API:

```c
typedef struct {
    bt_duplex_mode_t requested;
    bt_duplex_mode_t effective;
    bool a2dp_connected;
    bool a2dp_streaming;
    bool hfp_slc_connected;
    bool sco_connected;
    bool remote_suspended_a2dp;
} bt_duplex_policy_input_t;

bt_duplex_policy_result_t
bt_duplex_policy_evaluate(const bt_duplex_policy_input_t *in);
```

Keep policy evaluation pure so the state matrix is fully host-testable.

### Host tests

- [ ] Full state matrix.
- [ ] A2DP remains active.
- [ ] Remote suspend in strict A2DP mode.
- [ ] Remote suspend in AUTO mode.
- [ ] Recovery when SCO stops.
- [ ] Rapid event ordering permutations.
- [ ] Stale A2DP event ignored by generation.
- [ ] No duplicate mode-change events.

## FD-17 — Hardware-test simultaneous A2DP playback and CVSD mic [P0 hardware gate]

### Tests

- [ ] Start known continuous playback source.
- [ ] Verify A2DP audio is audible before SCO.
- [ ] Start HFP microphone.
- [ ] Observe whether earbuds keep A2DP active.
- [ ] Record exact A2DP profile events.
- [ ] Verify microphone PCM continues on I2S0.
- [ ] Verify existing I2S1 input remains stable.
- [ ] Verify A2DP underrun/drop counters do not regress.
- [ ] Repeat using each intended earbud model.

### Acceptance

- [ ] If earbuds support simultaneous A2DP + HFP microphone, strict mode works.
- [ ] If earbuds suspend A2DP, firmware reports that truthfully and proceeds to Phase 8 for compatibility mode.

---

# Phase 8 — HFP full-duplex compatibility downlink

## FD-18 — Add playback voice tap [P0]

### Files

```text
components/audio_processor/hfp_voice_tap.c
components/audio_processor/include/hfp_voice_tap.h
components/audio_processor/audio_processor.c
```

### Required work

- [ ] Tap canonical playback PCM from the existing audio engine.
- [ ] Do not create a second independent playback-source consumer.
- [ ] Downmix stereo to mono.
- [ ] Resample to active HFP codec rate.
- [ ] Write to a bounded HFP downlink ring.
- [ ] Disable all tap work when HFP downlink is inactive.
- [ ] Reset phase/ring on codec or session change.
- [ ] Preserve existing A2DP ring timing and source priority.
- [ ] Count downlink ring overflow.

### Architecture constraint

Do not call user/source fill functions twice per engine cycle. The voice tap must derive from PCM already produced by the authoritative audio engine.

### Host tests

- [ ] Correct mono output.
- [ ] Correct 8 kHz output count.
- [ ] Correct 16 kHz output count.
- [ ] Phase continuity across chunks.
- [ ] Disable path produces no extra writes.
- [ ] Ring-full path visible.
- [ ] Source priority unchanged.

## FD-19 — Implement HFP outgoing callback [P0]

### Required work

- [ ] Read only from the bounded downlink ring.
- [ ] No allocation.
- [ ] No blocking wait.
- [ ] No resampling in callback.
- [ ] Supply the exact requested size when data is available.
- [ ] Follow ESP-IDF API contract when data is unavailable.
- [ ] If silence is supplied, count exact silence bytes.
- [ ] Threshold repeated underruns into degraded/fault events.
- [ ] Trigger the ESP-IDF `outgoing_data_ready` mechanism from the producer side according to API requirements.

Reference intent:

```c
static uint32_t hfp_outgoing_cb(uint8_t *dst, uint32_t requested)
{
    if (!dst || requested == 0) {
        return 0;
    }

    size_t supplied = hfp_downlink_ring_read(dst, requested);
    if (supplied < requested) {
        memset(dst + supplied, 0, requested - supplied);
        duplex_stats_record_downlink_silence(requested - supplied);
        supplied = requested;
    }

    return (uint32_t)supplied;
}
```

Verify whether the installed ESP-IDF API expects a full buffer, partial count, or zero/no-data behavior. Use the documented contract, not assumptions.

### Tests

- [ ] Full data.
- [ ] Partial data.
- [ ] No data.
- [ ] Inactive session.
- [ ] Codec transition.
- [ ] Callback time bounds.
- [ ] No dynamic allocation.

## FD-20 — Hardware-test HFP full-duplex CVSD [P0 hardware gate]

### Tests

- [ ] Force/select `HFP_FULL` mode.
- [ ] Confirm earbud speaker audio uses HFP voice path.
- [ ] Confirm earbud microphone simultaneously reaches I2S0.
- [ ] Confirm no false A2DP-streaming status.
- [ ] Measure downlink underruns.
- [ ] Verify beep/UARTAUDIO/synth/I2S source priority still drives the voice tap correctly.
- [ ] Test mode switch from AUTO after remote A2DP suspend.
- [ ] Test return to ordinary A2DP after SCO stop.

---

# Phase 9 — mSBC wideband speech

## FD-21 — Enable and validate WBS/mSBC [P1]

### Required work

- [ ] Enable `CONFIG_BT_HFP_WBS_ENABLE=y`.
- [ ] Clean-build and measure flash/DRAM growth.
- [ ] Handle codec negotiation events explicitly.
- [ ] Add mSBC state to incoming and outgoing callback contracts.
- [ ] Forward 16 kHz incoming PCM directly to I2S ring.
- [ ] Configure voice tap for 16 kHz mono.
- [ ] Flush CVSD-generation data on codec switch.
- [ ] Report fallback to CVSD with reason.
- [ ] Do not claim mSBC before negotiation confirms it.

### Host tests

- [ ] mSBC state transition.
- [ ] 16 kHz input no-duplication behavior.
- [ ] CVSD -> mSBC generation flush.
- [ ] mSBC -> CVSD generation flush.
- [ ] Fallback permitted.
- [ ] Fallback forbidden/strict mode.
- [ ] Unexpected codec event.

### Hardware tests

- [ ] Negotiate mSBC with compatible earbuds.
- [ ] Verify 16 kHz microphone PCM.
- [ ] Verify HFP downlink voice audio.
- [ ] Compare heap and callback timing to CVSD.
- [ ] Repeat start/stop and disconnect tests.

---

# Phase 10 — Failure hardening and recovery

## FD-22 — Add thresholded health and fault/quarantine behavior [P0]

### Required work

- [ ] Define compile-time/configurable thresholds.
- [ ] Isolated underflow increments counters only.
- [ ] Repeated underflow transitions to degraded.
- [ ] Sustained overflow transitions to faulted.
- [ ] I2S write timeout transitions to faulted.
- [ ] Stop timeout transitions to quarantined.
- [ ] Invalid callback contract data transitions according to severity.
- [ ] Faults preserve last error and counters.
- [ ] Explicit recovery API verifies old resources are gone before reset.
- [ ] No automatic restart over an unproven worker.

### Pure tests

- [ ] Threshold boundary minus one.
- [ ] Exact threshold.
- [ ] Threshold plus one.
- [ ] Window expiry.
- [ ] Fault persistence.
- [ ] Quarantine rejects start.
- [ ] Explicit recovery only succeeds after cleanup proof.

## FD-23 — Harden all partial-start and teardown paths [P0]

### Failure injection matrix

- [ ] HFP callback registration failure.
- [ ] HFP profile init failure.
- [ ] HFP connect immediate failure.
- [ ] HFP SLC timeout.
- [ ] Ring allocation failure.
- [ ] I2S channel allocation failure.
- [ ] I2S mode init failure.
- [ ] Writer task creation failure.
- [ ] I2S enable failure.
- [ ] SCO connect immediate failure.
- [ ] SCO connect timeout.
- [ ] SCO disconnect timeout.
- [ ] I2S write timeout.
- [ ] Writer stop timeout.
- [ ] HFP profile deinit failure.

For each injection:

- [ ] Specific error returned or emitted.
- [ ] No leaked allocation.
- [ ] No leaked channel.
- [ ] No duplicate task.
- [ ] No false running state.
- [ ] A later valid start is either safe or explicitly blocked by quarantine.

## FD-24 — Harden event ordering and disconnect races [P0]

### Test sequences

- [ ] A2DP disconnect before HFP disconnect.
- [ ] HFP disconnect before A2DP disconnect.
- [ ] ACL loss while SCO active.
- [ ] SCO disconnected event before local stop call returns.
- [ ] Late SCO connected after timeout.
- [ ] Codec event after disconnect.
- [ ] Reconnect starts before stale old events arrive.
- [ ] User stop races remote disconnect.
- [ ] User mode change races remote suspend.

### Acceptance

- [ ] No stale event can resurrect a stopped session.
- [ ] No task/buffer is owned by two generations.
- [ ] Counters remain monotonic unless explicitly reset.

---

# Phase 11 — Full regression, resource gates, and documentation

## FD-25 — Complete host and device test suites [P0]

### Host tests

- [ ] All existing host binaries pass.
- [ ] New duplex state/policy/ring/converter tests pass.
- [ ] Failure-injection tests pass.
- [ ] Main-layering check passes.
- [ ] Static analysis/lint used by the repository passes.

### Device tests

Update applicable suites and mocks:

- [ ] `test_bluetooth`
- [ ] `test_app_audio`
- [ ] `test_manager`

Required device-test coverage:

- [ ] HFP init lifecycle with mocks.
- [ ] Event state transitions.
- [ ] I2S0 allocation/cleanup with test driver or wrappers.
- [ ] Callback routing.
- [ ] Command handlers.
- [ ] Failure paths not representable in pure host tests.

### Production restore

After any device-test flash, restore production firmware only with explicit user approval. Record which firmware remains on the device.

## FD-26 — Run resource acceptance matrix [P0]

Record all checkpoints:

| Checkpoint | Binary/heap/stack/audio data required |
|---|---|
| Boot | free/min/largest heap; task stacks |
| A2DP connected | heap delta |
| A2DP streaming | heap, A2DP counters |
| HFP SLC | heap delta |
| CVSD SCO | heap, callback, I2S, ring |
| CVSD simultaneous | all counters |
| HFP full duplex | downlink/uplink counters |
| mSBC SCO | heap, callback, ring |
| After stop | recovered heap and no live task |

Acceptance gates:

- [ ] At least 256 KiB app-partition headroom.
- [ ] Minimum internal heap at least 32 KiB, or explicit reviewed exception.
- [ ] Largest internal block at least 16 KiB, or explicit reviewed exception.
- [ ] Every affected task has required measured stack margin.
- [ ] HFP callback p99 below 500 us.
- [ ] HFP callback max below 2 ms.
- [ ] No persistent heap loss over 100 start/stop cycles.

## FD-27 — Run final hardware acceptance and soak [P0]

### Functional matrix

- [ ] Existing A2DP-only mode.
- [ ] CVSD A2DP plus HFP mic.
- [ ] CVSD HFP full duplex.
- [ ] mSBC A2DP plus HFP mic where supported.
- [ ] mSBC HFP full duplex.
- [ ] I2S receiver absent.
- [ ] Earbuds disappear.
- [ ] Reconnect.
- [ ] Repeated mode changes.
- [ ] Every existing playback source.

### 30-minute simultaneous soak

- [ ] Continuous playback.
- [ ] Continuous microphone input.
- [ ] I2S receiver continuously consumes.
- [ ] Zero microphone ring overflows after stabilization.
- [ ] Zero sustained I2S underruns.
- [ ] Zero I2S write timeouts.
- [ ] Zero invalid HFP frame-size events.
- [ ] No A2DP regression attributable to HFP.
- [ ] No watchdog, panic, brownout, or reboot.
- [ ] Record RF conditions, earbud model, codec, mode, and final counters.

## FD-28 — Update maintained documentation [P1]

### Files

- [ ] `esp_bt_audio_source/README.md`
- [ ] current architecture documentation
- [ ] command protocol documentation
- [ ] hardware/manual test checklist
- [ ] Kconfig help text
- [ ] `memory.md`

### Required documentation

- [ ] Wiring table: GPIO32 BCLK, GPIO33 WS, GPIO27 DOUT, common ground.
- [ ] Receiver requirements: 16 kHz, signed 16-bit mono, Philips I2S slave, no MCLK.
- [ ] A2DP/HFP operating modes.
- [ ] CVSD versus mSBC behavior.
- [ ] Commands and events.
- [ ] Failure counters and interpretation.
- [ ] Earbud compatibility limitation.
- [ ] Resource measurements.
- [ ] Recovery steps for faulted/quarantined sessions.
- [ ] Explicit statement that microphone audio does not start automatically at boot.

## FD-29 — Final code review and handoff [P0]

### Review checklist

- [ ] Search for ignored `esp_err_t` results.
- [ ] Search for `malloc`, `calloc`, `free`, logging, and blocking calls reachable from HFP callbacks.
- [ ] Search for external `vTaskDelete(handle)` shutdown shortcuts.
- [ ] Search for `volatile` used as synchronization.
- [ ] Search for zero-filling paths that do not increment counters.
- [ ] Search for success responses after task/profile/I2S failures.
- [ ] Search for unbounded waits and retries.
- [ ] Search for stale-session events without generation checks.
- [ ] Search for raw shared multi-field snapshots.
- [ ] Search for accidental changes to I2S1 or existing source priority.
- [ ] Confirm no generated build/log artifacts are committed.
- [ ] Confirm every referenced assistant-created document exists.

### Final handoff contents

- [ ] Spec path.
- [ ] TODO path with every task accurately checked/unchecked.
- [ ] Branch and commit list.
- [ ] Test commands and results.
- [ ] Hardware models and wiring.
- [ ] Resource table.
- [ ] Known limitations.
- [ ] Remaining work, if any.
- [ ] Latest `memory.md` entry.

---

# Definition of done

Do not mark this TODO complete until:

- [ ] HFP AG and one SCO/eSCO connection operate safely.
- [ ] CVSD and mSBC microphone PCM reach I2S0 at the fixed wire format.
- [ ] GPIO32/33/27 are verified on hardware.
- [ ] A2DP plus HFP microphone works where the earbuds support it.
- [ ] HFP full-duplex compatibility mode works when A2DP is suspended.
- [ ] All mode changes and fallbacks are visible.
- [ ] No Bluetooth callback blocks, allocates, writes I2S, or resamples.
- [ ] No quiet microphone loss or unreported silence insertion remains.
- [ ] Start/stop/disconnect/race/failure paths are safe.
- [ ] Existing behavior remains correct when HFP audio is inactive.
- [ ] All host/device tests pass.
- [ ] Resource gates pass or receive explicit user approval.
- [ ] The final 30-minute soak passes.
- [ ] Documentation and `memory.md` are current.
- [ ] Hardware was never flashed without explicit user approval.
