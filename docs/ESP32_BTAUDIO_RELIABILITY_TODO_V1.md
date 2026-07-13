# ESP32 Bluetooth Audio Reliability Hardening TODO v1.0

**Companion spec:** `ESP32_BTAUDIO_RELIABILITY_SPEC_V1.md`  
**Implement one task at a time. Do not combine unrelated refactors.**  
**Target model:** Qwen3.6 27B through Claude Code

## 0. Instructions for Claude Code

Before editing:

- [ ] Read the companion specification completely.
- [ ] Read root `CLAUDE.md` and both project-specific instructions.
- [ ] Do not inspect or modify `archive/`.
- [ ] Do not change the proven I2S role, pins, rate, framing, or slot format.
- [ ] Use TDD: create a failing test or failure-injection path before the fix.
- [ ] Do not mark a task complete because code compiles; run its acceptance tests.
- [ ] Do not return success from a fallback path when required work failed.
- [ ] Keep behavior changes and cleanup refactors in separate commits where practical.
- [ ] Never flash hardware without explicit user confirmation.
- [ ] Update `memory.md` only according to the repository timestamp/model rules.

Recommended commit pattern:

```text
fix(bt-link): RH-S3-01 make request lifetime ownership-safe
fix(radio): RH-S3-02 prevent overlapping playback generations
fix(audio): RH-WR-02 make engine stop timeout explicit
```

---

# Phase 1 — Stop memory corruption and duplicate workers

## RH-S3-01 — Fix `bt_link_send()` request lifetime [P0]

**Files:**

- `esp_i2s_source/components/bt_link/bt_link.c`
- `esp_i2s_source/components/bt_link/include/bt_link.h`
- new pure helper/test files if useful
- `esp_i2s_source/test/host_test/CMakeLists.txt`

### Required work

- [ ] Store the timeout passed to `bt_link_init()` in module state.
- [ ] Replace stack-local queued requests with heap/pool requests.
- [ ] Replace shared `s_done_sem` with one completion object per request.
- [ ] Add explicit request lifetime state.
- [ ] Protect abandonment/completion/free decisions with a lock/critical section.
- [ ] Make abandoned queued requests safe to discard.
- [ ] Guarantee exactly one free.
- [ ] Add full init rollback for UART/queue/mutex/task failures.
- [ ] Preserve command/result/data behavior.

### Reference implementation pattern

This is the intended ownership model. Adapt ESP-IDF cleanup names as needed, but do not simplify it back to a queued stack pointer.

```c
typedef enum {
    BT_REQ_QUEUED = 0,
    BT_REQ_ACTIVE,
    BT_REQ_COMPLETED,
    BT_REQ_ABANDONED,
} bt_req_lifetime_t;

typedef struct {
    char cmd[BT_LINK_LINE_MAX];
    bt_link_cmd_state_t state;
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
    SemaphoreHandle_t done;
    bt_req_lifetime_t lifetime;
} bt_link_request_t;

static portMUX_TYPE s_req_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_cmd_timeout_ms;

static void request_destroy(bt_link_request_t *req)
{
    if (!req) return;
    if (req->done) vSemaphoreDelete(req->done);
    free(req);
}

/* Called only by the link task when a request reaches a terminal result. */
static void request_complete(bt_link_request_t *req,
                             bt_link_cmd_state_t state,
                             const char *result,
                             const char *data)
{
    bool abandoned;

    req->state = state;
    strlcpy(req->result, result ? result : "", sizeof(req->result));
    strlcpy(req->data, data ? data : "", sizeof(req->data));

    portENTER_CRITICAL(&s_req_mux);
    abandoned = (req->lifetime == BT_REQ_ABANDONED);
    if (!abandoned) req->lifetime = BT_REQ_COMPLETED;
    portEXIT_CRITICAL(&s_req_mux);

    if (abandoned) {
        request_destroy(req);
    } else {
        xSemaphoreGive(req->done);
    }
}
```

Caller timeout handling:

```c
bt_link_request_t *req = calloc(1, sizeof(*req));
if (!req) return ESP_ERR_NO_MEM;
req->done = xSemaphoreCreateBinary();
if (!req->done) {
    free(req);
    return ESP_ERR_NO_MEM;
}
req->lifetime = BT_REQ_QUEUED;
strlcpy(req->cmd, cmd, sizeof(req->cmd));

if (xQueueSend(s_cmd_queue, &req, pdMS_TO_TICKS(250)) != pdTRUE) {
    request_destroy(req);
    return ESP_ERR_TIMEOUT;
}

TickType_t wait = pdMS_TO_TICKS(s_cmd_timeout_ms + 500U);
if (xSemaphoreTake(req->done, wait) != pdTRUE) {
    bool completed;
    portENTER_CRITICAL(&s_req_mux);
    completed = (req->lifetime == BT_REQ_COMPLETED);
    if (!completed) req->lifetime = BT_REQ_ABANDONED;
    portEXIT_CRITICAL(&s_req_mux);

    if (!completed) {
        /* Worker now owns final destruction. Do not touch req again. */
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_TIMEOUT;
    }
    /* Completion raced with timeout; consume the result below. */
}

/* Copy results before destroying. */
if (out_state) *out_state = req->state;
if (result && result_sz) strlcpy(result, req->result, result_sz);
if (data && data_sz) strlcpy(data, req->data, data_sz);
request_destroy(req);
```

