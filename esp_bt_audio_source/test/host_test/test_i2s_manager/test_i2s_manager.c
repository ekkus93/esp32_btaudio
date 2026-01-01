#include "unity.h"

#include <string.h>
#include <stdint.h>

#include "i2s_manager.h"
#include "audio_queue.h"
#include "audio_util.h"
#include "esp_err.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Shim helpers exposed by shim_audio_queue.c */
size_t audio_queue_last_len(void);
audio_source_tag_t audio_queue_last_tag(void);
uint16_t audio_queue_last_tag_id(void);
const uint8_t *audio_queue_last_data(void);
void audio_queue_set_fail_enqueue(bool fail);

#ifdef CONFIG_BT_MOCK_TESTING
typedef struct {
	const uint8_t *data;
	size_t len;
	audio_bit_depth_t bit_depth;
	audio_sample_rate_t rate;
} mock_item_t;
#endif

static audio_config_t make_config(void)
{
	audio_config_t cfg = {0};
	cfg.sample_rate = AUDIO_SAMPLE_RATE_32K;
	cfg.bit_depth = AUDIO_BIT_DEPTH_16;
	cfg.channels = AUDIO_CHANNEL_MONO;
	cfg.volume = 50;
	cfg.mute = false;
	cfg.i2s_port = 0;
	cfg.i2s_bclk_pin = -1;
	cfg.i2s_ws_pin = -1;
	cfg.i2s_din_pin = -1;
	cfg.i2s_dout_pin = -1;
	return cfg;
}

static i2s_manager_buffers_t make_buffers(uint8_t *proc, uint8_t *proc2, size_t work_bytes)
{
	i2s_manager_buffers_t bufs = {0};
	bufs.proc_buf = proc;
	bufs.proc_buf2 = proc2;
	bufs.work_bytes = work_bytes;
	return bufs;
}

void setUp(void)
{
	audio_chunk_clear();
	mock_task_reset();
}

void tearDown(void)
{
	i2s_manager_stop();
	i2s_manager_deinit();
}

void test_init_should_reject_null_buffers(void)
{
	audio_config_t cfg = make_config();
	i2s_manager_buffers_t bufs = {0};
	TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_manager_init(&cfg, &bufs));
}

void test_handle_frame_without_init_should_fail(void)
{
	uint8_t data[4] = {1, 2, 3, 4};
	TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_manager_handle_frame(data, sizeof(data), AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_32K));
}

void test_start_should_fail_without_init(void)
{
	TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_manager_start());
	TEST_ASSERT_EQUAL_UINT(0, mock_task_create_count());
}

void test_start_should_create_task_and_set_running(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);

	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
	TEST_ASSERT_FALSE(i2s_manager_is_running());
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
	TEST_ASSERT_TRUE(i2s_manager_is_running());
	TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());
	TEST_ASSERT_NOT_NULL(mock_task_last_handle());
	TEST_ASSERT_EQUAL_STRING("i2s_mgr", mock_task_last_name());
}

void test_start_should_not_recreate_task_when_running(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);

	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
	TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());
	TEST_ASSERT_TRUE(i2s_manager_is_running());
}

void test_start_should_fail_when_task_creation_fails(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);

	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
	mock_task_set_create_result(pdFALSE);
	TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, i2s_manager_start());
	TEST_ASSERT_FALSE(i2s_manager_is_running());
	TEST_ASSERT_EQUAL_UINT(1, mock_task_create_count());
	TEST_ASSERT_NULL(mock_task_last_handle());
}

void test_stop_should_clear_running_flag(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);

	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_start());
	TEST_ASSERT_TRUE(i2s_manager_is_running());
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_stop());
	TEST_ASSERT_FALSE(i2s_manager_is_running());
}

