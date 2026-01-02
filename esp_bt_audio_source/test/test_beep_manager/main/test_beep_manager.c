#include "unity.h"
#include "unity_test_runner.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "beep_manager.h"
#include "audio_queue.h"

static bool s_done_called = false;

static void done_cb(void *ctx)
{
    bool *flag = (bool *)ctx;
    if (flag != NULL) {
        *flag = true;
    }
}

static void reset_state(void)
{
    audio_chunk_pool_deinit();
    beep_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_init());
    s_done_called = false;
    beep_manager_set_done_callback(NULL, NULL);
}

static size_t drain_queue(uint16_t *first_tag, uint16_t *last_tag)
{
    audio_chunk_t chunk = {0};
    size_t count = 0;
    bool have_first = false;

    while (audio_chunk_dequeue(&chunk, 0)) {
        TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_BEEP, chunk.tag);
        if (!have_first) {
            *first_tag = chunk.tag_id;
            have_first = true;
        }
        *last_tag = chunk.tag_id;
        ++count;
        audio_chunk_release_block(chunk.data);
    }

    if (!have_first) {
        *first_tag = 0;
        *last_tag = 0;
    }

    return count;
}

static audio_config_t make_cfg(audio_sample_rate_t rate, audio_bit_depth_t depth, audio_channel_t ch)
{
    audio_config_t cfg = {
        .sample_rate = rate,
        .bit_depth = depth,
        .channels = ch,
        .volume = 80,
    };
    return cfg;
}

TEST_CASE("beep_manager_play_enqueues_and_calls_done", "[beep_manager]")
{
    reset_state();
    beep_manager_set_done_callback(done_cb, &s_done_called);

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 80,
    };

    beep_request_t req = {
        .duration_ms = 20,
        .freq_hz = 1200.0,
        .amplitude = 1500,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));

    TEST_ASSERT_TRUE(s_done_called);
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());

    uint16_t first_tag = 0;
    uint16_t last_tag = 0;
    size_t count = drain_queue(&first_tag, &last_tag);
    TEST_ASSERT_GREATER_THAN_UINT(0, count);
    TEST_ASSERT_EQUAL_UINT16(0, first_tag);
    TEST_ASSERT_EQUAL_UINT(0, audio_descriptor_used());
}

TEST_CASE("beep_manager_tag_ids_continue_across_runs", "[beep_manager]")
{
    reset_state();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_8K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 50,
    };

    beep_request_t req = {
        .duration_ms = 2, /* short enough to fit in one chunk */
        .freq_hz = 800.0,
        .amplitude = 1000,
    };

    uint16_t first_tag = 0;
    uint16_t last_tag = 0;
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    size_t count_first = drain_queue(&first_tag, &last_tag);
    TEST_ASSERT_EQUAL_UINT16(0, first_tag);
    TEST_ASSERT_GREATER_THAN_UINT(0, count_first);

    uint16_t first_tag_second = 0;
    uint16_t last_tag_second = 0;
    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));
    size_t count_second = drain_queue(&first_tag_second, &last_tag_second);
    TEST_ASSERT_GREATER_THAN_UINT(0, count_second);
    TEST_ASSERT_EQUAL_UINT16(last_tag + 1, first_tag_second);
    TEST_ASSERT_EQUAL_UINT(0, audio_descriptor_used());
}

TEST_CASE("beep_manager_rejects_invalid_args", "[beep_manager]")
{
    reset_state();

    audio_config_t bad_cfg = {
        .sample_rate = 0,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 10,
    };

    beep_request_t req = {
        .duration_ms = 5,
        .freq_hz = 1000.0,
        .amplitude = 500,
    };

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, beep_manager_play(NULL, &bad_cfg));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, beep_manager_play(&req, &bad_cfg));
}

TEST_CASE("beep_manager_rejects_unsupported_bit_depth", "[beep_manager]")
{
    reset_state();

    audio_config_t cfg = make_cfg(AUDIO_SAMPLE_RATE_8K, AUDIO_BIT_DEPTH_24, AUDIO_CHANNEL_MONO);
    beep_request_t req = {
        .duration_ms = 10,
        .freq_hz = 500.0,
        .amplitude = 800,
    };

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, beep_manager_play(&req, &cfg));
    TEST_ASSERT_EQUAL_UINT(0, audio_descriptor_used());
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
}

typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
} worker_ctx_t;

static bool wait_for_state(beep_state_t target, TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (beep_manager_get_state() == target) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

static void beep_worker(void *arg)
{
    worker_ctx_t *ctx = (worker_ctx_t *)arg;

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 70,
    };

    beep_request_t req = {
        .duration_ms = 20000, /* long to keep state busy */
        .freq_hz = 440.0,
        .amplitude = 1200,
    };

    ctx->result = beep_manager_play(&req, &cfg);

    if (ctx->done) {
        xSemaphoreGive(ctx->done);
    }

    vTaskDelete(NULL);
}

