#include "unity.h"
#include "unity_test_runner.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "i2s_manager.h"

static uint8_t s_raw_buf[256];
static uint8_t s_proc_buf[512];
static uint8_t s_proc_buf2[512];

static audio_config_t default_config(void)
{
    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_16K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_MONO,
        .volume = 50,
        .mute = false,
        .i2s_port = I2S_NUM_0,
        .i2s_bclk_pin = 26,
        .i2s_ws_pin = 25,
        .i2s_din_pin = 22,
        .i2s_dout_pin = GPIO_NUM_NC,
    };
    return cfg;
}

static i2s_manager_buffers_t default_buffers(void)
{
    i2s_manager_buffers_t bufs = {
        .raw_buf = s_raw_buf,
        .raw_buf_bytes = sizeof(s_raw_buf),
        .proc_buf = s_proc_buf,
        .proc_buf2 = s_proc_buf2,
        .work_bytes = sizeof(s_proc_buf),
    };
    return bufs;
}

void tearDown(void)
{
    i2s_manager_deinit();
}

TEST_CASE("i2s_source_fill_requires_init", "[i2s_manager]")
{
    size_t filled = i2s_source_fill(s_raw_buf, sizeof(s_raw_buf));
    TEST_ASSERT_EQUAL_UINT32(0, filled);
}

TEST_CASE("i2s_manager_init_rejects_missing_buffers", "[i2s_manager]")
{
    audio_config_t cfg = default_config();
    i2s_manager_buffers_t bufs = default_buffers();
    bufs.proc_buf = NULL;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_manager_init(&cfg, &bufs));
}

TEST_CASE("i2s_manager_init_requires_work_bytes", "[i2s_manager]")
{
    audio_config_t cfg = default_config();
    i2s_manager_buffers_t bufs = default_buffers();
    bufs.work_bytes = 0;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_manager_init(&cfg, &bufs));
}

TEST_CASE("i2s_manager_stop_requires_init", "[i2s_manager]")
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_manager_stop());
}

#ifdef CONFIG_BT_MOCK_TESTING
TEST_CASE("i2s_source_fill_consumes_mock_queue", "[i2s_manager]")
{
    audio_config_t cfg = default_config();
    i2s_manager_buffers_t bufs = default_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());

    int16_t samples[4] = {100, -200, 300, -400};
    TEST_ASSERT_EQUAL(ESP_OK,
                      i2s_manager_mock_push((const uint8_t *)samples,
                                            sizeof(samples),
                                            AUDIO_BIT_DEPTH_16,
                                            AUDIO_SAMPLE_RATE_16K));

    size_t filled = i2s_source_fill(s_raw_buf, sizeof(samples));
    TEST_ASSERT_EQUAL(sizeof(samples), filled);
    TEST_ASSERT_EQUAL_MEMORY(samples, s_raw_buf, filled);
}
#endif

TEST_CASE("i2s_manager_start_is_idempotent", "[i2s_manager]")
{
    audio_config_t cfg = default_config();
    i2s_manager_buffers_t bufs = default_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
    TEST_ASSERT_FALSE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
    TEST_ASSERT_TRUE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_stop());
    TEST_ASSERT_FALSE(i2s_manager_is_running());
}

TEST_CASE("i2s_manager_start_and_stop_toggle_running", "[i2s_manager]")
{
    audio_config_t cfg = default_config();
    i2s_manager_buffers_t bufs = default_buffers();

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
    TEST_ASSERT_FALSE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
    TEST_ASSERT_TRUE(i2s_manager_is_running());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_stop());
    TEST_ASSERT_FALSE(i2s_manager_is_running());
}

void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
