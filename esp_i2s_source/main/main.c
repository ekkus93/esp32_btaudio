/*
 * esp_i2s_source — ESP32-S3 internet-radio / tone source that streams audio
 * over I2S to the ESP32-WROOM32 A2DP bridge. See docs/SPEC.md.
 *
 * Boot order (ESP_I2S_SOURCE_FIX_RESPONSES_V2_2026-07-15.md, answer #1):
 * NVS -> boot diagnostics/boot_id -> i2s_out init+start -> bt_link_init ->
 * radio_init -> stations_init -> ctrl_init -> audio_out task (only once i2s
 * and radio are both initialized) -> wifi_mgr_init -> console_start ->
 * web_ui_start (optional) -> ctrl_start -> BOOT_COMPLETE marker -> async
 * read-only WROOM health probe -> low-rate diagnostics loop. Every
 * initializer runs exactly once; only NVS/heap/platform failures are fatal
 * (SPEC §5.2) — every other component failure is logged and the boot
 * continues in a degraded-but-controllable state.
 *
 * RADIO-2c: single audio_out feeder task arbitrates between radio decoded
 * PCM (when a stream is playing), tone generation, and silence. Packs PCM
 * 16-in-32 top-half for the WROOM32 slave RX, then pushes to i2s_out.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
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

/* 256 KB PSRAM ring ≈ 1.5 s of 44.1 kHz stereo s16 — ample jitter absorption. */
#define I2S_RING_BYTES   (256 * 1024)
#define RADIO_RING_BYTES (256 * 1024)
#define AUDIO_OUT_FRAMES 256   /* 256 stereo frames = 1024 B per block */

/* ---- boot step logging (SPEC §5.2: track components, don't abort blindly) ---- */

typedef struct {
    bool i2s_ok;
    bool bt_link_ok;
    bool radio_ok;
    bool stations_ok;
    bool ctrl_ok;
    bool audio_task_ok;
    bool wifi_ok;
    bool console_ok;
    bool web_ok;
    bool ctrl_start_ok;
} boot_status_t;

static bool log_boot_step(const char *name, esp_err_t err)
{
    printf("DIAG|BOOT|STEP|name=%s,result=%s\n", name, esp_err_to_name(err));
    fflush(stdout);
    return err == ESP_OK;
}

/* ---- audio source arbitration (RADIO-2c) ---- */

typedef enum {
    AUDIO_SOURCE_SILENCE = 0,
    AUDIO_SOURCE_TONE,
    AUDIO_SOURCE_RADIO,
} audio_source_t;

static audio_source_t choose_audio_source(radio_state_t radio_state, bool radio_ready, bool tone_on)
{
    if (radio_state == RADIO_STATE_RUNNING || radio_state == RADIO_STATE_STARTING) {
        return radio_ready ? AUDIO_SOURCE_RADIO : AUDIO_SOURCE_SILENCE;
    }
    return tone_on ? AUDIO_SOURCE_TONE : AUDIO_SOURCE_SILENCE;
}

static bool tone_is_on(void)
{
    bool enabled = false;
    tone_get(&enabled, NULL);
    return enabled;
}

/* ---- audio producer task: the single owner of I2S production ---- */

static TaskHandle_t s_audio_out_task;
static atomic_bool  s_audio_out_stop;

static void audio_out_task(void *arg)
{
    (void)arg;
    static int16_t block[AUDIO_OUT_FRAMES * 2];
    static int32_t block32[AUDIO_OUT_FRAMES * 2];

    while (!atomic_load(&s_audio_out_stop)) {
        switch (choose_audio_source(radio_get_state(), radio_audio_ready(), tone_is_on())) {
        case AUDIO_SOURCE_RADIO: {
            size_t got = radio_pcm_read(block, AUDIO_OUT_FRAMES);
            if (got < AUDIO_OUT_FRAMES) {
                memset(&block[got * 2], 0, (AUDIO_OUT_FRAMES - got) * 2 * sizeof(int16_t));
            }
            break;
        }
        case AUDIO_SOURCE_TONE:
            tone_fill(block, AUDIO_OUT_FRAMES);
            break;
        case AUDIO_SOURCE_SILENCE:
        default:
            memset(block, 0, sizeof(block));
            break;
        }

        /* Pre-I2S source trim (RADIO/CTRL volume): scale the mixed PCM before the
         * 16-in-32 pack. Independent of the WROOM32's post-mix VOLUME. */
        i2s_out_apply_gain(block, AUDIO_OUT_FRAMES * 2, i2s_out_get_gain());
        for (size_t i = 0; i < AUDIO_OUT_FRAMES * 2; i++) {
            block32[i] = (int32_t)block[i] * INT32_C(65536);   /* top half of the 32-bit slot */
        }
        const uint8_t *p = (const uint8_t *)block32;
        size_t total = sizeof(block32), off = 0;
        while (off < total && !atomic_load(&s_audio_out_stop)) {
            size_t w = i2s_out_write(p + off, total - off);
            off += w;
            if (w == 0) {
                /* Ring full / I2S not draining yet. Phase 3 adds a state
                 * field to i2s_out_stats_t for a state-aware backoff; a
                 * fixed 1-tick yield is the safe default until then. */
                vTaskDelay(1);
            }
        }
    }
    s_audio_out_task = NULL;
    vTaskDelete(NULL);
}

