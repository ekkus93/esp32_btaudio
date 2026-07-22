/*
 * i2s_out (device glue) — I2S std SLAVE-TX channel + writer task (SIG-1b).
 *
 * ROLE FLIP (2026-07-11): the S3 is the I2S *slave* transmitter and the
 * WROOM32 is the *master* receiver. The ESP32-classic (WROOM32) has a silicon
 * limitation that makes its I2S slave-RX never latch a present clock, so the
 * roles are inverted: the WROOM32 (reliable as master) generates BCLK+WS, and
 * the S3 (HW-v2, solid slave support) shifts data out synced to that clock.
 * Same 4 wires as before — only the BCLK/WS drive direction reverses.
 *
 * SPEC §3.3 interop contract (MUST match the WROOM32 master-RX exactly):
 *   Philips, data width 16-bit, SLOT width 32-bit, stereo, 44.1 kHz, MCLK
 *   unused. The 16-in-32 slot width is the subtle one — the WROOM32 uses
 *   ws_width 32, so both sides must pad 16-bit samples into 32-bit slots or
 *   the audio garbles. As a slave, the S3 follows the WROOM32's BCLK/WS; the
 *   sample rate below only sets the internal clock divider.
 *
 * TODO Phase 3 lifecycle/data-commit rewrite:
 *   - writer_task() keeps a persistent "pending" block across loop
 *     iterations (peek -> write -> i2s_pending_advance() -> consume only
 *     the real bytes actually accepted). A timeout or partial write never
 *     loses ring bytes (I2S-002).
 *   - No critical section is ever held across i2s_channel_write() (I2S-001,
 *     hardware-confirmed: holding one here panics the interrupt watchdog
 *     when the external clock is absent, since the write blocks its full
 *     timeout with interrupts disabled).
 *   - init/start/stop/deinit are serialized by a lifecycle mutex and are
 *     idempotent (I2S-003/I2S-004/I2S-005/I2S-006/I2S-007).
 *   - The ring is PSRAM-required, never silently falls back (I2S-014).
 *   - i2s_out_set_gain() fixes the uninitialized-NVS-handle close and
 *     publishes to RAM only after a successful commit (I2S-011/I2S-012/I2S-013).
 */
#include "i2s_out.h"

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include <stdio.h>
#include <inttypes.h>
#endif

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "i2s_out";

/* 2048 bytes = 256 stereo 32-bit-slot frames ≈ 5.8 ms at 44.1 kHz — one
 * audio_out_task block. (Was 512 B = 64 frames = 1.45 ms; ~690 driver
 * calls/sec of overhead for no benefit.) */
#define I2S_OUT_BLOCK_BYTES 2048
#define I2S_OUT_TASK_STACK  8192
#define I2S_OUT_TASK_PRIO   (configMAX_PRIORITIES - 2)
#define I2S_WRITE_TIMEOUT_MS 100
#define I2S_STOP_TIMEOUT_MS  500

/* Event-group bits (TODO 3.2): ENTERED (task alive) is distinct from READY
 * (writer has published its first operational state: RUNNING after a
 * successful write, WAITING_FOR_CLOCK after a timeout, or FAULTED after a
 * non-timeout error) — i2s_out_start() must wait for READY, not merely
 * ENTERED, before it can trust the published state. */
#define I2S_EVT_WRITER_ENTERED BIT(0)
#define I2S_EVT_WRITER_READY   BIT(1)
#define I2S_EVT_WRITER_EXITED  BIT(2)

static i2s_chan_handle_t     s_tx_chan;
static pcm_ring_t            *s_ring;
static TaskHandle_t          s_writer_task;
static _Atomic bool          s_stop_requested;
static _Atomic i2s_out_state_t s_state = I2S_STATE_UNINITIALIZED;
static EventGroupHandle_t    s_events;
static SemaphoreHandle_t     s_lifecycle_mtx;
static i2s_out_stats_t       s_stats;
static portMUX_TYPE          s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
/* Protected by s_lifecycle_mtx. True only after i2s_channel_enable()
 * returned ESP_OK; becomes false only after i2s_channel_disable() returns
 * ESP_OK. Tracked independently of s_state so deinit's "channel disabled"
 * check never has to infer it from a lifecycle state name. */
static bool                  s_channel_enabled;