The worker must check whether a dequeued request was already abandoned before transmitting it. Use the same critical section. If abandoned, destroy it and dequeue the next request.

### Tests

- [ ] Completion before timeout returns the correct result/data.
- [ ] Caller timeout followed by late worker completion does not access caller stack.
- [ ] Late completion of request A cannot release request B.
- [ ] Abandoned queued request is freed without UART transmission.
- [ ] Queue-full path frees request.
- [ ] Configured non-default timeout is honored.
- [ ] Repeated init after injected init failure succeeds.

### Acceptance

- [ ] ASan host tests pass.
- [ ] No module-global completion semaphore remains.
- [ ] No request pointer refers to a caller stack object.

---

## RH-S3-02 — Replace radio global boolean lifecycle with session generations [P0]

**Files:**

- `esp_i2s_source/components/radio/radio.c`
- `esp_i2s_source/components/radio/include/radio.h`
- new `radio_lifecycle.c/.h` pure helper recommended
- host tests

### Required work

- [ ] Change `radio_stop()` to return `esp_err_t`.
- [ ] Add lifecycle enum and active session pointer.
- [ ] Add monotonically increasing generation.
- [ ] Give each session its own URL and stop flag.
- [ ] Add exit event bits for stream and decoder tasks.
- [ ] Both workers must receive the session pointer as `arg`.
- [ ] Worker loops must test their own session stop flag, not a resurrectable global boolean.
- [ ] `radio_play()` must fully stop/join the old session before resetting rings.
- [ ] Stop timeout must leave the old session registered and block restart.
- [ ] Remove any path that permits static scratch buffers to be used by overlapping generations.

### Reference lifecycle skeleton

```c
typedef enum {
    RADIO_STATE_STOPPED = 0,
    RADIO_STATE_STARTING,
    RADIO_STATE_RUNNING,
    RADIO_STATE_STOPPING,
    RADIO_STATE_FAULTED,
} radio_state_t;

#define RADIO_EVT_STREAM_EXITED  BIT0
#define RADIO_EVT_DECODER_EXITED BIT1
#define RADIO_EVT_ALL_EXITED (RADIO_EVT_STREAM_EXITED | RADIO_EVT_DECODER_EXITED)

typedef struct radio_session {
    uint32_t generation;
    char url[RADIO_URL_MAX];
    TaskHandle_t stream_task;
    TaskHandle_t decoder_task;
    EventGroupHandle_t events;
    _Atomic bool stop_requested;
} radio_session_t;

static SemaphoreHandle_t s_control_mtx;
static radio_session_t *s_active_session;
static radio_state_t s_radio_state;
static uint32_t s_next_generation;

static bool session_should_run(const radio_session_t *s)
{
    return s && !atomic_load_explicit(&s->stop_requested, memory_order_acquire);
}
```

Worker exit pattern:

```c
static void stream_task(void *arg)
{
    radio_session_t *s = arg;
    /* ... finite-timeout HTTP loop using s->url ... */
    xEventGroupSetBits(s->events, RADIO_EVT_STREAM_EXITED);
    s->stream_task = NULL; /* protect if status readers can race */
    vTaskDelete(NULL);
}
```

Stop pattern:

```c
esp_err_t radio_stop(void)
{
    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    radio_session_t *s = s_active_session;
    if (!s) {
        s_radio_state = RADIO_STATE_STOPPED;
        xSemaphoreGive(s_control_mtx);
        return ESP_OK;
    }
    s_radio_state = RADIO_STATE_STOPPING;
    atomic_store_explicit(&s->stop_requested, true, memory_order_release);
    xSemaphoreGive(s_control_mtx);

    EventBits_t bits = xEventGroupWaitBits(
        s->events, RADIO_EVT_ALL_EXITED, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));

    if ((bits & RADIO_EVT_ALL_EXITED) != RADIO_EVT_ALL_EXITED) {
        xSemaphoreTake(s_control_mtx, portMAX_DELAY);
        s_radio_state = RADIO_STATE_FAULTED;
        xSemaphoreGive(s_control_mtx);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_control_mtx, portMAX_DELAY);
    if (s_active_session == s) s_active_session = NULL;
    s_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(s_control_mtx);

    vEventGroupDelete(s->events);
    free(s);
    return ESP_OK;
}
```

### Important warning

Do not set `s_active_session = NULL` or free the session before both exit bits are observed. Do not start a replacement generation after a stop timeout.

### Tests

- [ ] Stop then immediate play never overlaps generations.
- [ ] Old generation cannot become active when a new generation starts.
- [ ] Stream exit without decoder exit causes timeout/fault.
- [ ] Decoder exit without stream exit causes timeout/fault.
- [ ] Task-creation failure unwinds the other worker.
- [ ] Exactly one session free occurs.
- [ ] Ring reset occurs only after prior exit acknowledgement.

---

## RH-S3-03 — Make decoder-task creation failure fail `radio_play()` [P0]

This may be implemented as part of RH-S3-02 but must have its own test/evidence.

- [ ] If stream task creation fails, return `ESP_ERR_NO_MEM` and free the session.
- [ ] If decoder task creation fails, request stream stop, join it, clean up, and return `ESP_ERR_NO_MEM`.
- [ ] Do not leave the HTTP stream filling a ring with no decoder.
- [ ] Web/API response must report failure.

Reference pattern:

