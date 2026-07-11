/**
 * I2S manager: read/convert/resample capture audio and fill caller buffers.
 */

#include "i2s_manager.h"

#include <string.h>

#include "i2s_frame_extract.h"
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

/* I2S read timeout (CODE_REVIEW 2602101453, A1; retuned 2026-07-11)
 *
 * 8ms deliberately EXCEEDS AUDIO_ENGINE_TICK_MS: the I2S capture source
 * produces at exactly real-time (176.4 kB/s), so a sub-tick timeout made
 * most fills return short and the engine padded ~2/3 of the stream with
 * silence (heard as harsh chop). Blocking until a full raw chunk is
 * available paces chunk production to the source's real-time rate; the
 * 32KB output ring (~185ms) absorbs the slower tick cadence, and the
 * engine additionally skips silence-stuffing while a live I2S source is
 * mid-accumulation (see audio_engine_task).
 *
 * WHY SEPARATE: defined here to avoid a circular include
 * (audio_processor_internal.h includes this file's header). */
#define I2S_READ_TIMEOUT_MS  8

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
/* Which half-offsets carry the I2S link payload this session (see
 * i2s_frame_extract.h). I2S_FRAME_PHASE_NONE until first detection;
 * reset on every channel (re)enable — the phase shifts across enables. */