/* Publish a non-terminal state (RUNNING/WAITING_FOR_CLOCK/STARTING/
 * STOPPING/IDLE) without touching last_error. */
static void i2s_set_state(i2s_out_state_t state)
{
    atomic_store(&s_state, state);
}

/* Publish a terminal-fault state and record the error that caused it. A
 * missing-clock timeout is NOT terminal — it goes through i2s_set_state(),
 * not this, so last_error keeps reporting the most recent *real* driver/
 * lifecycle failure (TODO 3.6 / spec §6.6). */
static void i2s_set_faulted(esp_err_t err)
{
    atomic_store(&s_state, I2S_STATE_FAULTED);
    taskENTER_CRITICAL(&s_stats_mux);
    s_stats.last_error = (int)err;
    taskEXIT_CRITICAL(&s_stats_mux);
}

/* Short critical section: struct-field updates only, never a driver call
 * while locked (TODO 3.6). */
static void i2s_stats_record_write(esp_err_t err, size_t requested,
                                   size_t written, size_t real_written)
{
    taskENTER_CRITICAL(&s_stats_mux);
    s_stats.bytes_written += written;
    if (written > real_written) {
        s_stats.underrun_bytes += (written - real_written);
        s_stats.underrun_events += 1;
    }
    if (written > 0 && written < requested) {
        s_stats.partial_writes += 1;
    }
    if (err == ESP_ERR_TIMEOUT) {
        s_stats.write_timeouts += 1;
    } else if (err != ESP_OK) {
        s_stats.write_errors += 1;
        s_stats.last_error = (int)err;
    }
    taskEXIT_CRITICAL(&s_stats_mux);
}

/* Writer pending-block state. File-static (not writer_task locals) so host
 * tests can drive single loop iterations via i2s_test_writer_step() — the
 * 2026-07-22 DMA-starvation bug lived in exactly this loop, and it survived
 * every gate because the loop body never executed in host tests. Reset by
 * i2s_out_start() before the task is (re)created; single-writer, unlocked. */
typedef struct {
    uint8_t buf[I2S_OUT_BLOCK_BYTES];
    size_t  len;
    size_t  real;
    bool    first_result;
} writer_pending_t;
static writer_pending_t s_wr;

#ifdef UNIT_TEST
/* Count of 100 ms "no clock" backoff naps — the regression tests assert a
 * progress-timeout (written > 0) never naps and a zero-byte timeout naps
 * exactly once. */
static unsigned s_test_backoff_naps;
unsigned i2s_test_backoff_naps(void) { return s_test_backoff_naps; }
#endif

#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
/* DIAG|I2SWR writer-throughput telemetry (every 5 s). A healthy slave-TX
 * writer shows rate ~= the wire rate (352,800 B/s at 44.1 kHz 32-bit
 * stereo), to_zero=to_part=0, busy >90%. This line is what caught the
 * 2026-07-22 double-tick-conversion bug (writer at 54% of wire rate, DMA
 * replaying stale buffers as audible static) — a byte-rate check catches
 * that whole failure class; state/underrun counters alone did not. */
static struct {
    int64_t  last_us;
    uint32_t ok, to_zero, to_part, bytes;
    int64_t  write_us, write_max_us;
} s_wr_diag;

static void writer_diag_record(esp_err_t err, size_t written, int64_t write_dt_us)
{
    s_wr_diag.write_us += write_dt_us;
    if (write_dt_us > s_wr_diag.write_max_us) s_wr_diag.write_max_us = write_dt_us;
    s_wr_diag.bytes += (uint32_t)written;
    if (err == ESP_ERR_TIMEOUT) {
        if (written == 0) s_wr_diag.to_zero++; else s_wr_diag.to_part++;
    } else if (err == ESP_OK) {
        s_wr_diag.ok++;
    }
    int64_t now = esp_timer_get_time();
    if (now - s_wr_diag.last_us >= 5000000) {
        int64_t elapsed = now - s_wr_diag.last_us;
        printf("DIAG|I2SWR|rate=%luB/s,ok=%lu,to_zero=%lu,to_part=%lu,busy=%lu%%,maxw=%" PRId64 "ms\n",
               (unsigned long)((int64_t)s_wr_diag.bytes * 1000000 / elapsed),
               (unsigned long)s_wr_diag.ok, (unsigned long)s_wr_diag.to_zero,
               (unsigned long)s_wr_diag.to_part,
               (unsigned long)(s_wr_diag.write_us * 100 / elapsed),
               s_wr_diag.write_max_us / 1000);
        fflush(stdout);
        s_wr_diag.last_us = now;
        s_wr_diag.ok = s_wr_diag.to_zero = s_wr_diag.to_part = s_wr_diag.bytes = 0;
        s_wr_diag.write_us = s_wr_diag.write_max_us = 0;
    }
}
#endif

