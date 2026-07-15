/*
 * esp_i2s_source — ESP32-S3 internet-radio / tone source that streams audio
 * over I2S to the ESP32-WROOM32 A2DP bridge. See docs/SPEC.md.
 *
 * RADIO-2c: single audio_out feeder task arbitrates between radio decoded
 * PCM (when a stream is playing), tone generation, and silence. Packs PCM
 * 16-in-32 top-half for the WROOM32 slave RX, then pushes to i2s_out.
 * app_main prints an I2S stats beacon each second for TX clocking health.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

#include "driver/gpio.h"

#include "i2s_out.h"
#include "tone.h"
#include "bt_link.h"
#include "wifi_mgr.h"
#include "console.h"
#include "web_ui.h"
#include "radio.h"
#include "stations.h"
#include "ctrl.h"

static const char *TAG = "main";

/* Exact frequency meter: PCNT counts rising edges at full hardware speed
 * (no aliasing, unlike a gpio_get_level poller). Reports the WROOM32 master
 * clock's TRUE frequency and the BCLK:WS ratio the S3 slave actually sees. */
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
static float s3_measure_hz(int gpio, int ms)
{
    pcnt_unit_config_t ucfg = {
        .high_limit = 32000,
        .low_limit = -1,
        .flags = { .accum_count = true },
    };
    pcnt_unit_handle_t unit;
    if (pcnt_new_unit(&ucfg, &unit) != ESP_OK) return -1.0f;
    pcnt_chan_config_t ccfg = { .edge_gpio_num = gpio, .level_gpio_num = -1 };
    pcnt_channel_handle_t ch;
    if (pcnt_new_channel(unit, &ccfg, &ch) != ESP_OK) {
        pcnt_del_unit(unit);
        return -1.0f;
    }
    pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_unit_add_watch_point(unit, 32000);  /* required for accum_count */
    pcnt_unit_enable(unit);
    pcnt_unit_clear_count(unit);
    pcnt_unit_start(unit);
    int64_t t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(ms));
    int count = 0;
    pcnt_unit_get_count(unit, &count);
    int64_t dt = esp_timer_get_time() - t0;
    pcnt_unit_stop(unit);
    pcnt_unit_disable(unit);
    pcnt_del_channel(ch);
    pcnt_del_unit(unit);
    return (dt > 0) ? (float)count * 1e6f / (float)dt : -1.0f;
}

/* 256 KB PSRAM ring ≈ 1.5 s of 44.1 kHz stereo s16 — ample jitter absorption. */
#define I2S_RING_BYTES (256 * 1024)
#define AUDIO_OUT_FRAMES 256   /* 256 stereo frames = 1024 B per block */

/* RADIO-2c source arbitration: the single I2S feeder. Each block, pick the
 * active source — radio decoded PCM when a stream is playing (explicit user
 * action wins; underrun -> brief silence), else the tone (which is itself
 * silence when off) — pack it 16-in-32 top-half for the WROOM32 slave RX, and
 * push to i2s_out. Real source arbitration replacing the old always-on tone. */
static void audio_out_task(void *arg)
{
    (void)arg;
    static int16_t block[AUDIO_OUT_FRAMES * 2];
    static int32_t block32[AUDIO_OUT_FRAMES * 2];
    for (;;) {
        if (radio_audio_ready()) {
            size_t got = radio_pcm_read(block, AUDIO_OUT_FRAMES);
            if (got < AUDIO_OUT_FRAMES) {
                memset(&block[got * 2], 0, (AUDIO_OUT_FRAMES - got) * 2 * sizeof(int16_t));
            }
        } else {
            tone_fill(block, AUDIO_OUT_FRAMES);
        }
        /* Pre-I2S source trim (RADIO/CTRL volume): scale the mixed PCM before the
         * 16-in-32 pack. Independent of the WROOM32's post-mix VOLUME. */
        i2s_out_apply_gain(block, AUDIO_OUT_FRAMES * 2, i2s_out_get_gain());
        for (size_t i = 0; i < AUDIO_OUT_FRAMES * 2; i++) {
            block32[i] = (int32_t)block[i] * INT32_C(65536);   /* top half of the 32-bit slot */
        }
        const uint8_t *p = (const uint8_t *)block32;
        size_t total = sizeof(block32), off = 0;
        while (off < total) {
            size_t w = i2s_out_write(p + off, total - off);
            off += w;
            if (w == 0) vTaskDelay(1);   /* ring full — let the I2S consumer drain */
        }
    }
}

/* Initialise NVS exactly once (root CLAUDE.md contract mirrors esp_bt_audio_source). */
static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* LINK-1c: exercise the bt_link UART1 client against the WROOM32 UART2 command
 * port over the real wires (S3 TX=17->WROOM32 RX=16, S3 RX=18<-WROOM32 TX=17).
 * Sends VERSION/STATUS/VOLUME 40 and logs each round-trip as DIAG|BTLINK. This
 * is the first physical exercise of the dual-UART contract and doubles as a
 * boot-time link health check; non-fatal if the WROOM32 is absent/unwired. */