```c
if (xTaskCreate(stream_task, "radio", STREAM_STACK, s, STREAM_PRIO,
                &s->stream_task) != pdPASS) {
    err = ESP_ERR_NO_MEM;
    goto fail_session;
}

if (xTaskCreate(decoder_task, "radio_dec", DEC_STACK, s, DEC_PRIO,
                &s->decoder_task) != pdPASS) {
    atomic_store(&s->stop_requested, true);
    xEventGroupWaitBits(s->events, RADIO_EVT_STREAM_EXITED,
                        pdFALSE, pdTRUE, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));
    err = ESP_ERR_NO_MEM;
    goto fail_session;
}
```

---

## RH-WR-01 — Replace unsafe BT status request queue with synchronized snapshot [P0]

**Files:**

- `esp_bt_audio_source/components/platform_shim/platform_sync.h`
- `platform_sync_esp32.c`
- `platform_sync_host.c`
- `components/bt_manager/bt_manager.c`
- `components/bt_manager/include/bt_manager_internal.h`
- every BT manager module that reads/writes `bt_ctx`
- host tests

### Locked design

Use a platform mutex around `bt_ctx`. Do not merely call `bt_app_task_start_up()` and leave direct callback writes unsynchronized.

### Platform mutex reference API

```c
#define PLATFORM_WAIT_FOREVER UINT32_MAX

typedef struct platform_mutex_s *platform_mutex_t;

platform_mutex_t platform_mutex_create(void);
void platform_mutex_delete(platform_mutex_t mutex);
esp_err_t platform_mutex_lock(platform_mutex_t mutex, uint32_t timeout_ms);
esp_err_t platform_mutex_unlock(platform_mutex_t mutex);
```

ESP32 implementation outline:

```c
struct platform_mutex_s {
    SemaphoreHandle_t handle;
};

platform_mutex_t platform_mutex_create(void)
{
    platform_mutex_t m = platform_malloc(sizeof(*m), PLATFORM_MEM_CAP_DEFAULT);
    if (!m) return NULL;
    m->handle = xSemaphoreCreateMutex();
    if (!m->handle) {
        platform_free(m);
        return NULL;
    }
    return m;
}

esp_err_t platform_mutex_lock(platform_mutex_t m, uint32_t timeout_ms)
{
    if (!m || !m->handle) return ESP_ERR_INVALID_ARG;
    TickType_t ticks = timeout_ms == PLATFORM_WAIT_FOREVER
        ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(m->handle, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t platform_mutex_unlock(platform_mutex_t m)
{
    if (!m || !m->handle) return ESP_ERR_INVALID_ARG;
    return xSemaphoreGive(m->handle) == pdTRUE ? ESP_OK : ESP_FAIL;
}
```

### BT context helpers

```c
static platform_mutex_t s_bt_ctx_mutex;

esp_err_t bt_ctx_lock(uint32_t timeout_ms)
{
    if (!s_bt_ctx_mutex) return ESP_ERR_INVALID_STATE;
    return platform_mutex_lock(s_bt_ctx_mutex, timeout_ms);
}

void bt_ctx_unlock(void)
{
    esp_err_t err = platform_mutex_unlock(s_bt_ctx_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bt_ctx unlock failed: %s", esp_err_to_name(err));
    }
}

esp_err_t bt_manager_get_status(bt_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(100);
    if (err != ESP_OK) return err;

    out->initialized = bt_ctx.initialized;
    out->connected = bt_ctx.connected;
    out->audio_playing = bt_ctx.audio_playing;
    out->scanning = bt_ctx.scanning;
    safe_copy_str(out->connected_mac, sizeof(out->connected_mac),
                  bt_ctx.connected_mac);
    safe_copy_str(out->connected_name, sizeof(out->connected_name),
                  bt_ctx.connected_name);

    bt_ctx_unlock();
    return ESP_OK;
}
```

### Callback rule

Never call external callbacks while holding the mutex:

```c
bt_connected_cb cb = NULL;
char mac[18];
char name[32];

ESP_ERROR_CHECK(bt_ctx_lock(PLATFORM_WAIT_FOREVER));
bt_ctx.connected = true;
bt_ctx.connecting = false;
safe_copy_str(bt_ctx.connected_mac, sizeof(bt_ctx.connected_mac), bda_str);
cb = bt_ctx.connected_callback;
safe_copy_str(mac, sizeof(mac), bt_ctx.connected_mac);
safe_copy_str(name, sizeof(name), bt_ctx.connected_name);
bt_ctx_unlock();

if (cb) cb(mac, name);
```

### Audit checklist

- [ ] `bt_events_a2dp.c`
- [ ] `bt_events_gap.c`
- [ ] `bt_events_avrc.c`
- [ ] `bt_connection.c`
- [ ] `bt_scan.c`
- [ ] `bt_manager_ops.c`
- [ ] `bt_manager_mocks.c` where production-like state is used
- [ ] pairing/device list functions
- [ ] all status and command helpers

### Remove obsolete unsafe path

- [ ] Remove `bt_mgr_request_t` stack-pointer queue use.
- [ ] Remove or deprecate `BT_APP_SIG_MGR_REQUEST` if no longer used.
- [ ] Update tests that assert old queue behavior.
- [ ] Keep general `bt_app_work_dispatch()` only if independently needed.
- [ ] Correct ownership comments and contract documentation.

### Tests