/* ---- NVS: the one step allowed to be genuinely fatal (SPEC §5.2) ---- */

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* ---- BT link read-only health probe (SPEC §3.3: no mutating boot self-test) ----
 * Runs in its own low-priority task *after* BOOT_COMPLETE so a slow/absent
 * WROOM32 can never delay web/console/audio startup. */

static const char *link_state_str(bt_link_cmd_state_t st)
{
    switch (st) {
    case BT_LINK_CMD_DONE_OK:  return "OK";
    case BT_LINK_CMD_DONE_ERR: return "ERR";
    case BT_LINK_CMD_TIMEOUT:  return "TIMEOUT";
    default:                   return "PENDING";
    }
}

static void link_health_probe_task(void *arg)
{
    (void)arg;
    static const char *const cmds[] = { "VERSION", "STATUS" };
    int ok = 0;
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        bt_link_cmd_state_t state = BT_LINK_CMD_TIMEOUT;
        char result[BT_LINK_FIELD_MAX] = {0};
        char data[BT_LINK_FIELD_MAX] = {0};
        esp_err_t err = bt_link_send(cmds[i], &state, result, sizeof(result), data, sizeof(data));
        if (err == ESP_OK && state == BT_LINK_CMD_DONE_OK) ok++;
        printf("DIAG|BTLINK|PROBE|cmd=%s,transport=%s,state=%s\n",
               cmds[i], esp_err_to_name(err), link_state_str(state));
    }
    printf("DIAG|BTLINK|PROBE_DONE|ok=%d/%u\n", ok, (unsigned)(sizeof(cmds) / sizeof(cmds[0])));
    fflush(stdout);
    vTaskDelete(NULL);
}

/* ---- Clock diagnostics (MAIN-007/008/009): every PCNT call checked, no
 * bogus frequency printed on failure, measured only once per
 * CLOCK_DIAG_PERIOD_MS instead of every 1 s loop iteration. ---- */

#include "driver/pulse_cnt.h"
#include "esp_timer.h"

#define CLOCK_DIAG_PERIOD_MS 10000
#define CLOCK_MEASURE_MS       100

static bool s3_measure_hz(int gpio, int ms, float *out_hz)
{
    pcnt_unit_config_t ucfg = {
        .high_limit = 32000,
        .low_limit = -1,
        .flags = { .accum_count = true },
    };
    pcnt_unit_handle_t unit;
    if (pcnt_new_unit(&ucfg, &unit) != ESP_OK) return false;

    pcnt_chan_config_t ccfg = { .edge_gpio_num = gpio, .level_gpio_num = -1 };
    pcnt_channel_handle_t ch;
    if (pcnt_new_channel(unit, &ccfg, &ch) != ESP_OK) {
        pcnt_del_unit(unit);
        return false;
    }

    bool ok = true;
    ok = ok && (pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                             PCNT_CHANNEL_EDGE_ACTION_HOLD) == ESP_OK);
    ok = ok && (pcnt_unit_add_watch_point(unit, 32000) == ESP_OK); /* required for accum_count */
    ok = ok && (pcnt_unit_enable(unit) == ESP_OK);
    ok = ok && (pcnt_unit_clear_count(unit) == ESP_OK);
    ok = ok && (pcnt_unit_start(unit) == ESP_OK);

    int64_t t0 = esp_timer_get_time();
    if (ok) vTaskDelay(pdMS_TO_TICKS(ms));
    int64_t dt = esp_timer_get_time() - t0;

    int count = 0;
    if (ok) ok = (pcnt_unit_get_count(unit, &count) == ESP_OK);
    ok = (pcnt_unit_stop(unit) == ESP_OK) && ok;
    ok = (pcnt_unit_disable(unit) == ESP_OK) && ok;
    pcnt_del_channel(ch);
    pcnt_del_unit(unit);

    if (!ok || dt <= 0) return false;
    *out_hz = (float)count * 1e6f / (float)dt;
    return true;
}

static void clock_diag_task(void *arg)
{
    (void)arg;
    for (;;) {
        float bclk_hz, ws_hz;
        bool bclk_ok = s3_measure_hz(I2S_OUT_GPIO_BCLK, CLOCK_MEASURE_MS, &bclk_hz);
        bool ws_ok   = s3_measure_hz(I2S_OUT_GPIO_WS, CLOCK_MEASURE_MS, &ws_hz);
        if (bclk_ok && ws_ok) {
            printf("DIAG|I2SFREQ|bclk_hz=%.0f,ws_hz=%.0f,ratio=%.2f\n",
                   bclk_hz, ws_hz, (ws_hz > 0) ? bclk_hz / ws_hz : -1.0f);
        } else {
            printf("DIAG|I2SFREQ|ERROR|bclk_ok=%d,ws_ok=%d\n", bclk_ok, ws_ok);
        }
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(CLOCK_DIAG_PERIOD_MS));
    }
}

