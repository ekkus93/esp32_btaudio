#include "command_interface.h"
#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pull in the production audio processor API and test hooks.
#include "audio_processor.h"

static bool s_bt_connected = true;
static TaskHandle_t s_beep_drain_task = NULL;
static volatile bool s_beep_drain_stop = false;

static void beep_drain_task(void *arg)
{
    (void)arg;
    uint8_t buf[256];
    TickType_t idle_since = xTaskGetTickCount();

    while (!s_beep_drain_stop) {
        size_t ring_used = audio_processor_test_get_ring_used_bytes();
        size_t ring_free = audio_processor_test_get_audio_free_bytes();
        size_t ring_capacity = ring_used + ring_free;
        size_t high_water = (ring_capacity * 85U) / 100U;
        bool beep_active = audio_processor_is_beep_active();

        if ((ring_capacity > 0 && ring_used >= high_water) || beep_active) {
            size_t bytes_read = 0;
            (void)audio_processor_read(buf, sizeof(buf), &bytes_read);
            idle_since = xTaskGetTickCount();
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        if ((xTaskGetTickCount() - idle_since) > pdMS_TO_TICKS(200)) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    s_beep_drain_task = NULL;
    vTaskDelete(NULL);
}

static void start_beep_drain_task(void)
{
    if (s_beep_drain_task != NULL) {
        return;
    }
    s_beep_drain_stop = false;
    /* Heap/log churn in audio_processor_read + WAV abort logging needs more stack headroom. */
    BaseType_t ok = xTaskCreate(beep_drain_task, "beep_drain", 4096, NULL, tskIDLE_PRIORITY + 1, &s_beep_drain_task);
    if (ok != pdPASS) {
        s_beep_drain_task = NULL;
    }
}

static void stop_beep_drain_task(void)
{
    if (s_beep_drain_task == NULL) {
        return;
    }
    s_beep_drain_stop = true;
    for (int i = 0; i < 5 && s_beep_drain_task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool bt_manager_is_a2dp_connected(void)
{
    return s_bt_connected;
}

void bt_manager_mock_connection_closed(const char* mac)
{
    (void)mac;
    s_bt_connected = false;
}

void bt_manager_mock_connection_opened(const char* mac)
{
    (void)mac;
    s_bt_connected = true;
}

// Very small parser that understands STOP/BEEP commands used by the tests.
cmd_status_t cmd_parse(const char* cmd_str, cmd_context_t* ctx)
{
    if (!cmd_str || !ctx) return CMD_ERROR_INVALID_PARAM;
    memset(ctx, 0, sizeof(*ctx));

    // Copy and trim leading whitespace
    char buf[256];
    strncpy(buf, cmd_str, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    char* s = buf;
    while (*s && (*s == ' ' || *s == '\t')) ++s;
    if (*s == '\0') return CMD_ERROR_UNKNOWN;

    // Tokenize first token
    char* save = NULL;
    char* token = strtok_r(s, " \t", &save);
    if (!token) return CMD_ERROR_UNKNOWN;

    if (strcasecmp(token, "STOP") == 0) {
        ctx->type = CMD_TYPE_STOP;
        ctx->param_count = 0;
        return CMD_SUCCESS;
    }

    if (strcasecmp(token, "BEEP") == 0) {
        ctx->type = CMD_TYPE_BEEP;
        ctx->param_count = 0;
        return CMD_SUCCESS;
    }

    return CMD_ERROR_UNKNOWN;
}

cmd_status_t cmd_execute(const cmd_context_t* ctx)
{
    if (!ctx) return CMD_ERROR_NOT_INITIALIZED;

    if (ctx->type == CMD_TYPE_STOP) {
        /* Mirror STOP behavior enough for tests: drain queue and stop the processor. */
        (void)audio_processor_drain_ring();
        esp_err_t stop_ret = audio_processor_stop();
        if (stop_ret == ESP_OK || stop_ret == ESP_ERR_INVALID_STATE) {
            return CMD_SUCCESS;
        }
        ESP_LOGW("TEST_CMD_IF", "audio_processor_stop returned %d (%s)", (int)stop_ret, esp_err_to_name(stop_ret));
        return CMD_ERROR_UNKNOWN;
    }

    if (ctx->type == CMD_TYPE_BEEP) {
        /* Only allow BEEP when connected. */
        if (!bt_manager_is_a2dp_connected()) {
            ESP_LOGW("TEST_CMD_IF", "BEEP rejected: A2DP not connected");
            return CMD_ERROR_UNKNOWN;
        }
        /* Ensure the pipeline is ready to consume the long (10s) test tone so
         * the enqueue path does not fail on a full queue when the app is idle.
         * This mirrors production, where the pipeline is normally running. */
        (void)audio_processor_drain_ring();
        (void)audio_processor_start();
        start_beep_drain_task();
        esp_err_t bret = audio_processor_beep(10000);
        stop_beep_drain_task();
        if (bret == ESP_OK) {
            return CMD_SUCCESS;
        }
        ESP_LOGW("TEST_CMD_IF", "audio_processor_beep returned %d (%s)", (int)bret, esp_err_to_name(bret));
        return CMD_ERROR_UNKNOWN;
    }
    return CMD_ERROR_UNKNOWN;
}