- [ ] Host mutex create/lock/unlock/delete.
- [ ] Timeout behavior.
- [ ] Concurrent writer and status-reader stress.
- [ ] Status snapshot fields are internally consistent.
- [ ] Callback re-entry does not deadlock.
- [ ] `bt_manager_is_connected()` returns true after connected event without BtAppTask startup.

---

## RH-WR-02 — Make audio engine stop timeout safe [P0]

**Files:**

- `esp_bt_audio_source/components/audio_processor/audio_processor.c`
- `audio_processor_engine.c`
- internal header/state files
- tests

### Required work

- [ ] Add explicit audio lifecycle state.
- [ ] Do not set the task handle to null from the stopping task unless it is actually exiting.
- [ ] On stop timeout, return `ESP_ERR_TIMEOUT`.
- [ ] Retain the live handle.
- [ ] Do not stop I2S underneath the live engine.
- [ ] Reject restart while state is `STOPPING` or `FAULTED` and handle is non-null.
- [ ] Add a diagnostic with state and task handle presence.

Reference control flow:

```c
esp_err_t audio_processor_stop(void)
{
    if (s_audio_state != AUDIO_STATE_RUNNING) return ESP_ERR_INVALID_STATE;

    s_audio_state = AUDIO_STATE_STOPPING;
    audio_engine_request_stop();

    EventBits_t bits = xEventGroupWaitBits(
        s_engine_events, ENGINE_STOPPED_BIT,
        pdTRUE, pdTRUE, pdMS_TO_TICKS(AUDIO_STOP_TIMEOUT_MS));

    if ((bits & ENGINE_STOPPED_BIT) == 0) {
        s_audio_state = AUDIO_STATE_FAULTED;
        ESP_LOGE(TAG, "audio engine stop timed out; task remains owned");
        return ESP_ERR_TIMEOUT;
    }

    if (s_audio_engine_task_handle != NULL) {
        s_audio_state = AUDIO_STATE_FAULTED;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2s_manager_stop();
    if (err != ESP_OK) {
        s_audio_state = AUDIO_STATE_FAULTED;
        return err;
    }

    s_audio_state = AUDIO_STATE_STOPPED;
    s_is_running = false;
    return ESP_OK;
}
```

### Tests

- [ ] Cooperative stop success.
- [ ] Injected non-exiting engine returns timeout.
- [ ] Handle remains non-null on timeout.
- [ ] I2S stop mock is not called after timeout.
- [ ] Restart is rejected after timeout.
- [ ] Normal later exit can be observed and recovery behavior is documented/tested.

---

# Phase 2 — Correct audio data integrity

## RH-S3-04 — Consume all resampler input [P1]

**Files:** `esp_i2s_source/components/radio/radio.c`, resampler tests

### Required code pattern

```c
static esp_err_t resample_and_queue(radio_session_t *session,
                                    radio_resampler_t *rs,
                                    const int16_t *pcm,
                                    size_t in_frames,
                                    int channels)
{
    size_t offset = 0;
    int16_t rsbuf[4096 * 2];

    while (offset < in_frames && session_should_run(session)) {
        size_t used = 0;
        size_t produced = radio_resampler_run(
            rs,
            pcm + offset * (size_t)channels,
            in_frames - offset,
            rsbuf,
            4096,
            &used);

        if (used == 0 && produced == 0) {
            return ESP_ERR_INVALID_STATE; /* resampler stalled */
        }

        offset += used;

        size_t bytes = produced * 2U * sizeof(int16_t);
        size_t written = 0;
        while (written < bytes && session_should_run(session)) {
            size_t n = pcm_write((const uint8_t *)rsbuf + written,
                                 bytes - written);
            if (n == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            written += n;
        }

        if (written != bytes) return ESP_ERR_INVALID_STATE;
    }

    return offset == in_frames ? ESP_OK : ESP_ERR_INVALID_STATE;
}
```

Do not assume stereo input when advancing input; use decoder channel count. Output remains stereo.

### Tests

- [ ] 22.05 kHz, 4096 input frames: total `used == 4096` across calls.
- [ ] 32 kHz: total `used == input`.
- [ ] 44.1 kHz passthrough.
- [ ] 48 kHz downsample.
- [ ] Small output capacity forces multiple calls without data loss.
- [ ] Mono input offset arithmetic is correct.

---

## RH-S3-05 — Preserve compressed decoder tail [P1]

### Required work

- [ ] Maintain `pending` bytes in an accumulation buffer.
- [ ] Read new compressed bytes after the pending tail.
- [ ] After processing, memmove any unconsumed tail to offset zero.
- [ ] If no progress and buffer has free space, read more.
- [ ] If no progress and buffer is full, increment error/resync counter and discard one byte or reset decoder according to a documented rule.

Reference loop:

```c
uint8_t *inbuf = heap_caps_malloc(DECODER_INPUT_CAP, MALLOC_CAP_8BIT);
size_t pending = 0;

while (session_should_run(session)) {
    if (pending < DECODER_INPUT_CAP) {
        size_t got = radio_read(inbuf + pending, DECODER_INPUT_CAP - pending);
        pending += got;
    }

    if (pending == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
    }

    esp_audio_simple_dec_raw_t raw = {
        .buffer = inbuf,
        .len = (uint32_t)pending,
        .consumed = 0,
    };

    size_t consumed_total = 0;
    while (raw.len > 0 && session_should_run(session)) {
        raw.consumed = 0;
        esp_audio_simple_dec_out_t out = {
            .buffer = pcmbuf,
            .len = PCM_BUF_BYTES,
        };
        esp_audio_err_t err = esp_audio_simple_dec_process(dec, &raw, &out);

        if (out.decoded_size > 0) {
            /* resample_and_queue(...) */
        }

        if (raw.consumed == 0) break;
        consumed_total += raw.consumed;
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
    }

    if (consumed_total > 0) {
        pending -= consumed_total;
        memmove(inbuf, inbuf + consumed_total, pending);
    } else if (pending == DECODER_INPUT_CAP) {
        memmove(inbuf, inbuf + 1, pending - 1);
        pending--;
        record_decoder_resync();
    }
}
```

