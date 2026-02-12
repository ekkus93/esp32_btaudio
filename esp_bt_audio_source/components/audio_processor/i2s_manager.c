/**
 * I2S manager: read/convert/resample capture audio and fill caller buffers.
 */

#include "i2s_manager.h"

#include <string.h>

#include "util_safe.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, msg) do { esp_err_t __err = (x); if (__err != ESP_OK) { ESP_LOGE(tag, "%s: %d", msg, __err); return __err; } } while (0)
#endif

#if defined(ESP_PLATFORM)
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
#elif defined(CONFIG_BT_MOCK_TESTING)
/* Host test build - use mock headers */
#include "driver/i2s_std.h"
#endif

/* I2S read timeout (CODE_REVIEW 2602101453, A1)
 * 
 * TIMING RELATIONSHIP: Must be < AUDIO_ENGINE_TICK_MS (defined in audio_processor_internal.h)
 *                      to leave headroom for format conversion and resampling overhead.
 * 
 * CURRENT VALUES: I2S timeout = 1ms, Engine tick = 2ms → 1ms processing headroom
 * 
 * WHY SEPARATE: Defined here to avoid circular include (audio_processor_internal.h
 *               includes this file's header). Value must stay synchronized manually.
 * 
 * ON CHANGE: If AUDIO_ENGINE_TICK_MS changes, update this value to maintain
 *            relationship: I2S_READ_TIMEOUT_MS = AUDIO_ENGINE_TICK_MS - 1
 * 
 * BEHAVIOR: Quick timeout ensures non-blocking returns from i2s_channel_read().
 *           Audio engine proceeds with silence chunk if I2S source is slow/disconnected,
 *           maintaining real-time pipeline responsiveness.
 */
#define I2S_READ_TIMEOUT_MS  1  /* Synchronized with AUDIO_ENGINE_TICK_MS=2 (2ms - 1ms overhead) */

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
#if defined(ESP_PLATFORM) || defined(CONFIG_BT_MOCK_TESTING)
	i2s_chan_handle_t i2s_rx;
#endif
#ifdef CONFIG_BT_MOCK_TESTING
	QueueHandle_t mock_queue;
#endif
} i2s_manager_state_t;

static i2s_manager_state_t s_mgr = {0};

#if defined(ESP_PLATFORM) || defined(CONFIG_BT_MOCK_TESTING)
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
	std_cfg.slot_cfg.data_bit_width = bit_width;
	std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
	std_cfg.slot_cfg.slot_mask = (cfg->channels == AUDIO_CHANNEL_MONO) ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH;
	std_cfg.slot_cfg.ws_width = 32;
	std_cfg.slot_cfg.ws_pol = false;
	std_cfg.slot_cfg.bit_shift = true;
#else
	std_cfg.clk_cfg.sample_rate_hz = cfg->sample_rate;
	std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
	std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
	std_cfg.slot_cfg.data_bit_width = bit_width;
	std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
	std_cfg.slot_cfg.slot_mode = cfg->channels == AUDIO_CHANNEL_MONO ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
	std_cfg.slot_cfg.slot_mask = (cfg->channels == AUDIO_CHANNEL_MONO) ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH;
	std_cfg.slot_cfg.ws_width = 32;
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