/* One writer-loop iteration: peek/refill the pending block, push it at the
 * DMA, advance by what was accepted, publish state. Returns false only on a
 * terminal fault (the task loop must exit). Host-testable via
 * i2s_test_writer_step(). */
static bool writer_step(void)
{
    if (s_wr.len == 0) {
        s_wr.real = pcm_ring_peek(s_ring, s_wr.buf, sizeof(s_wr.buf));
        if (s_wr.real < sizeof(s_wr.buf)) {
            memset(s_wr.buf + s_wr.real, 0, sizeof(s_wr.buf) - s_wr.real);
        }
        s_wr.len = sizeof(s_wr.buf);
    }

    /* No critical section here: this call blocks up to
     * I2S_WRITE_TIMEOUT_MS. Holding a spinlock across it disables
     * interrupts for that whole duration and panics the interrupt
     * watchdog when the external clock is absent (I2S-001). */
    size_t requested = s_wr.len;
    size_t written = 0;
#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
    int64_t t0 = esp_timer_get_time();
#endif
    /* NOTE: i2s_channel_write() takes MILLISECONDS, not ticks — it converts
     * internally. Passing pdMS_TO_TICKS() here double-converts (found live
     * 2026-07-22: at CONFIG_FREERTOS_HZ=100 the intended 100 ms became
     * 1 tick, causing constant spurious timeouts, 100 ms "no clock" naps, a
     * DMA starved to ~1/3 wire rate, and stale-buffer replay heard as
     * static). Guarded by test_write_timeout_is_milliseconds. */
    esp_err_t err = i2s_channel_write(s_tx_chan, s_wr.buf, s_wr.len, &written,
                                      I2S_WRITE_TIMEOUT_MS);
#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
    writer_diag_record(err, written, esp_timer_get_time() - t0);
#endif
    if (written > s_wr.len) {
        written = s_wr.len;   /* defensive: driver must not exceed request */
        err = ESP_FAIL;
    }

    size_t real_accepted = i2s_pending_advance(s_wr.buf, &s_wr.len, &s_wr.real, written);
    if (real_accepted > 0) {
        size_t consumed = pcm_ring_consume(s_ring, real_accepted);
        configASSERT(consumed == real_accepted);
    }

    i2s_stats_record_write(err, requested, written, real_accepted);

    if (err == ESP_ERR_TIMEOUT) {
        /* Distinguish two very different timeouts (found live 2026-07-22:
         * conflating them starved the DMA to ~1/3 of the wire rate and the
         * hardware replayed stale buffers as audible static):
         *
         *   written == 0  ->  the DMA accepted NOTHING for the whole wait:
         *     no external clock. Retain pending, back off, retry — never
         *     faster than 10 Hz (SPEC §6.4). Not a terminal error:
         *     last_error is untouched (I2S-006 / spec §6.6).
         *
         *   written > 0   ->  the DMA drained data during the wait, so the
         *     WROOM32's clock is provably present — the block merely didn't
         *     complete within the window. Keep pumping IMMEDIATELY: any
         *     sleep here lets the DMA wrap and replay stale audio (a 100 ms
         *     nap = ~3 full replays of the default 6x240-frame DMA buffer). */
        if (written == 0) {
            i2s_set_state(I2S_STATE_WAITING_FOR_CLOCK);
            if (s_wr.first_result) {
                xEventGroupSetBits(s_events, I2S_EVT_WRITER_READY);
                s_wr.first_result = false;
            }
#ifdef UNIT_TEST
            s_test_backoff_naps++;
#endif
            vTaskDelay(pdMS_TO_TICKS(100));
            return true;
        }
        i2s_set_state(I2S_STATE_RUNNING);
        if (s_wr.first_result) {
            xEventGroupSetBits(s_events, I2S_EVT_WRITER_READY);
            s_wr.first_result = false;
        }
        return true;
    }
    if (err != ESP_OK) {
        i2s_set_faulted(err);
        if (s_wr.first_result) {
            xEventGroupSetBits(s_events, I2S_EVT_WRITER_READY);
            s_wr.first_result = false;
        }
        return false;
    }
    i2s_set_state(I2S_STATE_RUNNING);
    if (s_wr.first_result) {
        xEventGroupSetBits(s_events, I2S_EVT_WRITER_READY);
        s_wr.first_result = false;
    }
    return true;
}