Confirm the decoder API’s exact `consumed` semantics while implementing.

---

## RH-S3-06 — Fix signed sample packing UB [P1]

**File:** `esp_i2s_source/main/main.c`

- [ ] Replace signed left shift with multiplication.
- [ ] Add a pure host test covering `INT16_MIN`, `-1`, `0`, `1`, `INT16_MAX`.
- [ ] Verify output bit patterns match the intended top-half packing.

```c
static inline int32_t pack_s16_msb(int16_t sample)
{
    return (int32_t)sample * INT32_C(65536);
}
```

Expected examples:

```text
-32768 -> 0x80000000
-1     -> 0xFFFF0000
0      -> 0x00000000
1      -> 0x00010000
32767  -> 0x7FFF0000
```

---

## RH-S3-07 — Make `pcm_ring` a valid atomic SPSC ring [P1]

### Required work

- [ ] Replace volatile indices with C11 atomics.
- [ ] Use acquire/release ordering.
- [ ] Make peak statistic atomic or snapshot-protected.
- [ ] Update comments that incorrectly equate atomic word reads with synchronization.
- [ ] Set C standard to C11 where needed.

Reference core:

```c
#include <stdatomic.h>

struct pcm_ring {
    uint8_t *buf;
    size_t size;
    size_t capacity;
    _Atomic size_t head;
    _Atomic size_t tail;
    _Atomic size_t peak;
};

size_t pcm_ring_write(pcm_ring_t *r, const uint8_t *src, size_t len)
{
    size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    size_t used = used_of(head, tail, r->size);
    size_t n = len < (r->capacity - used) ? len : (r->capacity - used);

    /* copy exactly as current implementation */

    atomic_store_explicit(&r->head, (head + n) % r->size,
                          memory_order_release);

    size_t candidate = used + n;
    size_t peak = atomic_load_explicit(&r->peak, memory_order_relaxed);
    while (candidate > peak &&
           !atomic_compare_exchange_weak_explicit(
               &r->peak, &peak, candidate,
               memory_order_relaxed, memory_order_relaxed)) {
    }
    return n;
}

size_t pcm_ring_read(pcm_ring_t *r, uint8_t *dst, size_t len)
{
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    /* calculate and copy */
    atomic_store_explicit(&r->tail, (tail + n) % r->size,
                          memory_order_release);
    return n;
}
```

### Tests

- [ ] Existing single-thread tests pass.
- [ ] Pthread producer writes incrementing sequence blocks.
- [ ] Consumer verifies exact byte order and total count.
- [ ] Force many wraparounds with a small ring.
- [ ] Run at least 1,000,000 bytes.
- [ ] Run under ThreadSanitizer if available.

---

# Phase 3 — Make worker start/stop truthful

## RH-S3-08 — Make I2S writer stoppable without external clocks [P1]

**Files:** `i2s_out.c`, header, tests/hooks

### Required work

- [ ] Add state enum and event group/task notification.
- [ ] Use finite write timeout, e.g. 100 ms.
- [ ] Treat timeout as a chance to check stop, not automatically fatal.
- [ ] Signal writer started and exited.
- [ ] Wait for exit before disabling channel.
- [ ] Return timeout and retain handle on failed stop.
- [ ] Reject start if handle is non-null.
- [ ] Protect stats snapshot.

Reference sink:

```c
#define I2S_WRITE_TIMEOUT_MS 100

static int i2s_sink(void *ctx, const uint8_t *data, size_t len)
{
    size_t written = 0;
    esp_err_t err = i2s_channel_write(
        (i2s_chan_handle_t)ctx, data, len, &written,
        pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS));

    if (err == ESP_ERR_TIMEOUT) return 1; /* retry/check stop */
    if (err != ESP_OK || written != len) return -1;
    return 0;
}
```

Writer pattern:

```c
xEventGroupSetBits(s_events, I2S_EVT_WRITER_STARTED);
while (atomic_load(&s_run_requested)) {
    int rc = i2s_out_pump_once(...);
    if (rc < 0) {
        record_i2s_error();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
s_writer_task = NULL;
xEventGroupSetBits(s_events, I2S_EVT_WRITER_EXITED);
vTaskDelete(NULL);
```

If `i2s_out_pump_once()` cannot expose sink timeout distinctly, change its contract or add a wrapper without breaking pure tests.

### Device acceptance

With WROOM32 powered off/no BCLK:

- [ ] `i2s_out_stop()` returns within the defined deadline.
- [ ] writer exit bit is observed.
- [ ] restart after clock restoration creates one writer only.

---

## RH-WR-03 — Make audio engine startup acknowledgement truthful [P1]

