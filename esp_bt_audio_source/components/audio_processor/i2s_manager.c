/**
 * I2S manager: read/convert/resample capture audio and enqueue to audio_queue.
 */

#include "i2s_manager.h"

#include <string.h>

#include "util_safe.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "audio_queue.h"
#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, msg) do { esp_err_t __err = (x); if (__err != ESP_OK) { ESP_LOGE(tag, "%s: %d", msg, __err); return __err; } } while (0)
#endif

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#ifdef CONFIG_BT_MOCK_TESTING
#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC (-1)
#endif
#ifndef I2S_GPIO_UNUSED
#define I2S_GPIO_UNUSED GPIO_NUM_NC
#endif
#endif
#endif

static const char *TAG = "i2s_manager";

#ifdef CONFIG_BT_MOCK_TESTING
typedef struct {
	const uint8_t *data;
	size_t len;
	audio_bit_depth_t bit_depth;
	audio_sample_rate_t rate;
} mock_item_t;
#endif

typedef struct {
	bool initialized;
	bool running;
	bool i2s_enabled;
	audio_config_t cfg;
	i2s_manager_buffers_t bufs;
#ifdef ESP_PLATFORM
	i2s_chan_handle_t i2s_rx;
#endif
	TaskHandle_t task;
#ifdef CONFIG_BT_MOCK_TESTING
	QueueHandle_t mock_queue;
#endif
} i2s_manager_state_t;

static i2s_manager_state_t s_mgr = {0};

#ifdef ESP_PLATFORM
static esp_err_t configure_i2s(const audio_config_t *cfg)
{
	if (cfg == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (s_mgr.i2s_rx != NULL) {
		i2s_channel_disable(s_mgr.i2s_rx);
		i2s_del_channel(s_mgr.i2s_rx);
		s_mgr.i2s_rx = NULL;
		s_mgr.i2s_enabled = false;
	}

	i2s_chan_config_t chan_cfg = {
		.id = cfg->i2s_port,
		.role = I2S_ROLE_SLAVE,
		.dma_desc_num = 6,
		.dma_frame_num = 32,
		.auto_clear = true,
	};

	esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_mgr.i2s_rx);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "i2s_new_channel failed: %d", ret);  // NOLINT(bugprone-branch-clone)
		return ret;
	}

	i2s_data_bit_width_t bit_width = I2S_DATA_BIT_WIDTH_16BIT;
	switch (cfg->bit_depth) {
	case AUDIO_BIT_DEPTH_24: bit_width = I2S_DATA_BIT_WIDTH_24BIT; break;
	case AUDIO_BIT_DEPTH_32: bit_width = I2S_DATA_BIT_WIDTH_32BIT; break;
	default: break;
	}

	i2s_std_config_t std_cfg = {0};
#ifndef CONFIG_BT_MOCK_TESTING
	std_cfg.clk_cfg = (i2s_std_clk_config_t)I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate);
	std_cfg.slot_cfg = (i2s_std_slot_config_t)I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
		bit_width,
		cfg->channels == AUDIO_CHANNEL_MONO ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO);
#else
	std_cfg.clk_cfg.sample_rate_hz = cfg->sample_rate;
	std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
	std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
	std_cfg.slot_cfg.data_bit_width = bit_width;
	std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
	std_cfg.slot_cfg.slot_mode = cfg->channels == AUDIO_CHANNEL_MONO ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
	std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
	std_cfg.slot_cfg.ws_width = bit_width;
	std_cfg.slot_cfg.ws_pol = false;
	std_cfg.slot_cfg.bit_shift = true;
#endif
	std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
	std_cfg.gpio_cfg.bclk = cfg->i2s_bclk_pin;
	std_cfg.gpio_cfg.ws = cfg->i2s_ws_pin;
	std_cfg.gpio_cfg.din = cfg->i2s_din_pin;
	std_cfg.gpio_cfg.dout = cfg->i2s_dout_pin;
	std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
	std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
	std_cfg.gpio_cfg.invert_flags.ws_inv = false;

	ret = i2s_channel_init_std_mode(s_mgr.i2s_rx, &std_cfg);
	if (ret != ESP_OK) {
		i2s_del_channel(s_mgr.i2s_rx);
		s_mgr.i2s_rx = NULL;
		ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %d", ret);  // NOLINT(bugprone-branch-clone)
		return ret;
	}

	return ESP_OK;
}
#endif