static int s_payload_phase = I2S_FRAME_PHASE_NONE;

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

	/* ROLE FLIP (2026-07-11): the WROOM32 is the I2S *master* receiver.
	 * ESP32-classic slave-RX is a known-broken silicon path (i2s_channel_read
	 * times out with 0 bytes even when BCLK/WS are physically present at the
	 * pads — verified via I2S_PROBE/I2S_RXTEST). ESP32-classic is reliable as
	 * master, so the WROOM32 now GENERATES BCLK+WS and the S3 sends data as an
	 * I2S slave synced to that clock. A true RX-only master drives the clock
	 * pads (as in I2S-mic setups); BCLK/WS moved off the DAC pins to GPIO18/19
	 * so the peripheral clock actually reaches the pins. */
	i2s_chan_config_t chan_cfg = {
		.id = cfg->i2s_port,
		.role = I2S_ROLE_MASTER,
		/* 8 x 128 frames: at 32-bit stereo a 32-frame desc is only 256 B, so
		 * 1 ms engine reads could never accumulate a full 2 KB raw chunk ->
		 * chronic short fills heard as chopped/static audio. 128-frame descs
		 * (1 KB) x 8 give ~23 ms of buffer. */
		.dma_desc_num = 8,
		.dma_frame_num = 128,
		.auto_clear = true,
	};

	/* Log the ACTUAL port/pins used: main.c defaults can be silently
	 * overridden from NVS, which cost a day of bring-up — keep this visible. */
	ESP_LOGI(TAG, "configure_i2s: port=%d role=MASTER bclk=%d ws=%d din=%d dout=%d",
	         (int)cfg->i2s_port, cfg->i2s_bclk_pin, cfg->i2s_ws_pin,
	         cfg->i2s_din_pin, cfg->i2s_dout_pin);

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
	/* APLL: the default PLL_160M clock is FRACTIONALLY divided (alternating
	 * /N,/N+1 periods = cycle-to-cycle BCLK jitter). Fine for DACs, but the
	 * S3's I2S slave never syncs to it, while it locks perfectly to a clean
	 * clock (proven via S3 on-chip loopback). APLL generates the exact audio
	 * clock — jitter-free. */
	std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
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
	
	/* Read budget: one chunk of OUTPUT audio (AUDIO_CHUNK_BLOCK_BYTES of s16)
	 * needs 2x that many RAW bytes when capturing 32-bit slots — capping the
	 * raw read at chunk size guaranteed every chunk was at most half real
	 * audio + half silence padding (heard as harsh static). Scale the budget
	 * by the capture width. */
	size_t raw_budget = AUDIO_CHUNK_BLOCK_BYTES;
	if (s_mgr.cfg.bit_depth == AUDIO_BIT_DEPTH_32) {
		raw_budget = AUDIO_CHUNK_BLOCK_BYTES * 2;
	}
	size_t read_bytes_limit = (s_mgr.bufs.raw_buf_bytes > raw_budget)
	                          ? raw_budget
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
	(void)ret;

	/* A short timeout with a PARTIAL read still carries valid samples — the
	 * driver hands back what the DMA had (ret=ESP_ERR_TIMEOUT, read_bytes>0).
	 * Discarding those partials threw away most of the stream (observed:
	 * ret=263 rb=256 on most ticks) and produced choppy/static audio. Only
	 * bail when literally nothing arrived. */
	if (read_bytes == 0) {
		return 0;  /* No data available */
	}

	/* 32-bit capture from the S3 slave (SPEC §3.3): each 32-bit slot carries
	 * 16 significant bits + 16 zero-pad bits, and the classic's capture
	 * lands the two payload halves of each frame at a phase that shifts per
	 * enable session (HW v1 <-> v2 pairing quirk). Detect the phase PER
	 * BLOCK (a one-time latch can lock onto the enable transient) and
	 * extract straight to s16 — host-tested in test_i2s_frame_extract.c. */
	if (s_mgr.cfg.bit_depth == AUDIO_BIT_DEPTH_32) {
		uint16_t *h = (uint16_t *)s_mgr.bufs.raw_buf;
		size_t nh = read_bytes / sizeof(uint16_t);
		int phase = i2s_frame_extract_detect(h, nh);
		if (phase == I2S_FRAME_PHASE_NONE) {
			phase = s_payload_phase;  /* silent block: reuse session phase */
		} else if (phase != s_payload_phase) {
			ESP_LOGI(TAG, "i2s payload phase: %d,%d",
			         (phase >> 4) & 0xF, phase & 0xF);
			s_payload_phase = phase;
		}
		if (phase < 0) {
			return 0;  /* phase never seen — deliver silence, not garbage */
		}
		/* In-place extraction is safe (the extractor reads each frame's
		 * halves before writing). Output is s16 stereo at the link rate —
		 * already the engine format, so copy directly; the generic
		 * convert/resample stages would be identity here (host-verified). */
		size_t nsamp = i2s_frame_extract(h, nh, phase,
		                                 (int16_t *)s_mgr.bufs.raw_buf);
		size_t nbytes = nsamp * sizeof(int16_t);
		size_t n = (nbytes < dst_bytes) ? nbytes : dst_bytes;
		util_safe_memcpy(dst, dst_bytes, s_mgr.bufs.raw_buf, n);
		return n;
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

esp_err_t i2s_manager_rxtest(uint32_t timeout_ms, size_t *out_bytes,
                             uint8_t *sample, size_t sample_cap, size_t *out_sample)
{
	if (out_bytes) { *out_bytes = 0; }
	if (out_sample) { *out_sample = 0; }

#if defined(ESP_PLATFORM) && !defined(CONFIG_BT_MOCK_TESTING)
	if (!s_mgr.initialized || s_mgr.i2s_rx == NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	/* Enable the RX master just for the test if the engine hasn't already.
	 * As a true master it generates and drives BCLK/WS. */
	bool was_enabled = s_mgr.i2s_enabled;
	if (!was_enabled) {
		esp_err_t en = i2s_channel_enable(s_mgr.i2s_rx);
		if (en != ESP_OK) {
			ESP_LOGE(TAG, "rxtest: rx enable failed %d", (int)en);
			return en;
		}
		s_mgr.i2s_enabled = true;
	}

	/* Hold the master clock running CONTINUOUSLY for ~timeout_ms by looping
	 * reads without disabling between them. A bursty (enable/read/disable)
	 * probe never lets an I2S-slave transmitter establish sync; continuous
	 * clocking does. Accumulate total bytes and capture the first non-zero
	 * sample window seen across the whole hold, so we can tell "clock works,
	 * silence" from "clock works, tone flowing". */
	static uint8_t buf[4096];
	/* ~176.4 bytes/ms at 44.1k stereo 32-bit slots; cap iterations for safety. */
	size_t target_bytes = (size_t)timeout_ms * 176u;
	size_t total_read = 0;
	esp_err_t ret = ESP_OK;
	bool got_sample = false;
	for (int it = 0; it < 4096 && total_read < target_bytes; it++) {
		size_t rb = 0;
		ret = i2s_channel_read(s_mgr.i2s_rx, buf, sizeof(buf), &rb, 200);
		if (ret != ESP_OK) { break; }
		total_read += rb;
		if (!got_sample && sample && sample_cap > 0) {
			for (size_t i = 0; i < rb; i++) {
				if (buf[i] != 0) {
					size_t avail = rb - i;
					size_t n = (avail < sample_cap) ? avail : sample_cap;
					memcpy(sample, buf + i, n);
					if (out_sample) { *out_sample = n; }
					got_sample = true;
					break;
				}
			}
		}
	}
	if (out_bytes) { *out_bytes = total_read; }

	/* Restore prior state so a normal stream isn't left half-enabled. */
	if (!was_enabled) {
		i2s_channel_disable(s_mgr.i2s_rx);
		s_mgr.i2s_enabled = false;
	}

	return ret;
#else
	(void)timeout_ms; (void)sample; (void)sample_cap;
	return ESP_ERR_NOT_SUPPORTED;
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

	s_payload_phase = I2S_FRAME_PHASE_NONE;  /* phase shifts per enable — re-detect */
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