- [ ] Add `s_engine_start_error` or equivalent.
- [ ] Engine sets error before `ENGINE_STOPPED_BIT` when work allocation/init fails.
- [ ] Start waits for `ENGINE_RUNNING_BIT | ENGINE_STOPPED_BIT`.
- [ ] If stopped arrives first, return stored error.
- [ ] If wait times out, return `ESP_ERR_TIMEOUT` and do not mark running.

```c
EventBits_t bits = xEventGroupWaitBits(
    s_engine_events,
    ENGINE_RUNNING_BIT | ENGINE_STOPPED_BIT,
    pdFALSE, pdFALSE,
    pdMS_TO_TICKS(AUDIO_START_TIMEOUT_MS));

if (bits & ENGINE_RUNNING_BIT) {
    s_audio_state = AUDIO_STATE_RUNNING;
    s_is_running = true;
    return ESP_OK;
}

if (bits & ENGINE_STOPPED_BIT) {
    s_audio_state = AUDIO_STATE_STOPPED;
    return s_engine_start_error != ESP_OK
        ? s_engine_start_error : ESP_FAIL;
}

s_audio_state = AUDIO_STATE_FAULTED;
return ESP_ERR_TIMEOUT;
```

Tests must inject engine buffer allocation failure.

---

# Phase 4 — Serialize control-plane operations

## RH-S3-09 — Replace per-request radio tasks with one command worker [P1]

**Files:** `web_ui_radio.c`, web startup, new radio command module recommended

### Command object

```c
typedef enum {
    RADIO_CMD_PLAY = 0,
    RADIO_CMD_STOP,
} radio_cmd_type_t;

typedef struct {
    radio_cmd_type_t type;
    char url[RADIO_URL_MAX];
    int station_id;
} radio_cmd_t;
```

### Worker

```c
static void radio_control_task(void *arg)
{
    radio_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_radio_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t err = cmd.type == RADIO_CMD_PLAY
            ? radio_play(cmd.url)
            : radio_stop();
        record_last_radio_command_result(cmd.type, err);
    }
}
```

### HTTP handler behavior

```c
radio_cmd_t cmd = {
    .type = RADIO_CMD_PLAY,
    .station_id = station_id,
};
strlcpy(cmd.url, url, sizeof(cmd.url));

if (xQueueSend(s_radio_cmd_q, &cmd, 0) != pdTRUE) {
    return send_json_error(req, "radio_queue_full", ESP_ERR_TIMEOUT);
}
return send_json_accepted(req);
```

- [ ] Remove shared static `s_radio_url`.
- [ ] Check worker creation.
- [ ] Initialize before web routes.
- [ ] Do not return success before queue acceptance.
- [ ] Test two rapid play URLs retain their own values.

---

## RH-S3-10 — Initialize controller before web server [P1]

- [ ] Identify every web handler that uses controller state.
- [ ] Split `ctrl_init()` from long-running startup orchestration if necessary.
- [ ] Create mutex/state before `web_ui_start()`.
- [ ] Add defensive `ESP_ERR_INVALID_STATE` checks in public controller APIs.
- [ ] Add startup-order test or assertions.

---

## RH-S3-11 — Check all URI registration results [P1]

Reference helper:

```c
static esp_err_t register_uri(httpd_handle_t server,
                              const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(server, uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "route registration failed: %s %s: %s",
                 http_method_str(uri->method), uri->uri,
                 esp_err_to_name(err));
    }
    return err;
}
```

Startup pattern:

```c
#define REGISTER_OR_FAIL(uri_ptr) do { \
    err = register_uri(s_server, (uri_ptr)); \
    if (err != ESP_OK) goto fail; \
} while (0)

/* registrations */
return ESP_OK;

fail:
httpd_stop(s_server);
s_server = NULL;
return err;
```

- [ ] Failure injection proves server stops and reports failure.

---

# Phase 5 — Synchronization and coherent status

## RH-S3-12 — Protect I2S stats snapshot [P1]

Use a `portMUX_TYPE` because updates are short and frequent.

```c
static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;

static void stats_add_written(size_t n)
{
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.bytes_written += n;
    portEXIT_CRITICAL(&s_stats_mux);
}

void i2s_out_get_stats(i2s_out_stats_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}
```

If pure `i2s_out_pump_once()` currently updates the structure directly, either protect the entire call or have it update a local delta that the device wrapper commits under lock.

- [ ] Add stress test/snapshot consistency test.

---

## RH-S3-13 — Make radio status coherent [P1]

- [ ] Define one telemetry/state lock policy.
- [ ] Ensure every writer follows it, including `on_audio`, HTTP header callback, stream task, decoder task, title callback, play/stop.
- [ ] Do not take the same non-recursive mutex twice through nested helpers.
- [ ] Snapshot related fields under one lock.
- [ ] Protect 64-bit byte counter.
- [ ] Add generation and last-error fields.

Recommended approach:

- control mutex for lifecycle/session pointer.
- telemetry mutex for status fields.
- existing compressed-ring and PCM-ring locks only for ring storage.

Avoid using one mutex for blocking network/decoder operations.

---

# Phase 6 — Cleanup and rollback

## RH-S3-14 — Add `bt_link_init()` rollback [P1]

Use acquired-resource flags and one `fail:` label.