#ifdef UNIT_TEST
bool i2s_test_writer_step(void) { return writer_step(); }
#endif

static void writer_task(void *arg)
{
    (void)arg;
    xEventGroupSetBits(s_events, I2S_EVT_WRITER_ENTERED);

    while (!atomic_load(&s_stop_requested)) {
        if (!writer_step()) {
            break;
        }
    }

    /* Lifecycle code (join_writer_locked()) clears s_writer_task after
     * observing EXITED — the worker is not the source of truth for safe
     * reclamation (TODO 3.2). Resources (s_ring/s_tx_chan/s_events) are
     * never freed here. */
    xEventGroupSetBits(s_events, I2S_EVT_WRITER_EXITED);
    vTaskDelete(NULL);
}

/* Wait up to `timeout` for the writer to signal EXITED. Must be called with
 * s_lifecycle_mtx held. On success, clears s_writer_task (lifecycle code
 * owns this handle, not the worker — TODO 3.2/3.3) and returns ESP_OK. On
 * timeout, publishes FAULTED_JOIN_PENDING and returns ESP_ERR_TIMEOUT
 * WITHOUT touching s_writer_task/s_ring/s_tx_chan/s_events — a timeout
 * means ownership is unresolved, never permission to force-free (spec
 * §4.1). Safe to call with no writer task (returns ESP_OK immediately). */
static esp_err_t join_writer_locked(TickType_t timeout)
{
    if (!s_writer_task) {
        return ESP_OK;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, I2S_EVT_WRITER_EXITED,
                                           pdFALSE, pdTRUE, timeout);
    if ((bits & I2S_EVT_WRITER_EXITED) == 0) {
        atomic_store(&s_state, I2S_STATE_FAULTED_JOIN_PENDING);
        return ESP_ERR_TIMEOUT;
    }

    s_writer_task = NULL;
    return ESP_OK;
}

/* ---- Test injection hooks (TODO 3.7) — see i2s_out.h. ---- */
void i2s_test_inject_writer_state(i2s_out_state_t state, esp_err_t err)
{
    if (err == ESP_OK) {
        i2s_set_state(state);
    } else {
        i2s_set_faulted(err);
    }
}

void i2s_test_inject_writer_bits(uint32_t bits)
{
    if (s_events) {
        xEventGroupSetBits(s_events, (EventBits_t)bits);
    }
}