void test_handle_frame_should_convert_and_enqueue(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);

	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));

	uint8_t payload[8] = {10, 20, 30, 40, 50, 60, 70, 80};
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_handle_frame(payload, sizeof(payload), AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_32K));

	TEST_ASSERT_EQUAL_size_t(8, audio_queue_last_len());
	TEST_ASSERT_EQUAL(AUDIO_SOURCE_TAG_CAPTURE, audio_queue_last_tag());
	const int16_t *out = (const int16_t *)audio_queue_last_data();
	TEST_ASSERT_EQUAL_INT16(5130, out[0]);
	TEST_ASSERT_EQUAL_INT16(10270, out[1]);
}

void test_mock_push_should_queue_item(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 16);

	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));

	const uint8_t data[4] = {1, 2, 3, 4};
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_mock_push(data, sizeof(data), AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_32K));

	QueueHandle_t q = i2s_manager_get_mock_queue();
	TEST_ASSERT_NOT_NULL(q);
	mock_item_t item = {0};
	TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &item, 0));
	TEST_ASSERT_EQUAL_PTR(data, item.data);
	TEST_ASSERT_EQUAL_size_t(sizeof(data), item.len);
	TEST_ASSERT_EQUAL(AUDIO_BIT_DEPTH_16, item.bit_depth);
	TEST_ASSERT_EQUAL(AUDIO_SAMPLE_RATE_32K, item.rate);
}

void test_handle_frame_should_resample_up(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));

	int16_t payload[] = {0, 1000};
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_handle_frame((uint8_t *)payload, sizeof(payload), AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_16K));

	TEST_ASSERT_EQUAL_size_t(4, audio_queue_last_len());
	const int16_t *out = (const int16_t *)audio_queue_last_data();
	TEST_ASSERT_EQUAL_INT16(0, out[0]);
	TEST_ASSERT_EQUAL_INT16(1000, out[1]);
}

void test_handle_frame_should_downsample_and_truncate(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	/* Limit work_bytes to force truncation of dst. */
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 4);
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));

	int16_t payload[] = {1000, 2000, 3000, 4000};
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_handle_frame((uint8_t *)payload, sizeof(payload), AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_32K));

	TEST_ASSERT_EQUAL_size_t(4, audio_queue_last_len());
	const int16_t *out = (const int16_t *)audio_queue_last_data();
	TEST_ASSERT_EQUAL_INT16(1000, out[0]);
	TEST_ASSERT_EQUAL_INT16(2000, out[1]);
}

void test_handle_frame_should_report_enqueue_failure(void)
{
	audio_config_t cfg = make_config();
	uint8_t proc[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	uint8_t proc2[AUDIO_CHUNK_BLOCK_BYTES] = {0};
	i2s_manager_buffers_t bufs = make_buffers(proc, proc2, 32);
	TEST_ASSERT_EQUAL(ESP_OK, i2s_manager_init(&cfg, &bufs));

	uint8_t payload[4] = {1, 2, 3, 4};
	audio_queue_set_fail_enqueue(true);
	TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, i2s_manager_handle_frame(payload, sizeof(payload), AUDIO_BIT_DEPTH_16, AUDIO_SAMPLE_RATE_32K));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_init_should_reject_null_buffers);
	RUN_TEST(test_handle_frame_without_init_should_fail);
	RUN_TEST(test_start_should_fail_without_init);
	RUN_TEST(test_start_should_create_task_and_set_running);
	RUN_TEST(test_start_should_not_recreate_task_when_running);
	RUN_TEST(test_start_should_fail_when_task_creation_fails);
	RUN_TEST(test_stop_should_clear_running_flag);
	RUN_TEST(test_handle_frame_should_convert_and_enqueue);
	RUN_TEST(test_mock_push_should_queue_item);
	RUN_TEST(test_handle_frame_should_resample_up);
	RUN_TEST(test_handle_frame_should_downsample_and_truncate);
	RUN_TEST(test_handle_frame_should_report_enqueue_failure);
	return UNITY_END();
}