static const char *link_state_str(bt_link_cmd_state_t st)
{
    switch (st) {
    case BT_LINK_CMD_DONE_OK:  return "OK";
    case BT_LINK_CMD_DONE_ERR: return "ERR";
    case BT_LINK_CMD_TIMEOUT:  return "TIMEOUT";
    default:                   return "PENDING";
    }
}

static void link_selftest(void)
{
    static const char *const cmds[] = { "VERSION", "STATUS", "VOLUME 40" };
    esp_err_t err = bt_link_init(BT_LINK_DEFAULT_TIMEOUT_MS);
    if (err != ESP_OK) {
        printf("DIAG|BTLINK|INIT_FAIL|err=%s\n", esp_err_to_name(err));
        fflush(stdout);
        return;
    }
    int ok = 0;
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        bt_link_cmd_state_t st = BT_LINK_CMD_TIMEOUT;
        char result[BT_LINK_FIELD_MAX] = {0};
        bt_link_send(cmds[i], &st, result, sizeof(result), NULL, 0);
        if (st == BT_LINK_CMD_DONE_OK) ok++;
        printf("DIAG|BTLINK|cmd=%s,state=%s,result=%s\n",
               cmds[i], link_state_str(st), result);
        fflush(stdout);
    }
    printf("DIAG|BTLINK|SELFTEST|ok=%d/3\n", ok);
    fflush(stdout);
}

void app_main(void)
{
    init_nvs();

    size_t psram_bytes = 0;
#if CONFIG_SPIRAM
    psram_bytes = esp_psram_get_size();
#endif
    ESP_LOGI(TAG, "esp_i2s_source boot: idf=%s psram=%u KB free_heap=%u B",
             esp_get_idf_version(), (unsigned)(psram_bytes / 1024),
             (unsigned)esp_get_free_heap_size());
    printf("DIAG|BOOT|READY|psram_kb=%u,heap=%u\n",
           (unsigned)(psram_bytes / 1024), (unsigned)esp_get_free_heap_size());
    fflush(stdout);

    /* Skip I2S init when WROOM32 is absent to avoid hanging on slave enable */
    /* WIFI-1b: bring up WiFi — STA if provisioned, else AP provisioning. */
    ESP_LOGI(TAG, "Bringing up WiFi...");
    ESP_ERROR_CHECK(wifi_mgr_init());
    ESP_LOGI(TAG, "WiFi init done");

    /* LINK-1c: validate the UART command link to the WROOM32 once at boot. */
    link_selftest();

    /* LINK-1c: validate the UART command link to the WROOM32 once at boot. */
    link_selftest();

    /* WIFI-1b: bring up WiFi — STA if provisioned, else AP provisioning. */
    ESP_ERROR_CHECK(wifi_mgr_init());
    /* RADIO-1b: internet-radio stream client (256 KB PSRAM compressed ring). */
    ESP_ERROR_CHECK(radio_init(256 * 1024));
    /* RADIO-1c: NVS station presets (seeded on first boot). */
    ESP_ERROR_CHECK(stations_init());
    /* WIFI-1c: console for runtime provisioning (WIFI <ssid> <pass> / STATUS). */
    ESP_LOGI(TAG, "WiFi init done");
    ESP_ERROR_CHECK(console_start());
    /* RH-S3-10: ctrl_init() creates the mutex BEFORE web_ui_start(), so HTTP
     * handlers never access an uninitialised mutex. ctrl_start() spawns the
     * orchestrator task after. */
    ESP_ERROR_CHECK(ctrl_init());
    /* WEB-1a: HTTP server (embedded SPA + /api/status). */
    ESP_LOGI(TAG, "Web UI starting...");
    esp_err_t web_err = web_ui_start();
    if (web_err == ESP_OK) {
        ESP_LOGI(TAG, "Web UI started on port 80");
    } else {
        ESP_LOGE(TAG, "Web UI start failed: %d", web_err);
    }
    /* CTRL-1b: boot orchestrator — if autostart+sink configured, connect the
     * A2DP sink over bt_link and resume the last station with no interaction. */
    ESP_ERROR_CHECK(ctrl_start());

    /* I2S stats beacon: bytes_written must climb and underruns stay flat once
     * the ring primes; the PCNT freq meter confirms the WROOM32 master clock
     * (bclk≈2.8224 MHz, ws≈44.1 kHz, ratio≈64). Repeated so it survives
     * USB-JTAG re-enumeration. */
    i2s_out_stats_t st;
    for (;;) {
        i2s_out_get_stats(&st);
        float bclk_hz = s3_measure_hz(I2S_OUT_GPIO_BCLK, 200);
        float ws_hz = s3_measure_hz(I2S_OUT_GPIO_WS, 200);
        printf("DIAG|I2S|bytes=%llu,und=%llu,undev=%u,ringpeak=%u\n",
               (unsigned long long)st.bytes_written,
               (unsigned long long)st.underrun_bytes,
               (unsigned)st.underrun_events, (unsigned)st.ring_peak);
        printf("DIAG|I2SFREQ|bclk_hz=%.0f,ws_hz=%.0f,ratio=%.2f\n",
               bclk_hz, ws_hz, (ws_hz > 0) ? bclk_hz / ws_hz : -1.0f);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
