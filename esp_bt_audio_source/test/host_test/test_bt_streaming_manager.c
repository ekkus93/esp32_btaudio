#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "esp_err.h"
#include "esp_a2dp_api.h"
#include "bt_source.h"
#include "audio_processor.h"
#include "mock_a2dp.h"

/* Forward declare streaming manager APIs (not exposed via a public header). */
esp_err_t bt_streaming_start(void);
esp_err_t bt_streaming_stop(void);
esp_err_t bt_streaming_pause(void);
esp_err_t bt_streaming_resume(void);
esp_err_t bt_get_streaming_info(bt_streaming_info_t* info);
void bt_streaming_manager_init(void);
void bt_streaming_manager_reset_state_for_test(void);
void bt_streaming_manager_force_state_for_test(bt_streaming_state_t state);

/* Helpers from mocks */
void bt_manager_test_set_connection_state(int v);
void bt_manager_test_reset_btstate_mock(void);
void audio_source_tag_test_reset_buffer(void);
void mock_task_set_tick(uint32_t ticks);

static void init_streaming_manager(void)
{
    mock_a2dp_reset();
    bt_manager_test_reset_btstate_mock();
    audio_source_tag_test_reset_buffer();
    bt_streaming_manager_reset_state_for_test();
    bt_streaming_manager_init();
}

void setUp(void)
{
    init_streaming_manager();
}

void tearDown(void)
{
    mock_a2dp_reset();
}

void test_stream_start_fails_when_not_connected(void)
{
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_streaming_start());
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_media_ctrl_calls());

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, info.state);
}

void test_stream_start_invokes_media_ctrl_and_sets_starting_state(void)
{
    bt_manager_test_set_connection_state(1);

    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_start());
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_START, mock_a2dp_get_last_media_ctrl());

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STARTING, info.state);
    TEST_ASSERT_FALSE(info.paused);
    TEST_ASSERT_EQUAL_UINT32(0, info.bytes_sent);
    TEST_ASSERT_EQUAL_UINT32(0, info.packets_sent);
}

void test_stream_stop_transitions_and_calls_media_ctrl(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);

    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_stop());
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_STOP, mock_a2dp_get_last_media_ctrl());

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPING, info.state);
}

void test_stream_pause_requires_streaming_state(void)
{
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_streaming_pause());
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_media_ctrl_calls());

    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_pause());
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_SUSPEND, mock_a2dp_get_last_media_ctrl());

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, info.state);
    TEST_ASSERT_TRUE(info.paused);
}

void test_stream_resume_requires_paused_state(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_streaming_resume());
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_media_ctrl_calls());

    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_PAUSED);
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_resume());
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_media_ctrl_calls());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_START, mock_a2dp_get_last_media_ctrl());

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, info.state);
    TEST_ASSERT_FALSE(info.paused);
}

void test_data_callback_updates_stats_when_streaming(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);

    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);

    const int16_t sample_data[] = {10000, -12000, 5000, -6000};
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data((const uint8_t*)sample_data, sizeof(sample_data)));

    uint8_t out[sizeof(sample_data)] = {0};
    int32_t written = data_cb(out, (int32_t)sizeof(out));
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(out), written);

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(out), info.bytes_sent);
    TEST_ASSERT_EQUAL_UINT32(1, info.packets_sent);
    TEST_ASSERT_FALSE(info.paused);

    /* Ensure audio_processor_read populated the buffer with non-zero data */
    bool any_nonzero = false;
    for (size_t i = 0; i < sizeof(out); ++i) {
        if (out[i] != 0) {
            any_nonzero = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(any_nonzero);
}

void test_data_callback_returns_silence_when_paused(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_PAUSED);

    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);

    uint8_t out[16] = {0};
    memset(out, 0xAA, sizeof(out));
    int32_t written = data_cb(out, (int32_t)sizeof(out));
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(out), written);

    for (size_t i = 0; i < sizeof(out); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, out[i]);
    }

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32(0, info.bytes_sent);
    TEST_ASSERT_EQUAL_UINT32(0, info.packets_sent);
    TEST_ASSERT_TRUE(info.paused);
}

void test_data_callback_zero_fills_on_underrun(void)
{
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);

    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);

    uint8_t out[32];
    memset(out, 0xAB, sizeof(out));
    int32_t written = data_cb(out, (int32_t)sizeof(out));
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(out), written);

    for (size_t i = 0; i < sizeof(out); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, out[i]);
    }

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(out), info.bytes_sent);
    TEST_ASSERT_EQUAL_UINT32(1, info.packets_sent);
    TEST_ASSERT_FALSE(info.paused);
}

void test_resume_after_underrun_updates_duration(void)
{
    mock_task_set_tick(100); /* baseline start time */
    bt_manager_test_set_connection_state(1);
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_start());

    /* Move into streaming state for data callback consumption */
    bt_streaming_manager_force_state_for_test(BT_STREAMING_STATE_STREAMING);

    esp_a2d_source_data_cb_t data_cb = mock_a2dp_get_registered_data_callback();
    TEST_ASSERT_NOT_NULL(data_cb);

    /* First pull underruns (no injected audio) but should advance stats */
    mock_task_set_tick(150);
    uint8_t underrun_buf[32];
    memset(underrun_buf, 0xAB, sizeof(underrun_buf));
    TEST_ASSERT_EQUAL_INT32(32, data_cb(underrun_buf, 32));

    /* Pause then resume and deliver real audio */
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_pause());
    mock_task_set_tick(200);
    TEST_ASSERT_EQUAL(ESP_OK, bt_streaming_resume());
    TEST_ASSERT_EQUAL(ESP_A2D_MEDIA_CTRL_START, mock_a2dp_get_last_media_ctrl());

    const int16_t sample_data[] = {1000, -1000, 2000, -2000};
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_test_inject_audio_data((const uint8_t*)sample_data, sizeof(sample_data)));

    mock_task_set_tick(260);
    uint8_t out[sizeof(sample_data)] = {0};
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(out), data_cb(out, (int32_t)sizeof(out)));

    bt_streaming_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_streaming_info(&info));
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STREAMING, info.state);
    TEST_ASSERT_FALSE(info.paused);
    TEST_ASSERT_EQUAL_UINT32(32 + sizeof(out), info.bytes_sent);
    TEST_ASSERT_EQUAL_UINT32(2, info.packets_sent);
    TEST_ASSERT_EQUAL_UINT32(160, info.stream_duration);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stream_start_fails_when_not_connected);
    RUN_TEST(test_stream_start_invokes_media_ctrl_and_sets_starting_state);
    RUN_TEST(test_stream_stop_transitions_and_calls_media_ctrl);
    RUN_TEST(test_stream_pause_requires_streaming_state);
    RUN_TEST(test_stream_resume_requires_paused_state);
    RUN_TEST(test_data_callback_updates_stats_when_streaming);
    RUN_TEST(test_data_callback_returns_silence_when_paused);
    RUN_TEST(test_data_callback_zero_fills_on_underrun);
    RUN_TEST(test_resume_after_underrun_updates_duration);
    return UNITY_END();
}