void app_main(void)
{
    /* 1. NVS */
    init_nvs();

    /* 2. Boot diagnostics + boot_id */
    uint32_t boot_id = esp_random();
    size_t psram_bytes = 0;
#if CONFIG_SPIRAM
    psram_bytes = esp_psram_get_size();
#endif
    ESP_LOGI(TAG, "esp_i2s_source boot: idf=%s psram=%u KB free_heap=%u B boot_id=%08" PRIx32,
             esp_get_idf_version(), (unsigned)(psram_bytes / 1024),
             (unsigned)esp_get_free_heap_size(), boot_id);
    printf("DIAG|BOOT|READY|boot_id=%08" PRIx32 ",psram_kb=%u,heap=%u\n",
           boot_id, (unsigned)(psram_bytes / 1024), (unsigned)esp_get_free_heap_size());
    fflush(stdout);

    boot_status_t boot = {0};

    /* 3-4. I2S: independent of WROOM32 presence, must not hang boot (SPEC §6.4). */
    esp_err_t err = i2s_out_init(I2S_RING_BYTES);
    boot.i2s_ok = log_boot_step("i2s_init", err);
    if (boot.i2s_ok) {
        err = i2s_out_start();
        boot.i2s_ok = log_boot_step("i2s_start", err);
    }

    /* 5. bt_link — UART link to the WROOM32. Non-fatal: absence keeps local
     * tone/radio + control available (SPEC §3.2). */
    boot.bt_link_ok = log_boot_step("bt_link_init", bt_link_init(BT_LINK_DEFAULT_TIMEOUT_MS));

    /* 6. radio */
    boot.radio_ok = log_boot_step("radio_init", radio_init(RADIO_RING_BYTES));

    /* 7. stations (includes any persistence migration) */
    boot.stations_ok = log_boot_step("stations_init", stations_init());

    /* 8. ctrl — must exist before web_ui_start() touches its mutex. */
    boot.ctrl_ok = log_boot_step("ctrl_init", ctrl_init());

    /* 9. audio_out task — only once I2S and the radio interface both exist. */
    if (boot.i2s_ok) {
        atomic_store(&s_audio_out_stop, false);
        BaseType_t created = xTaskCreate(audio_out_task, "audio_out", 4096, NULL,
                                          tskIDLE_PRIORITY + 4, &s_audio_out_task);
        boot.audio_task_ok = (created == pdPASS);
        log_boot_step("audio_out_start", boot.audio_task_ok ? ESP_OK : ESP_ERR_NO_MEM);
    } else {
        log_boot_step("audio_out_start", ESP_ERR_INVALID_STATE);
    }

    /* 10. Wi-Fi */
    boot.wifi_ok = log_boot_step("wifi_mgr_init", wifi_mgr_init());

    /* 11. console */
    boot.console_ok = log_boot_step("console_start", console_start());

    /* 12. web UI — optional/degraded (SPEC §3.2: web failure keeps console + audio). */
    boot.web_ok = log_boot_step("web_ui_start", web_ui_start());

    /* 13. ctrl_start — boot orchestrator (autostart/resume). */
    boot.ctrl_start_ok = log_boot_step("ctrl_start", ctrl_start());

    /* 14. BOOT_COMPLETE marker — exactly once per boot generation (SPEC §5.3). */
    bool required_ok = boot.i2s_ok && boot.audio_task_ok;
    bool degraded = !(boot.bt_link_ok && boot.radio_ok && boot.stations_ok &&
                       boot.ctrl_ok && boot.wifi_ok && boot.console_ok &&
                       boot.web_ok && boot.ctrl_start_ok);
    printf("DIAG|BOOT|COMPLETE|required_ok=%d,degraded=%d,boot_id=%08" PRIx32 "\n",
           required_ok ? 1 : 0, degraded ? 1 : 0, boot_id);
    fflush(stdout);

    /* 15. Bounded read-only WROOM probe — async, cannot delay the boot above. */
    if (boot.bt_link_ok) {
        TaskHandle_t probe_task = NULL;
        xTaskCreate(link_health_probe_task, "link_probe", 4096, NULL,
                    tskIDLE_PRIORITY + 1, &probe_task);
    }

    /* 16. Low-rate diagnostics — outside the critical boot path. */
    TaskHandle_t clock_diag = NULL;
    xTaskCreate(clock_diag_task, "clock_diag", 3072, NULL, tskIDLE_PRIORITY + 1, &clock_diag);

    for (;;) {
        i2s_out_stats_t st;
        i2s_out_get_stats(&st);
        printf("DIAG|I2S|bytes=%llu,und=%llu,undev=%u,ringpeak=%u\n",
               (unsigned long long)st.bytes_written,
               (unsigned long long)st.underrun_bytes,
               (unsigned)st.underrun_events, (unsigned)st.ring_peak);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