#ifdef UNIT_TEST
void i2s_test_reset_module_state(void)
{
    memset(&s_wr, 0, sizeof(s_wr));
    s_wr.first_result = true;
    s_test_backoff_naps = 0;
    if (s_tx_chan) {
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    if (s_ring) {
        pcm_ring_destroy(s_ring);
        s_ring = NULL;
    }
    if (s_lifecycle_mtx) {
        vSemaphoreDelete(s_lifecycle_mtx);
        s_lifecycle_mtx = NULL;
    }
    s_writer_task = NULL;
    s_channel_enabled = false;
    atomic_store(&s_stop_requested, false);
    atomic_store(&s_state, I2S_STATE_UNINITIALIZED);
    memset(&s_stats, 0, sizeof(s_stats));
}
#endif

esp_err_t i2s_out_init(size_t ring_capacity_bytes)
{
    if (!s_lifecycle_mtx) {
        s_lifecycle_mtx = xSemaphoreCreateMutex();
        if (!s_lifecycle_mtx) return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_lifecycle_mtx, portMAX_DELAY);

    i2s_out_state_t state = atomic_load(&s_state);
    if (state != I2S_STATE_UNINITIALIZED) {
        xSemaphoreGive(s_lifecycle_mtx);
        return (state == I2S_STATE_IDLE) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    s_ring = pcm_ring_create(ring_capacity_bytes, PCM_RING_PSRAM_REQUIRED);
    if (!s_ring) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_NO_MEM;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        pcm_ring_destroy(s_ring);
        s_ring = NULL;
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_NO_MEM;
    }

    memset(&s_stats, 0, sizeof(s_stats));
    i2s_out_gain_load();   /* restore persisted pre-I2S volume (default 30%) */

    /* SLAVE: the WROOM32 master drives BCLK/WS; the S3 clocks data out on them. */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);  /* TX only */
    if (err != ESP_OK) {
        vEventGroupDelete(s_events);
        s_events = NULL;
        pcm_ring_destroy(s_ring);
        s_ring = NULL;
        xSemaphoreGive(s_lifecycle_mtx);
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_OUT_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_OUT_GPIO_BCLK,
            .ws   = I2S_OUT_GPIO_WS,
            .dout = I2S_OUT_GPIO_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    /* SPEC §3.3: 16-bit data padded into 32-bit slots to match the WROOM32
     * master EXACTLY. The WROOM32 (i2s_manager.c) sets slot_bit_width=32,
     * ws_width=32, ws_pol=false, bit_shift=true. The Philips default macro
     * leaves ws_width = data_bit_width (16), so WS would toggle every 16 BCLK
     * while the master drives every 32 → framing never aligns and the audio
     * garbles. Set ws_width=32 to match. (Slot framing must agree regardless
     * of which side is master.) */
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.ws_width = 32;
    std_cfg.slot_cfg.ws_pol = false;
    std_cfg.slot_cfg.bit_shift = true;
    /* SLAVE clocking margin: the slave samples the external BCLK with its
     * internal clock, which must be >= 8x BCLK (esp-idf #9513). The default
     * bclk_div=8 puts us exactly AT the minimum through a fractional divider;
     * use 16 for real margin (internal clk ~45 MHz vs 2.82 MHz BCLK). Both 8
     * and 16 verified working via laptop A2DP FFT (100% purity); 16 kept for
     * margin. */
    std_cfg.clk_cfg.bclk_div = 16;

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        vEventGroupDelete(s_events);
        s_events = NULL;
        pcm_ring_destroy(s_ring);
        s_ring = NULL;
        xSemaphoreGive(s_lifecycle_mtx);
        return err;
    }

    atomic_store(&s_state, I2S_STATE_IDLE);

    ESP_LOGI(TAG, "init: SLAVE-TX 44.1kHz 16in32 stereo, bclk=%d ws=%d dout=%d, ring=%u B",
             I2S_OUT_GPIO_BCLK, I2S_OUT_GPIO_WS, I2S_OUT_GPIO_DOUT,
             (unsigned)ring_capacity_bytes);
    xSemaphoreGive(s_lifecycle_mtx);
    return ESP_OK;
}

esp_err_t i2s_out_start(void)
{
    if (!s_lifecycle_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lifecycle_mtx, portMAX_DELAY);

    i2s_out_state_t state = atomic_load(&s_state);
    if (state == I2S_STATE_RUNNING || state == I2S_STATE_WAITING_FOR_CLOCK) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_OK;  /* idempotent while already running */
    }
    if (state != I2S_STATE_IDLE) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupClearBits(s_events,
                         I2S_EVT_WRITER_ENTERED | I2S_EVT_WRITER_READY | I2S_EVT_WRITER_EXITED);
    taskENTER_CRITICAL(&s_stats_mux);
    s_stats.last_error = 0;   /* clear for this attempt (TODO 3.4) */
    taskEXIT_CRITICAL(&s_stats_mux);
    i2s_set_state(I2S_STATE_STARTING);

    esp_err_t err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        i2s_set_state(I2S_STATE_IDLE);
        xSemaphoreGive(s_lifecycle_mtx);
        return err;
    }
    s_channel_enabled = true;

    atomic_store(&s_stop_requested, false);
    /* Fresh pending state for this writer generation (see writer_pending_t). */
    memset(&s_wr, 0, sizeof(s_wr));
    s_wr.first_result = true;
#if defined(ESP_PLATFORM) && !defined(UNIT_TEST)
    memset(&s_wr_diag, 0, sizeof(s_wr_diag));
    s_wr_diag.last_us = esp_timer_get_time();