```c
esp_err_t bt_link_init(uint32_t timeout_ms)
{
    esp_err_t err;
    bool uart_installed = false;

    err = uart_driver_install(...);
    if (err != ESP_OK) goto fail;
    uart_installed = true;

    err = uart_param_config(...);
    if (err != ESP_OK) goto fail;

    /* create queue/mutex, then task */
    return ESP_OK;

fail:
    if (s_task_handle) { /* only if a safe shutdown exists */ }
    if (s_send_mutex) { vSemaphoreDelete(s_send_mutex); s_send_mutex = NULL; }
    if (s_cmd_queue) { vQueueDelete(s_cmd_queue); s_cmd_queue = NULL; }
    if (uart_installed) uart_driver_delete(BT_LINK_UART);
    return err;
}
```

Do not externally delete a worker that might be active; init failure happens before publishing readiness, so order acquisition to avoid that problem.

---

## RH-S3-15 — Fix `radio_init()` leaks and make retry safe [P1]

- [ ] Free compressed ring if PCM allocation fails.
- [ ] Free both rings if mutex creation fails.
- [ ] Delete partially created mutexes.
- [ ] Reset capacities/pointers.
- [ ] Reject duplicate successful init or define idempotence.
- [ ] Add allocation failure tests for every stage.

---

## RH-WR-04 — Add audio processor partial-init cleanup [P1]

### Helper pattern

```c
static void audio_processor_cleanup_partial_init(void)
{
    if (s_volume_commit_timer) {
        esp_timer_stop(s_volume_commit_timer);
        esp_timer_delete(s_volume_commit_timer);
        s_volume_commit_timer = NULL;
    }

    if (s_engine_events) {
        vEventGroupDelete(s_engine_events);
        s_engine_events = NULL;
    }

    audio_ringbuffer_deinit(); /* make idempotent if needed */
    beep_manager_deinit();
    i2s_manager_deinit();

    platform_free(s_proc_buffer2); s_proc_buffer2 = NULL;
    platform_free(s_proc_buffer);  s_proc_buffer = NULL;
    platform_free(s_capture_buffer); s_capture_buffer = NULL;
    s_runtime_work_bytes = 0;
    s_is_initialized = false;
}
```

- [ ] Make subordinate deinit calls safe when not initialized, or guard them.
- [ ] Use one failure label.
- [ ] Test each failure stage and retry.

---

## RH-WR-05 — Add Bluetooth initialization rollback [P1]

- [ ] Track completed stages in local booleans/enum.
- [ ] Unregister/deinit profiles where ESP-IDF APIs support it.
- [ ] Disable/deinit Bluedroid in reverse order.
- [ ] Disable/deinit controller in reverse order.
- [ ] delete BT context mutex.
- [ ] clear `bt_ctx` only under safe initialization conditions.
- [ ] return original failure.
- [ ] Add failure injection per stage.

Do not call cleanup APIs for stages that never completed.

---

# Phase 7 — Persistence and truthful API responses

## RH-S3-16 — Return NVS errors from gain and prebuffer setters [P1]

Reference helper:

```c
static esp_err_t nvs_set_u8_committed(const char *ns,
                                      const char *key,
                                      uint8_t value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
```

Setter pattern:

```c
esp_err_t i2s_out_set_gain(int pct)
{
    pct = CLAMP(pct, 0, 100);
    s_gain_pct = pct; /* runtime application */

    esp_err_t err = nvs_set_u8_committed(
        I2S_GAIN_NVS_NS, I2S_GAIN_NVS_KEY, (uint8_t)pct);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gain applied but persistence failed: %s",
                 esp_err_to_name(err));
    }
    return err;
}
```

- [ ] Update callers and web JSON.
- [ ] Do the same for radio prebuffer.
- [ ] Test set failure and commit failure separately.

---

## RH-S3-17 — Make station mutations transactional or explicitly partial [P1]

Current memory mutation plus failed save can report success.

Choose and document one behavior:

**Preferred transactional behavior:**

1. Lock.
2. Copy current store to temporary.
3. Apply mutation to temporary.
4. Persist temporary.
5. On success, replace live store.
6. On failure, leave live store unchanged.

- [ ] Add/update/remove/move use the same transaction helper.
- [ ] Return precise persistence errors.
- [ ] Web handler distinguishes invalid data from NVS failure.
- [ ] Tests inject save failure and verify live list unchanged.

---

## RH-S3-18 — Propagate controller and Wi-Fi persistence errors [P1]

- [ ] `ctrl_note_station()` returns persistence status.
- [ ] Wi-Fi credential reset checks erase and commit.
- [ ] HTTP responses expose failures.
- [ ] Do not log passwords.
- [ ] Tests cover commit failure.

---

## RH-S3-19 — Check deferred task/queue creation before HTTP success [P1]

Audit all web handlers, not only radio.

- [ ] Search `xTaskCreate` in `components/web_ui`.
- [ ] Search queue sends and deferred work functions.
- [ ] Move success response after successful scheduling.
- [ ] For network teardown operations that require replying first, reserve/allocate the work item before reply and document the sequence.
- [ ] Return stable JSON error codes.

---

# Phase 8 — Network/decoder robustness

## RH-S3-20 — Check `esp_http_client_init()` everywhere [P1]

- [ ] Playlist resolver.
- [ ] Stream task.
- [ ] Any station-validation fetch.

```c
esp_http_client_handle_t client = esp_http_client_init(&cfg);
if (!client) {
    set_radio_error(session, "http_client_alloc_failed", ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
}
```

- [ ] Add allocation-failure hook/test.

---

## RH-S3-21 — Stop unsupported codec sessions explicitly [P1]

