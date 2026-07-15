/*
 * i2s_out (device glue) — I2S std SLAVE-TX channel + writer task (SIG-1b).
 * Built on the pure i2s_out_pump_once(); verified on hardware at SIG-1c.
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
 * RH-S3-08: finite-timeout sink + event-group signalling.
 *   i2s_channel_write() uses a 100 ms timeout so the writer loop can check
 *   a stop flag on each timeout — the stop path no longer polls blindly.
 *   Writer start/exit are signalled via event-group bits so stop() waits
 *   for the task to actually exit before disabling the channel.
 */
#include "i2s_out.h"

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs.h"

#include <stdatomic.h>
#include <string.h>

static const char *TAG = "i2s_out";

/* 512 bytes = 128 stereo s16 frames ≈ 2.9 ms at 44.1 kHz. */
#define I2S_OUT_BLOCK_BYTES 512
#define I2S_OUT_TASK_STACK  4096
#define I2S_OUT_TASK_PRIO   (configMAX_PRIORITIES - 2)
#define I2S_WRITE_TIMEOUT_MS 100
#define I2S_STOP_TIMEOUT_MS  500

/* Event-group bits */
#define I2S_EVT_WRITER_STARTED BIT(0)
#define I2S_EVT_WRITER_EXITED  BIT(1)

/* Lifecycle state */
typedef enum {
    I2S_STATE_IDLE = 0,
    I2S_STATE_STARTING,
    I2S_STATE_RUNNING,
    I2S_STATE_STOPPING,
    I2S_STATE_FAULTED,
} i2s_state_t;

static i2s_chan_handle_t s_tx_chan;
static pcm_ring_t        *s_ring;
static TaskHandle_t       s_writer_task;
static _Atomic bool       s_stop_requested;
static i2s_state_t        s_state;
static EventGroupHandle_t s_events;
static volatile bool      s_running; /* legacy compat — writer loop gate */
static i2s_out_stats_t    s_stats;
static portMUX_TYPE       s_stats_mux = portMUX_INITIALIZER_UNLOCKED;

/* i2s_sink — sink callback for i2s_out_pump_once.
 * Returns 0 on success, 1 on timeout (caller may retry or check stop),
 * <0 on error. */
static int i2s_sink(void *ctx, const uint8_t *data, size_t len)
{
    i2s_chan_handle_t ch = (i2s_chan_handle_t)ctx;
    size_t written = 0;
    esp_err_t err = i2s_channel_write(ch, data, len, &written,
                                       pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS));

    if (err == ESP_ERR_TIMEOUT) return 1;  /* timeout — check stop flag */
    if (err != ESP_OK || written != len) return -1;
    return 0;
}

static void writer_task(void *arg)
{
    (void)arg;
    uint8_t scratch[I2S_OUT_BLOCK_BYTES];

    /* Signal that the writer has started. */
    xEventGroupSetBits(s_events, I2S_EVT_WRITER_STARTED);

    while (s_running) {
        /* I2S-001: i2s_sink() -> i2s_channel_write() blocks up to
         * I2S_WRITE_TIMEOUT_MS. With no external BCLK/WS (WROOM32 absent or
         * not yet clocking), it blocks for the full timeout every call. That
         * must never happen with interrupts disabled: a critical section
         * held across it starves the interrupt watchdog and panics the
         * core. Snapshot stats out, run the pump (and its blocking write)
         * fully unlocked, then merge the (possibly stale-based) delta back
         * under a short critical section — only a struct copy, never a
         * driver call. */
        i2s_out_stats_t local;
        taskENTER_CRITICAL(&s_stats_mux);
        local = s_stats;
        taskEXIT_CRITICAL(&s_stats_mux);

        i2s_out_pump_once(s_ring, scratch, sizeof(scratch),
                          i2s_sink, s_tx_chan, &local);

        taskENTER_CRITICAL(&s_stats_mux);
        s_stats = local;
        taskEXIT_CRITICAL(&s_stats_mux);

        /* On stop request, break out to let the task exit. */
        if (atomic_load(&s_stop_requested)) break;
    }

    s_writer_task = NULL;
    xEventGroupSetBits(s_events, I2S_EVT_WRITER_EXITED);
    vTaskDelete(NULL);
}

