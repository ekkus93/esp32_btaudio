/*
 * radio_decode.c — decoder domain: drains the compressed ring, decodes
 * MP3/AAC via esp_audio_simple_dec, resamples to 44.1 kHz stereo, and fills
 * the decoded-PCM ring the I2S feeder drains. Split out of radio.c
 * (RADIO-2); see radio.h.
 */
#include "radio_internal.h"

#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "radio_resampler.h"
#include "esp_audio_simple_dec.h"
#include "esp_aac_dec.h"

static const char *TAG = "radio";

/* ---- Decoder helpers ---- */

static esp_audio_simple_dec_handle_t open_decoder(radio_codec_t codec)
{
    esp_audio_simple_dec_cfg_t cfg = {0};
    esp_aac_dec_cfg_t aac = { .aac_plus_enable = true };
    if (codec == RADIO_CODEC_AAC) {
        cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        cfg.dec_cfg = &aac;
        cfg.cfg_size = sizeof(aac);
    } else {
        cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    esp_audio_simple_dec_handle_t h = NULL;
    if (esp_audio_simple_dec_open(&cfg, &h) != ESP_AUDIO_ERR_OK) return NULL;
    return h;
}

/* ---- Decoder task (compressed ring -> decode -> resample -> PCM ring) ---- */
void decoder_task(void *arg)
{
    radio_session_t *s = arg;
    esp_audio_simple_dec_handle_t dec = NULL;
    radio_codec_t opened = RADIO_CODEC_UNKNOWN;
    radio_resampler_t rs;
    bool rs_ready = false;
    /* RH-S3-05: inbuf doubles as accumulation buffer for unconsumed decoder tail.
     * pending tracks unconsumed bytes that must be preserved across iterations. */
    #define DECODER_INPUT_CAP 4096
    static uint8_t inbuf[DECODER_INPUT_CAP];
    static uint8_t pcmbuf[16384];   /* one decoded frame */
    static int16_t rsbuf[8192];     /* resampled stereo frames (4096 max) */
    size_t pending = 0;

    /* 7.3: ENTERED fires immediately — before any operational check. */
    xEventGroupSetBits(s->events, RADIO_EVT_DECODER_ENTERED);

    while (session_should_run(s)) {
        radio_codec_t codec;
        xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
        codec = g_radio_codec;
        xSemaphoreGive(g_radio_ring_mtx);

        if (codec == RADIO_CODEC_UNKNOWN) { if (wait_or_stop(s, 50)) break; continue; }
        if (!dec || codec != opened) {
            if (dec) { esp_audio_simple_dec_close(dec); dec = NULL; }
            dec = open_decoder(codec);
            opened = codec;
            rs_ready = false;
            if (!dec) {
                xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
                g_radio_decode_errors++;
                xSemaphoreGive(g_radio_ring_mtx);
                if (wait_or_stop(s, 200)) break;
                continue;
            }
            ESP_LOGI(TAG, "decoder open: %s", radio_codec_str(codec));
            /* 7.3/7.8: READY — decoder has opened successfully. Try to
             * promote BUFFERING -> RUNNING (the stream worker may already
             * be READY too). */
            xEventGroupSetBits(s->events, RADIO_EVT_DECODER_READY);
            radio_try_publish_running(s);
        }

 /* RH-S3-05: read new compressed data after the pending tail. */
        if (pending < DECODER_INPUT_CAP) {
            size_t got = radio_read(inbuf + pending, DECODER_INPUT_CAP - pending);
            pending += got;
        }

        if (pending == 0) { if (wait_or_stop(s, 10)) break; continue; }

        esp_audio_simple_dec_raw_t raw = { .buffer = inbuf, .len = (uint32_t)pending };
        size_t consumed_total = 0;
        while (raw.len > 0 && session_should_run(s)) {
            esp_audio_simple_dec_out_t out = { .buffer = pcmbuf, .len = sizeof(pcmbuf) };
            esp_audio_err_t err = esp_audio_simple_dec_process(dec, &raw, &out);
            if (err == ESP_AUDIO_ERR_OK && out.decoded_size > 0) {
                esp_audio_simple_dec_info_t info;
                esp_audio_simple_dec_get_info(dec, &info);
                if (!rs_ready || (int)info.sample_rate != rs.src_rate || info.channel != rs.channels) {
                    radio_resampler_init(&rs, info.sample_rate, info.channel);
                    rs_ready = true;
                    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
                    g_radio_dec_rate = info.sample_rate;
                    g_radio_dec_ch = info.channel;
                    xSemaphoreGive(g_radio_ring_mtx);
                }
                /* RH-S3-04: loop resampler until all input frames consumed. */
                size_t in_frames = out.decoded_size / (size_t)(2 * (info.channel ? info.channel : 1));
                size_t channels = (info.channel == 1) ? 1 : 2;
                size_t offset = 0;
                while (offset < in_frames && session_should_run(s)) {
                    size_t used = 0;
                    size_t of = radio_resampler_run(&rs,
                                                    (const int16_t *)pcmbuf + channels * offset,
                                                    in_frames - offset,
                                                    rsbuf,
                                                    sizeof(rsbuf) / (2 * sizeof(int16_t)),
                                                    &used);
                    if (of) {
                        size_t bytes = of * 4;
                        size_t written = 0;
                        while (written < bytes && session_should_run(s)) {
                            size_t n = pcm_write((const uint8_t *)rsbuf + written, bytes - written);
                            if (n == 0) {
                                if (wait_or_stop(s, 5)) break;
                                continue;
                            }
                            written += n;
                        }
                        if (written != bytes) break;  /* PCM ring full */
                    } else if (used == 0) {
                        break;  /* resampler stalled */
                    }
                    offset += used;
                }
            } else if (err != ESP_AUDIO_ERR_OK) {
                xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
                g_radio_decode_errors++;
                xSemaphoreGive(g_radio_ring_mtx);
                /* 7.8: don't force consumed=1 — let the loop break naturally.
                 * The decoder may need more input, which is handled below. */
            }
            /* 7.8: validate decoder contract — consumed must not exceed len. */
            if (raw.consumed > raw.len) {
                ESP_LOGW(TAG, "decoder consumed > len (consumed=%u len=%u)", raw.consumed, raw.len);
                set_radio_error(RADIO_ERR_DECODER_CONTRACT, "decoder consumed beyond input");
                atomic_store_explicit(&s->stop_requested, true, memory_order_release);
                break;
            }
            if (raw.consumed == 0) break;   /* needs more input */
            consumed_total += raw.consumed;
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            raw.consumed = 0;
        }

        /* RH-S3-05: preserve unconsumed decoder tail in accumulation buffer. */
        if (consumed_total > 0) {
            pending -= consumed_total;
            memmove(inbuf, inbuf + consumed_total, pending);
        } else if (pending == DECODER_INPUT_CAP) {
            /* No progress and buffer full — resync by dropping one byte. */
            memmove(inbuf, inbuf + 1, (size_t)(pending - 1));
            pending--;
            xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
            g_radio_decode_errors++;
            xSemaphoreGive(g_radio_ring_mtx);
        }
    }

    if (dec) esp_audio_simple_dec_close(dec);
    xSemaphoreTake(g_radio_ring_mtx, portMAX_DELAY);
    g_radio_dec_rate = 0;
    xSemaphoreGive(g_radio_ring_mtx);
    s->decoder_task = NULL;
    xEventGroupSetBits(s->events, RADIO_EVT_DECODER_EXITED);
    vTaskDelete(NULL);
}