TEST_CASE("beep_manager_reports_busy_while_playing", "[beep_manager]")
{
    reset_state();

    worker_ctx_t ctx = {
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL,
    };

    TEST_ASSERT_NOT_NULL(ctx.done);

    TaskHandle_t handle = NULL;
    BaseType_t created = xTaskCreate(beep_worker, "beep_worker", 4096, &ctx, 5, &handle);
    TEST_ASSERT_EQUAL(pdPASS, created);

    bool playing = wait_for_state(BEEP_STATE_PLAYING, pdMS_TO_TICKS(200));

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 60,
    };

    beep_request_t req = {
        .duration_ms = 5,
        .freq_hz = 1000.0,
        .amplitude = 800,
    };

    esp_err_t rc = beep_manager_play(&req, &cfg);
    if (playing) {
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, rc);
    } else {
        /* If the worker exited early (e.g., pool exhausted), any non-OK error is acceptable. */
        TEST_ASSERT_NOT_EQUAL(ESP_OK, rc);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    beep_manager_stop();

    TEST_ASSERT_TRUE_MESSAGE(xSemaphoreTake(ctx.done, pdMS_TO_TICKS(1000)) == pdTRUE,
                             "worker did not finish in time");

    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());
    TEST_ASSERT(ctx.result == ESP_OK || ctx.result == ESP_ERR_NO_MEM);

    vSemaphoreDelete(ctx.done);
    audio_chunk_pool_deinit();
}

TEST_CASE("beep_manager_stop_requests_terminate_and_invoke_done", "[beep_manager]")
{
    reset_state();
    beep_manager_set_done_callback(done_cb, &s_done_called);

    audio_config_t cfg = make_cfg(AUDIO_SAMPLE_RATE_16K, AUDIO_BIT_DEPTH_16, AUDIO_CHANNEL_MONO);
    beep_request_t req = {
        .duration_ms = 500, /* long enough to stop mid-stream */
        .freq_hz = 600.0,
        .amplitude = 1200,
    };

    worker_ctx_t ctx = {
        .done = xSemaphoreCreateBinary(),
        .result = ESP_FAIL,
    };
    TEST_ASSERT_NOT_NULL(ctx.done);

    TaskHandle_t handle = NULL;
    BaseType_t created = xTaskCreate(beep_worker, "beep_worker_stop", 4096, &ctx, 5, &handle);
    TEST_ASSERT_EQUAL(pdPASS, created);

    /* Wait until playback begins, then stop */
    (void)wait_for_state(BEEP_STATE_PLAYING, pdMS_TO_TICKS(200));
    beep_manager_stop();

    TEST_ASSERT_TRUE_MESSAGE(xSemaphoreTake(ctx.done, pdMS_TO_TICKS(1000)) == pdTRUE,
                             "worker did not finish in time");

    TEST_ASSERT_TRUE(s_done_called);
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());

    uint16_t first_tag = 0, last_tag = 0;
    size_t drained = drain_queue(&first_tag, &last_tag);
    if (drained > 0) {
        TEST_ASSERT_EQUAL_UINT16(0, first_tag);
    }
    TEST_ASSERT_EQUAL_UINT(0, audio_descriptor_used());

    vSemaphoreDelete(ctx.done);
    audio_chunk_pool_deinit();
}

TEST_CASE("beep_manager_emits_consecutive_tags_within_long_beep", "[beep_manager]")
{
    reset_state();

    audio_config_t cfg = make_cfg(AUDIO_SAMPLE_RATE_16K, AUDIO_BIT_DEPTH_16, AUDIO_CHANNEL_MONO);
    beep_request_t req = {
        .duration_ms = 200, /* multiple chunks */
        .freq_hz = 700.0,
        .amplitude = 1800,
    };

    TEST_ASSERT_EQUAL(ESP_OK, beep_manager_play(&req, &cfg));

    uint16_t first_tag = 0, last_tag = 0;
    size_t drained = drain_queue(&first_tag, &last_tag);
    TEST_ASSERT_GREATER_THAN_UINT(1, drained); /* should span multiple chunks */
    TEST_ASSERT_EQUAL_UINT16(drained - 1, last_tag);
    TEST_ASSERT_EQUAL_UINT(0, audio_descriptor_used());
    TEST_ASSERT_EQUAL(BEEP_STATE_STOPPED, beep_manager_get_state());

    audio_chunk_pool_deinit();
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