static esp_err_t process_frame(const uint8_t *data,
							   size_t len,
							   audio_bit_depth_t bit_depth,
							   audio_sample_rate_t sample_rate)
{
	if (!s_mgr.initialized || data == NULL || len == 0) {
		return ESP_ERR_INVALID_STATE;
	}

	if (s_mgr.bufs.proc_buf == NULL || s_mgr.bufs.proc_buf2 == NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	size_t conv_size = 0;
	audio_convert_args_t conv_args = {
		.src = data,
		.dst = s_mgr.bufs.proc_buf,
		.src_size = len,
		.src_bit_depth = bit_depth,
		.dst_bit_depth = s_mgr.cfg.bit_depth,
		.dst_size = &conv_size,
		.work_bytes = s_mgr.bufs.work_bytes,
	};
	ESP_RETURN_ON_ERROR(convert_audio_format(&conv_args), TAG, "convert failed");  // NOLINT(bugprone-branch-clone)

	size_t res_size = 0;
	audio_resample_args_t res_args = {
		.src = s_mgr.bufs.proc_buf,
		.dst = s_mgr.bufs.proc_buf2,
		.src_size = conv_size,
		.src_rate = sample_rate,
		.dst_rate = s_mgr.cfg.sample_rate,
		.bit_depth = s_mgr.cfg.bit_depth,
		.channels = s_mgr.cfg.channels,
		.dst_size = &res_size,
		.work_bytes = s_mgr.bufs.work_bytes,
	};
	ESP_RETURN_ON_ERROR(resample_audio(&res_args), TAG, "resample failed");  // NOLINT(bugprone-branch-clone)

	if (res_size == 0) {
		return ESP_OK;
	}

	/* Enqueue into audio_queue with capture tag; tag_id assigned inside queue. */
	if (!audio_chunk_enqueue_bytes(s_mgr.bufs.proc_buf2, res_size, AUDIO_SOURCE_TAG_CAPTURE)) {
		return ESP_ERR_NO_MEM;
	}
	return ESP_OK;
}

static void i2s_manager_task(void *arg)
{
	(void)arg;
	ESP_LOGI(TAG, "i2s_manager task started");  // NOLINT(bugprone-branch-clone)
	const TickType_t idle_wait = pdMS_TO_TICKS(20);
	const uint32_t read_timeout_ms = 5;
	while (s_mgr.running) {
		bool did_work = false;
#ifdef CONFIG_BT_MOCK_TESTING
		mock_item_t item;
		if (s_mgr.mock_queue != NULL && xQueueReceive(s_mgr.mock_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
			(void)process_frame(item.data, item.len, item.bit_depth, item.rate);
			did_work = true;
			continue;
		}
#endif
#ifdef ESP_PLATFORM
		if (s_mgr.i2s_rx != NULL && s_mgr.bufs.raw_buf != NULL && s_mgr.bufs.raw_buf_bytes > 0) {
			size_t read_bytes = 0;
			esp_err_t ret = i2s_channel_read(s_mgr.i2s_rx,
											 s_mgr.bufs.raw_buf,
											 s_mgr.bufs.raw_buf_bytes,
											 &read_bytes,
									 read_timeout_ms);
			if (ret == ESP_OK && read_bytes > 0) {
				(void)process_frame(s_mgr.bufs.raw_buf, read_bytes, s_mgr.cfg.bit_depth, s_mgr.cfg.sample_rate);
				did_work = true;
			} else if (ret == ESP_ERR_TIMEOUT) {
				/* No samples available; pause a bit to keep idle tasks feeding the WDT. */
				vTaskDelay(idle_wait);
			}
		}
#else
		vTaskDelay(pdMS_TO_TICKS(10));
		did_work = true;
		continue;
#endif

		if (!did_work) {
			vTaskDelay(idle_wait);
		}
		else {
			/* Even when work was done, yield briefly to keep the WDT fed. */
			vTaskDelay(pdMS_TO_TICKS(1));
		}
	}
	ESP_LOGI(TAG, "i2s_manager task exiting");  // NOLINT(bugprone-branch-clone)
	s_mgr.task = NULL;
	vTaskDelete(NULL);
}

esp_err_t i2s_manager_init(const audio_config_t *config, const i2s_manager_buffers_t *buffers)
{
	if (config == NULL || buffers == NULL || buffers->proc_buf == NULL || buffers->proc_buf2 == NULL || buffers->work_bytes == 0) {
		return ESP_ERR_INVALID_ARG;
	}

	if (!audio_chunk_pool_init()) {
		return ESP_ERR_NO_MEM;
	}

	util_safe_memset(&s_mgr, sizeof(s_mgr), 0, sizeof(s_mgr));
	s_mgr.cfg = *config;
	s_mgr.bufs = *buffers;

#ifdef CONFIG_BT_MOCK_TESTING
	s_mgr.mock_queue = xQueueCreate(8, sizeof(mock_item_t));
	if (s_mgr.mock_queue == NULL) {
		return ESP_ERR_NO_MEM;
	}
#endif

#ifdef ESP_PLATFORM
	ESP_RETURN_ON_ERROR(configure_i2s(&s_mgr.cfg), TAG, "configure_i2s failed");  // NOLINT(bugprone-branch-clone)
#endif

	s_mgr.initialized = true;
	return ESP_OK;
}

void i2s_manager_deinit(void)
{
	if (!s_mgr.initialized) {
		return;
	}

	(void)i2s_manager_stop();

#ifdef CONFIG_BT_MOCK_TESTING
	if (s_mgr.mock_queue) {
		vQueueDelete(s_mgr.mock_queue);
		s_mgr.mock_queue = NULL;
	}
#endif

#ifdef ESP_PLATFORM
	if (s_mgr.i2s_rx != NULL) {
		i2s_channel_disable(s_mgr.i2s_rx);
		i2s_del_channel(s_mgr.i2s_rx);
		s_mgr.i2s_rx = NULL;
	}
#endif

	s_mgr.initialized = false;
}

esp_err_t i2s_manager_start(void)
{
	if (!s_mgr.initialized) {
		return ESP_ERR_INVALID_STATE;
	}
	if (s_mgr.running) {
		return ESP_OK;
	}

#ifdef ESP_PLATFORM
	if (s_mgr.i2s_rx != NULL) {
		ESP_RETURN_ON_ERROR(i2s_channel_enable(s_mgr.i2s_rx), TAG, "i2s_channel_enable failed");  // NOLINT(bugprone-branch-clone)
		s_mgr.i2s_enabled = true;
	}
#endif

	s_mgr.running = true;
	if (s_mgr.task == NULL) {
		BaseType_t task_created = xTaskCreate(i2s_manager_task, "i2s_mgr", 4096, NULL, 5, &s_mgr.task);
		if (task_created != pdPASS) {
			s_mgr.running = false;
			return ESP_ERR_NO_MEM;
		}
	}
	return ESP_OK;
}

esp_err_t i2s_manager_stop(void)
{
	if (!s_mgr.initialized) {
		return ESP_ERR_INVALID_STATE;
	}
	s_mgr.running = false;
#ifdef ESP_PLATFORM
	if (s_mgr.i2s_rx != NULL && s_mgr.i2s_enabled) {
		i2s_channel_disable(s_mgr.i2s_rx);
		s_mgr.i2s_enabled = false;
	}
#endif
	for (int i = 0; i < 5 && s_mgr.task != NULL; ++i) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (s_mgr.task != NULL) {
		vTaskDelete(s_mgr.task);
		s_mgr.task = NULL;
	}
	return ESP_OK;
}

bool i2s_manager_is_running(void)
{
	return s_mgr.running;
}

esp_err_t i2s_manager_handle_frame(const uint8_t *data,
								   size_t len,
								   audio_bit_depth_t bit_depth,
								   audio_sample_rate_t sample_rate)
{
	return process_frame(data, len, bit_depth, sample_rate);
}

#ifdef CONFIG_BT_MOCK_TESTING
esp_err_t i2s_manager_mock_push(const uint8_t *data,
								size_t len,
								audio_bit_depth_t bit_depth,
								audio_sample_rate_t sample_rate)
{
	if (!s_mgr.initialized || s_mgr.mock_queue == NULL || data == NULL || len == 0) {
		return ESP_ERR_INVALID_STATE;
	}
	mock_item_t item = {
		.data = data,
		.len = len,
		.bit_depth = bit_depth,
		.rate = sample_rate,
	};
	if (xQueueSend(s_mgr.mock_queue, &item, pdMS_TO_TICKS(10)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}
	return ESP_OK;
}

QueueHandle_t i2s_manager_get_mock_queue(void)
{
	return s_mgr.mock_queue;
}
#endif