static size_t convert_and_resample_to_dst(const uint8_t *data,
                                          size_t len,
                                          audio_bit_depth_t bit_depth,
                                          audio_sample_rate_t sample_rate,
                                          uint8_t *dst,
                                          size_t dst_bytes)
{
	if (!s_mgr.initialized || data == NULL || len == 0 || dst == NULL || dst_bytes == 0) {
		return 0;
	}

	if (s_mgr.bufs.proc_buf == NULL || s_mgr.bufs.proc_buf2 == NULL) {
		return 0;
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
	if (convert_audio_format(&conv_args) != ESP_OK || conv_size == 0) {
		return 0;
	}

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
	if (resample_audio(&res_args) != ESP_OK || res_size == 0) {
		return 0;
	}

	size_t copy_bytes = (res_size < dst_bytes) ? res_size : dst_bytes;
	util_safe_memcpy(dst, dst_bytes, s_mgr.bufs.proc_buf2, copy_bytes);
	return copy_bytes;
}

/**
 * I2S source fill API for ring buffer architecture (CODE_REVIEW6 Phase 3.2)
 * 
 * WHY: Ring buffer architecture needs direct buffer fills instead of queue enqueue.
 *      Eliminates I2S truncation issue by writing exactly what was captured.
 * 
 * HOW: Reads from I2S DMA, converts/resamples, writes directly to caller's buffer.
 *      Handles format conversion same as process_frame() but without enqueue.
 * 
 * CORRECTNESS: Never blocks (quick return if no data), handles conversion errors,
 *              returns 0 if not running/initialized (caller fills silence).
 */
size_t i2s_source_fill(uint8_t *dst, size_t dst_bytes)
{
	if (dst == NULL || dst_bytes == 0) {
		return 0;
	}
	
	if (!s_mgr.initialized || !s_mgr.running) {
		return 0;  /* Not running - return silence */
	}
	
	if (s_mgr.bufs.proc_buf == NULL || s_mgr.bufs.proc_buf2 == NULL) {
		return 0;
	}

#ifdef CONFIG_BT_MOCK_TESTING
	if (s_mgr.mock_queue != NULL) {
		mock_item_t item;
		if (xQueueReceive(s_mgr.mock_queue, &item, 0) == pdTRUE) {
			return convert_and_resample_to_dst(item.data,
			                                   item.len,
			                                   item.bit_depth,
			                                   item.rate,
			                                   dst,
			                                   dst_bytes);
		}
	}
#endif

#ifdef ESP_PLATFORM
	if (s_mgr.i2s_rx == NULL || s_mgr.bufs.raw_buf == NULL || s_mgr.bufs.raw_buf_bytes == 0) {
		return 0;  /* I2S not configured */
	}
	
	/* Limit read size to AUDIO_CHUNK_BLOCK_BYTES (same as task loop) */
	size_t read_bytes_limit = (s_mgr.bufs.raw_buf_bytes > AUDIO_CHUNK_BLOCK_BYTES)
	                          ? AUDIO_CHUNK_BLOCK_BYTES
	                          : s_mgr.bufs.raw_buf_bytes;
	
	/* Read from I2S DMA with timeout (CODE_REVIEW 2602101453, A1)
	 * 
	 * TIMING: Timeout < AUDIO_ENGINE_TICK_MS to prevent task overrun
	 *         Current: 1ms timeout, 2ms tick → 1ms headroom for processing
	 * 
	 * ON TIMEOUT: Returns 0 (silence) - audio engine proceeds without blocking
	 *             Engine produces silence chunk, maintaining real-time pipeline
	 * 
	 * WHY SHORT: Audio engine expects non-blocking behavior; quick timeout maintains
	 *            real-time responsiveness even when I2S source is slow/disconnected */
	size_t read_bytes = 0;
	esp_err_t ret = i2s_channel_read(s_mgr.i2s_rx,
	                                  s_mgr.bufs.raw_buf,
	                                  read_bytes_limit,
	                                  &read_bytes,
	                                  I2S_READ_TIMEOUT_MS);
	
	if (ret != ESP_OK || read_bytes == 0) {
		return 0;  /* No data available or timeout */
	}

	return convert_and_resample_to_dst(s_mgr.bufs.raw_buf,
	                                   read_bytes,
	                                   s_mgr.cfg.bit_depth,
	                                   s_mgr.cfg.sample_rate,
	                                   dst,
	                                   dst_bytes);
#else
	/* Non-ESP platform (host tests) - return silence */
	(void)dst_bytes;
	return 0;
#endif
}

esp_err_t i2s_manager_init(const audio_config_t *config, const i2s_manager_buffers_t *buffers)
{
	if (config == NULL || buffers == NULL || buffers->proc_buf == NULL || buffers->proc_buf2 == NULL || buffers->work_bytes == 0) {
		return ESP_ERR_INVALID_ARG;
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

#if defined(ESP_PLATFORM) || defined(CONFIG_BT_MOCK_TESTING)
	esp_err_t ret = configure_i2s(&s_mgr.cfg);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "configure_i2s failed");
#ifdef CONFIG_BT_MOCK_TESTING
		if (s_mgr.mock_queue) {
			vQueueDelete(s_mgr.mock_queue);
			s_mgr.mock_queue = NULL;
		}
#endif
		return ret;
	}
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

#if defined(ESP_PLATFORM) || defined(CONFIG_BT_MOCK_TESTING)
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

#if defined(ESP_PLATFORM) || defined(CONFIG_BT_MOCK_TESTING)
	if (s_mgr.i2s_rx != NULL) {
		ESP_RETURN_ON_ERROR(i2s_channel_enable(s_mgr.i2s_rx), TAG, "i2s_channel_enable failed");  // NOLINT(bugprone-branch-clone)
		s_mgr.i2s_enabled = true;
	}
#endif

	s_mgr.running = true;
	return ESP_OK;
}

esp_err_t i2s_manager_stop(void)
{
	if (!s_mgr.initialized) {
		return ESP_ERR_INVALID_STATE;
	}
	s_mgr.running = false;
#if defined(ESP_PLATFORM) || defined(CONFIG_BT_MOCK_TESTING)
	if (s_mgr.i2s_rx != NULL && s_mgr.i2s_enabled) {
		i2s_channel_disable(s_mgr.i2s_rx);
		s_mgr.i2s_enabled = false;
	}
#endif
	return ESP_OK;
}

bool i2s_manager_is_running(void)
{
	return s_mgr.running;
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
