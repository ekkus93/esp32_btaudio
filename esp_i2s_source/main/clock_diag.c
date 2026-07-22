/*
 * clock_diag — exact PCNT frequency meter for the WROOM32 master clock
 * (BCLK/WS), reported at a low cadence so it never competes with audio or
 * boot for CPU time (MAIN-007). Every PCNT call is checked; a bogus
 * frequency is never printed as though it were a successful measurement
 * (MAIN-008/MAIN-009).
 */
#include "clock_diag.h"

#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"

#include "i2s_out.h"

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

void clock_diag_start(void)
{
    TaskHandle_t task = NULL;
    /* 10.4: check every task creation — an optional diagnostic task failing
     * to start must be visible, not silently claimed as running. */
    if (xTaskCreate(clock_diag_task, "clock_diag", 3072, NULL, tskIDLE_PRIORITY + 1, &task) != pdPASS) {
        printf("DIAG|BOOT|DEGRADED|component=clock_diag,err=NO_MEM\n");
        fflush(stdout);
    }
}