- [ ] After headers, if codec is unknown, set last error.
- [ ] Close client.
- [ ] Decide whether to reconnect only when content type may change; otherwise terminate session.
- [ ] Do not fill compressed ring forever.
- [ ] Web status shows unsupported content type.
- [ ] Test unknown `Content-Type`.

---

# Phase 9 — Documentation and security baseline

## RH-DOC-01 — Correct stale I2S comments [P2]

- [ ] `esp_i2s_source/main/main.c` no longer says current phase is always-on 440 Hz or I2S master.
- [ ] `i2s_out/include/i2s_out.h` matches slave role and GPIO15/16/7.
- [ ] README/spec cross references are correct.
- [ ] No contradictory pin maps remain in active docs.

Search:

```bash
rg -n "I2S master|GPIO5|GPIO6|always-on|440 Hz test tone|slave RX" \
  esp_i2s_source esp_bt_audio_source \
  --glob '!archive/**' --glob '!build/**'
```

---

## RH-DOC-02 — Correct BT ownership documentation [P2]

- [ ] `bt_manager_internal.h` describes mutex protection.
- [ ] `BT_STATE_ACCESS_CONTRACT.md` is superseded or rewritten.
- [ ] Comments no longer claim direct callbacks run on BtAppTask.
- [ ] Remove obsolete request/semaphore usage examples.

---

## RH-SEC-01 — Stop exposing credentials by default [P2]

- [ ] Remove AP password from normal `/api/status` after provisioning.
- [ ] Remove credential/password logging in release path.
- [ ] Add build option to disable raw console endpoint.
- [ ] Document trusted-LAN/no-auth limitation.
- [ ] Ensure frontend still provides first-time provisioning instructions without continuously exposing secrets.

---

# Phase 10 — Full validation

## RH-TEST-01 — Offline host-test reliability

The S3 host build currently fetches Unity from GitHub if not installed.

- [ ] Prefer `find_package(unity)` with documented local install or vendor the allowed test dependency.
- [ ] Do not make internet access a hidden requirement for routine local tests.
- [ ] Do not commit arbitrary downloaded build output.

---

## RH-TEST-02 — Run S3 host suite with ASan

```bash
cd esp_i2s_source/test/host_test
rm -rf build
cmake -S . -B build -DENABLE_ASAN=ON
cmake --build build -- -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Record:

- [ ] total tests.
- [ ] pass/fail count.
- [ ] ASan result.
- [ ] any skipped concurrency tests and reason.

---

## RH-TEST-03 — Run WROOM host suite with ASan

```bash
cd esp_bt_audio_source/test/host_test
rm -rf build_host_tests
cmake -S . -B build_host_tests -DENABLE_ASAN=ON
cmake --build build_host_tests -- -j"$(nproc)"
ctest --test-dir build_host_tests --output-on-failure
```

- [ ] all tests pass.
- [ ] no leaks/use-after-free.
- [ ] BT concurrency tests pass.

---

## RH-TEST-04 — Build both firmware projects

```bash
. "$HOME/esp/esp-idf/export.sh"
idf.py -C esp_i2s_source build
idf.py -C esp_bt_audio_source build
```

- [ ] no new warnings from modified code.
- [ ] no component dependency cycles.
- [ ] firmware still fits partitions.

---

## RH-TEST-05 — Hardware regression tests

**Ask for confirmation before flashing.**

- [ ] Existing tone path and FFT purity unchanged.
- [ ] Browser radio playback works.
- [ ] UART VERSION/STATUS/VOLUME round trips.
- [ ] UART late-response timeout test does not corrupt next command.
- [ ] I2S stop works with WROOM clock absent.
- [ ] Exactly one I2S writer after restart.
- [ ] 100 radio play/stop cycles with no overlapping generation.
- [ ] 22.05 kHz stream continuous.
- [ ] 32 kHz stream continuous.
- [ ] Wi-Fi interruption causes clean rebuffer.
- [ ] Bluetooth disconnect/reconnect works.
- [ ] WROOM audio stop/restart never duplicates engine task.

---

## RH-TEST-06 — Soak and telemetry review

Run at least eight hours after all fixes.

Collect periodically:

- radio generation/state.
- stream and decoder task presence.
- compressed/PCM ring occupancy.
- reconnect and decode errors.
- I2S bytes/underruns/errors.
- BT connected/audio state.
- WROOM audio engine state.
- free heap/minimum free heap.

Acceptance:

- [ ] no reboot/panic.
- [ ] no task-count growth.
- [ ] no generation overlap.
- [ ] no impossible/torn counters.
- [ ] no permanent buffering after recoverable network stall.
- [ ] no silent persistence failures.

---

# Final completion checklist

## P0 blockers

- [ ] RH-S3-01 UART request ownership.
- [ ] RH-S3-02 radio generation isolation.
- [ ] RH-S3-03 decoder creation failure rollback.
- [ ] RH-WR-01 BT context synchronization.
- [ ] RH-WR-02 audio stop timeout ownership.

## P1 reliability

- [ ] RH-S3-04 through RH-S3-21.
- [ ] RH-WR-03 through RH-WR-05.

## Validation

- [ ] Both host ASan suites pass.
- [ ] Both firmware builds pass.
- [ ] Hardware regression evidence recorded.
- [ ] Soak test passes.
- [ ] Active documentation matches implementation.
- [ ] No task handle is forgotten while live.
- [ ] No async queue points to caller stack memory.
- [ ] No required operation reports false success.