#endif
    if (xTaskCreate(writer_task, "i2s_out_wr", I2S_OUT_TASK_STACK, NULL,
                    I2S_OUT_TASK_PRIO, &s_writer_task) != pdPASS) {
        esp_err_t disable_err = i2s_channel_disable(s_tx_chan);
        if (disable_err == ESP_OK) {
            s_channel_enabled = false;
            i2s_set_state(I2S_STATE_IDLE);
            xSemaphoreGive(s_lifecycle_mtx);
            return ESP_ERR_NO_MEM;
        }
        i2s_set_faulted(disable_err);
        xSemaphoreGive(s_lifecycle_mtx);
        return disable_err;
    }

    /* Wait for the writer's first operational-state publication, not
     * merely task entry (TODO 3.2/3.4) — i2s_out_start() must never
     * unconditionally store RUNNING. */
    EventBits_t bits = xEventGroupWaitBits(
        s_events, I2S_EVT_WRITER_READY | I2S_EVT_WRITER_EXITED,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));

    if ((bits & (I2S_EVT_WRITER_READY | I2S_EVT_WRITER_EXITED)) == 0) {
        /* Writer hasn't published anything yet. Cancel the attempt: request
         * stop, join, and disable — never leave STARTING indefinitely. */
        atomic_store(&s_stop_requested, true);
        esp_err_t join_err = join_writer_locked(pdMS_TO_TICKS(I2S_STOP_TIMEOUT_MS));
        if (join_err != ESP_OK) {
            /* join_writer_locked() already published FAULTED_JOIN_PENDING
             * and retained everything. */
            xSemaphoreGive(s_lifecycle_mtx);
            return ESP_ERR_TIMEOUT;
        }
        esp_err_t disable_err = ESP_OK;
        if (s_channel_enabled) {
            disable_err = i2s_channel_disable(s_tx_chan);
            if (disable_err == ESP_OK) {
                s_channel_enabled = false;
            }
        }
        if (disable_err == ESP_OK) {
            i2s_set_state(I2S_STATE_IDLE);
        } else {
            i2s_set_faulted(disable_err);
        }
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_TIMEOUT;
    }

    /* The writer published a state before we timed out — read it rather
     * than assuming success (TODO 3.4). */
    i2s_out_state_t observed = atomic_load(&s_state);
    esp_err_t result;
    if (observed == I2S_STATE_RUNNING || observed == I2S_STATE_WAITING_FOR_CLOCK) {
        result = ESP_OK;
    } else {
        taskENTER_CRITICAL(&s_stats_mux);
        result = (esp_err_t)s_stats.last_error;
        taskEXIT_CRITICAL(&s_stats_mux);
        if (result == ESP_OK) {
            result = ESP_FAIL;   /* defensive: FAULTED must have a recorded cause */
        }
    }
    xSemaphoreGive(s_lifecycle_mtx);
    return result;
}

esp_err_t i2s_out_stop(void)
{
    if (!s_lifecycle_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lifecycle_mtx, portMAX_DELAY);

    i2s_out_state_t state = atomic_load(&s_state);
    if (state == I2S_STATE_IDLE) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_OK;  /* idempotent while already stopped */
    }
    /* STARTING and FAULTED_JOIN_PENDING are legal here too: STARTING lets a
     * caller cancel an in-progress start, and JOIN_PENDING lets a caller
     * retry a previously timed-out join (TODO 3.5). */
    if (state != I2S_STATE_STARTING && state != I2S_STATE_RUNNING &&
        state != I2S_STATE_WAITING_FOR_CLOCK && state != I2S_STATE_FAULTED &&
        state != I2S_STATE_FAULTED_JOIN_PENDING) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_INVALID_STATE;
    }

    i2s_set_state(I2S_STATE_STOPPING);
    atomic_store(&s_stop_requested, true);

    /* join_writer_locked() clears s_writer_task on success and publishes
     * FAULTED_JOIN_PENDING (retaining every resource) on timeout — including
     * when this is a retry of an already-JOIN_PENDING state. */
    esp_err_t join_err = join_writer_locked(pdMS_TO_TICKS(I2S_STOP_TIMEOUT_MS));
    if (join_err != ESP_OK) {
        ESP_LOGW(TAG, "writer stop timed out after %d ms", I2S_STOP_TIMEOUT_MS);
        xSemaphoreGive(s_lifecycle_mtx);
        return join_err;
    }

    xEventGroupClearBits(s_events, I2S_EVT_WRITER_ENTERED | I2S_EVT_WRITER_READY);

    esp_err_t err = ESP_OK;
    if (s_channel_enabled) {
        err = i2s_channel_disable(s_tx_chan);
        if (err == ESP_OK) {
            s_channel_enabled = false;
        }
    }
    if (err == ESP_OK) {
        i2s_set_state(I2S_STATE_IDLE);
    } else {
        /* Channel-enabled stays true — a later stop()/recovery retry can
         * attempt disable() again (TODO 3.5/6.4). */
        i2s_set_faulted(err);
    }
    xSemaphoreGive(s_lifecycle_mtx);
    return err;
}

