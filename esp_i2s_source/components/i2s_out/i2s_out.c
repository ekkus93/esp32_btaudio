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
 */
#include "i2s_out.h"

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "i2s_out";

/* 512 bytes = 128 stereo s16 frames ≈ 2.9 ms at 44.1 kHz. */
#define I2S_OUT_BLOCK_BYTES 512
#define I2S_OUT_TASK_STACK  4096
#define I2S_OUT_TASK_PRIO   (configMAX_PRIORITIES - 2)

static i2s_chan_handle_t s_tx_chan;
static pcm_ring_t       *s_ring;
static TaskHandle_t      s_writer_task;
static volatile bool     s_running;
static i2s_out_stats_t   s_stats;

/* Sink adapter: hand a full block to the I2S DMA (blocks until space, which
 * paces the writer at the I2S clock rate). */
static int i2s_sink(void *ctx, const uint8_t *data, size_t len)
{
    i2s_chan_handle_t ch = (i2s_chan_handle_t)ctx;
    size_t written = 0;
    esp_err_t err = i2s_channel_write(ch, data, len, &written, portMAX_DELAY);
    return (err == ESP_OK && written == len) ? 0 : -1;
}

static void writer_task(void *arg)
{
    (void)arg;
    uint8_t scratch[I2S_OUT_BLOCK_BYTES];
    while (s_running) {
        i2s_out_pump_once(s_ring, scratch, sizeof(scratch),
                          i2s_sink, s_tx_chan, &s_stats);
    }
    s_writer_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t i2s_out_init(size_t ring_capacity_bytes)
{
    if (s_ring) return ESP_ERR_INVALID_STATE;

    s_ring = pcm_ring_create(ring_capacity_bytes, /*use_psram=*/true);
    if (!s_ring) return ESP_ERR_NO_MEM;
    memset(&s_stats, 0, sizeof(s_stats));

    /* SLAVE: the WROOM32 master drives BCLK/WS; the S3 clocks data out on them. */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);  /* TX only */
    if (err != ESP_OK) {
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
    if (!s_tx_chan || s_running) return ESP_ERR_INVALID_STATE;
    esp_err_t err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) return err;

    s_running = true;
    if (xTaskCreate(writer_task, "i2s_out_wr", I2S_OUT_TASK_STACK, NULL,
                    I2S_OUT_TASK_PRIO, &s_writer_task) != pdPASS) {
        s_running = false;
        i2s_channel_disable(s_tx_chan);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t i2s_out_stop(void)
{
    if (!s_running) return ESP_ERR_INVALID_STATE;
    s_running = false;
    /* Let the writer task observe the flag and self-delete (block writes use
     * portMAX_DELAY, so give the current block time to drain). */
    for (int i = 0; i < 50 && s_writer_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return i2s_channel_disable(s_tx_chan);
}

size_t i2s_out_write(const uint8_t *data, size_t len)
{
    if (!s_ring) return 0;
    return pcm_ring_write(s_ring, data, len);
}

void i2s_out_get_stats(i2s_out_stats_t *out)
{
    if (out) *out = s_stats;
}