esp_err_t i2s_out_init(size_t ring_capacity_bytes)
{
    if (s_ring) return ESP_ERR_INVALID_STATE;

    s_ring = pcm_ring_create(ring_capacity_bytes, /*use_psram=*/true);
    if (!s_ring) return ESP_ERR_NO_MEM;

    s_events = xEventGroupCreate();
    if (!s_events) {
        pcm_ring_destroy(s_ring);
        s_ring = NULL;
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
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_OUT_SAMPLE_RATE_HZ),
        /* DBG 32-in-32 experiment: slave-TX with 16-bit data in 32-bit slots
         * never shifts (FIFO half-width expansion suspect). Send full 32-bit
         * samples instead — tone in the TOP 16 bits — which the WROOM32's
         * 16-in-32 MSB-first extraction reads identically. Same BCLK. */
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
        return err;
    }

    ESP_LOGI(TAG, "init: SLAVE-TX 44.1kHz 16in32 stereo, bclk=%d ws=%d dout=%d, ring=%u B",
             I2S_OUT_GPIO_BCLK, I2S_OUT_GPIO_WS, I2S_OUT_GPIO_DOUT,
             (unsigned)ring_capacity_bytes);
    return ESP_OK;
}

esp_err_t i2s_out_start(void)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    /* Reject start if a writer is already running. */
    if (s_writer_task != NULL) return ESP_ERR_INVALID_STATE;

    atomic_store(&s_stop_requested, false);
    s_state = I2S_STATE_STARTING;

    esp_err_t err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        s_state = I2S_STATE_IDLE;
        return err;
    }

    s_running = true;
    if (xTaskCreate(writer_task, "i2s_out_wr", I2S_OUT_TASK_STACK, NULL,
                    I2S_OUT_TASK_PRIO, &s_writer_task) != pdPASS) {
        s_running = false;
        s_state = I2S_STATE_IDLE;
        i2s_channel_disable(s_tx_chan);
        return ESP_ERR_NO_MEM;
    }

    s_state = I2S_STATE_RUNNING;
    return ESP_OK;
}

esp_err_t i2s_out_stop(void)
{
    if (!s_running) return ESP_ERR_INVALID_STATE;
    s_state = I2S_STATE_STOPPING;

    /* Signal the writer to stop. */
    s_running = false;
    atomic_store(&s_stop_requested, true);

    /* Wait for the writer task to actually exit (signalled via event group). */
    if (s_events) {
        EventBits_t bits = xEventGroupWaitBits(s_events, I2S_EVT_WRITER_EXITED,
                                               pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(I2S_STOP_TIMEOUT_MS));
        if (!(bits & I2S_EVT_WRITER_EXITED)) {
            /* Timeout: the writer didn't exit in time. Retain handle. */
            ESP_LOGW(TAG, "writer stop timed out after %d ms", I2S_STOP_TIMEOUT_MS);
            s_state = I2S_STATE_FAULTED;
            return ESP_ERR_TIMEOUT;
        }
    }

    /* Clear the start bit for the next lifecycle. */
    if (s_events) {
        xEventGroupClearBits(s_events, I2S_EVT_WRITER_STARTED);
    }

    esp_err_t err = i2s_channel_disable(s_tx_chan);
    s_state = I2S_STATE_IDLE;
    s_running = false;
    return err;
}

size_t i2s_out_write(const uint8_t *data, size_t len)
{
    if (!s_ring) return 0;
    return pcm_ring_write(s_ring, data, len);
}

/* Pre-I2S software volume (0..100). The scaling itself is the pure
 * i2s_out_apply_gain(); the audio_out feeder reads this and applies it.
 * Persisted in NVS so it survives reboot; default a conservative 30%. */
#define I2S_GAIN_DEFAULT 30
#define I2S_GAIN_NVS_NS  "i2s"
#define I2S_GAIN_NVS_KEY "gain"

static volatile int s_gain_pct = I2S_GAIN_DEFAULT;

/* Load the persisted pre-I2S gain (default 30%). Call once at init. */
void i2s_out_gain_load(void)
{
    nvs_handle_t h;
    if (nvs_open(I2S_GAIN_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = I2S_GAIN_DEFAULT;
    if (nvs_get_u8(h, I2S_GAIN_NVS_KEY, &v) == ESP_OK) {
        s_gain_pct = (v > 100) ? 100 : v;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "pre-I2S gain loaded: %d%%", s_gain_pct);
}

esp_err_t i2s_out_set_gain(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_gain_pct = pct;

    nvs_handle_t h;
    esp_err_t err = nvs_open(I2S_GAIN_NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, I2S_GAIN_NVS_KEY, (uint8_t)pct);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gain applied but persistence failed: %s",
                 esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "pre-I2S gain set to %d%%", pct);
    return err;
}

int i2s_out_get_gain(void)
{
    return s_gain_pct;
}

void i2s_out_get_stats(i2s_out_stats_t *out)
{
    if (out) {
        taskENTER_CRITICAL(&s_stats_mux);
        *out = s_stats;
        taskEXIT_CRITICAL(&s_stats_mux);
    }
}