esp_err_t i2s_out_deinit(void)
{
    if (!s_lifecycle_mtx) return ESP_OK;
    xSemaphoreTake(s_lifecycle_mtx, portMAX_DELAY);

    i2s_out_state_t state = atomic_load(&s_state);
    if (state == I2S_STATE_UNINITIALIZED) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_OK;
    }
    /* Legal only once the writer's exit is acknowledged, the lifecycle
     * owner has cleared its task handle, and the channel is confirmed
     * disabled (TODO 3.5 / spec §6.5) — never while a worker might still
     * touch s_ring/s_tx_chan/s_events. */
    if (state != I2S_STATE_IDLE || s_writer_task || s_channel_enabled) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_tx_chan) {
        esp_err_t err = i2s_del_channel(s_tx_chan);
        if (err != ESP_OK) {
            /* Propagate the failure; do not touch s_events/s_ring — the
             * channel handle itself is still whatever i2s_del_channel()
             * left it as (TODO 3.5). */
            xSemaphoreGive(s_lifecycle_mtx);
            return err;
        }
        s_tx_chan = NULL;
    }
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    if (s_ring) {
        pcm_ring_destroy(s_ring);
        s_ring = NULL;
    }
    atomic_store(&s_state, I2S_STATE_UNINITIALIZED);

    xSemaphoreGive(s_lifecycle_mtx);
    return ESP_OK;
}

size_t i2s_out_write(const uint8_t *data, size_t len)
{
    if (!s_ring) return 0;
    return pcm_ring_write(s_ring, data, len);
}

i2s_out_state_t i2s_out_get_state(void)
{
    return atomic_load(&s_state);
}

/* Pre-I2S software volume (0..100). The scaling itself is the pure
 * i2s_out_apply_gain(); the audio_out feeder reads this and applies it.
 * Persisted in NVS so it survives reboot; default a conservative 30%. */
#define I2S_GAIN_DEFAULT 30
#define I2S_GAIN_NVS_NS  "i2s"
#define I2S_GAIN_NVS_KEY "gain"

static _Atomic int s_gain_pct = I2S_GAIN_DEFAULT;

/* Load the persisted pre-I2S gain (default 30%). Call once at init. */
void i2s_out_gain_load(void)
{
    nvs_handle_t h;
    if (nvs_open(I2S_GAIN_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = I2S_GAIN_DEFAULT;
    if (nvs_get_u8(h, I2S_GAIN_NVS_KEY, &v) == ESP_OK) {
        atomic_store(&s_gain_pct, (v > 100) ? 100 : v);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "pre-I2S gain loaded: %d%%", atomic_load(&s_gain_pct));
}

esp_err_t i2s_out_set_gain(int pct)
{
    if (pct < 0 || pct > 100) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(I2S_GAIN_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gain %d%% not applied: nvs_open failed: %s", pct, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, I2S_GAIN_NVS_KEY, (uint8_t)pct);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gain %d%% not applied: persistence failed: %s", pct, esp_err_to_name(err));
        return err;
    }

    /* Publish to RAM only after a successful commit — no reboot-dependent
     * runtime/NVS divergence (I2S-012). */
    atomic_store(&s_gain_pct, pct);
    ESP_LOGI(TAG, "pre-I2S gain set to %d%%", pct);
    return ESP_OK;
}

int i2s_out_get_gain(void)
{
    return atomic_load(&s_gain_pct);
}

void i2s_out_get_stats(i2s_out_stats_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    taskEXIT_CRITICAL(&s_stats_mux);
    out->state = atomic_load(&s_state);
    out->ring_used = s_ring ? pcm_ring_used(s_ring) : 0;
    out->ring_capacity = s_ring ? pcm_ring_capacity(s_ring) : 0;
    out->ring_peak = s_ring ? pcm_ring_peak_used(s_ring) : 0;
}
