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
/* DBG-I2SCAP: raised 1 -> 8ms. The I2S capture source produces at exactly
 * 176.4 kB/s (real-time), but the engine tick demands chunks at ~512 kB/s —
 * with a 1ms timeout most fills came back short and the engine padded ~2/3
 * of the stream with silence (heard as harsh chop). Letting the read block
 * until a full chunk of raw data is available paces production to the
 * source's real-time rate; the 32KB output ring (185ms) absorbs the slower
 * tick cadence. */
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
/* DBG-I2SCAP: which 16-bit half-stream carries the payload this session
 * (-1 = not yet detected; re-detected after each channel (re)enable). */
static int s_payload_phase = -1;

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

	/* DBG-I2SCAP: log the ACTUAL port/pins used (main.c defaults can be
	 * overridden from NVS — rule out stale saved pins fighting the tests). */
	ESP_LOGW(TAG, "DBG-I2SCAP configure_i2s: port=%d role=MASTER bclk=%d ws=%d din=%d dout=%d",
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
	
	{
		static unsigned s_dbg_isf = 0;
		if ((s_dbg_isf++ & 0xFFU) == 0U) {
			ESP_LOGW(TAG, "DBG-I2SCAP i2s_read ret=%d read_bytes=%u limit=%u",
			         (int)ret, (unsigned)read_bytes, (unsigned)read_bytes_limit);
		}
	}

	/* A short timeout with a PARTIAL read still carries valid samples — the
	 * driver hands back what the DMA had (ret=ESP_ERR_TIMEOUT, read_bytes>0).
	 * Discarding those partials threw away most of the stream (observed:
	 * ret=263 rb=256 on most ticks) and produced choppy/static audio. Only
	 * bail when literally nothing arrived. */
	if (read_bytes == 0) {
		return 0;  /* No data available */
	}

	/* 32-bit capture from the S3 slave: the link contract puts 16 significant
	 * bits + 16 zero-pad bits in each 32-bit slot, but the classic's capture
	 * phase shifts by 16 bits per enable session (HW v1 <-> v2 pairing quirk).
	 * Viewed as a flat stream of 16-bit halves, the payload always occupies a
	 * stride-2 sub-stream at EVEN or ODD parity — detect the live parity once
	 * per session (the padding halves are all zero by contract) and extract
	 * the payload halves directly to s16. */
	size_t conv_in_bytes = read_bytes;
	audio_bit_depth_t conv_depth = s_mgr.cfg.bit_depth;
	if (s_mgr.cfg.bit_depth == AUDIO_BIT_DEPTH_32) {
		/* One stereo frame = 2x 32-bit words = 4x 16-bit halves. The link
		 * contract fills exactly TWO of the four halves with the L/R payload
		 * (the other two are zero pad), but WHICH two shifts per enable
		 * session (observed: {1,3}, {2,3}, ...). Detect the two energetic
		 * offsets once per session, emit them in wire order (word first,
		 * high half before low within a word — MSB is on the wire first). */
		uint16_t *h = (uint16_t *)s_mgr.bufs.raw_buf;
		size_t nh = read_bytes / sizeof(uint16_t);
		size_t nframes = nh / 4;
		{
			/* DBG-I2SCAP: aligned raw words, straight from DMA. */
			static unsigned s_dbg_raw = 0;
			if ((s_dbg_raw++ & 0xFFU) == 0U && read_bytes >= 16) {
				const uint32_t *rw = (const uint32_t *)s_mgr.bufs.raw_buf;
				ESP_LOGW(TAG, "DBG-I2SCAP raw words: %08x %08x %08x %08x",
				         (unsigned)rw[0], (unsigned)rw[1],
				         (unsigned)rw[2], (unsigned)rw[3]);
			}
		}
		/* Detect PER BLOCK (no latch): a one-time latch at engine start can
		 * lock onto the enable transient and stay wrong forever. Detection
		 * over this block's frames is ~1k adds — negligible — and the phase
		 * is constant within a session, so per-block answers are stable. */
		if (nframes >= 16) {
			uint32_t e[4] = {0, 0, 0, 0};
			size_t scan = (nframes < 256) ? nframes : 256;
			for (size_t f = 0; f < scan; f++) {
				for (int o = 0; o < 4; o++) {
					int16_t v = (int16_t)h[4 * f + o];
					e[o] += (uint32_t)(v < 0 ? -v : v);
				}
			}
			int a = 0;
			for (int o = 1; o < 4; o++) { if (e[o] > e[a]) a = o; }
			int b = (a == 0) ? 1 : 0;
			for (int o = 0; o < 4; o++) { if (o != a && e[o] > e[b]) b = o; }
			if (e[a] > 0) {
				/* temporal order: by word, high half (LE odd) before low */
				int ka = (a >> 1) * 2 + ((a & 1) ? 0 : 1);
				int kb = (b >> 1) * 2 + ((b & 1) ? 0 : 1);
				int first = (ka < kb) ? a : b;
				int second = (ka < kb) ? b : a;
				int phase = (first << 4) | second;
				if (phase != s_payload_phase) {
					ESP_LOGW(TAG, "DBG-I2SCAP payload halves: %d,%d (e=%u,%u,%u,%u)",
					         first, second,
					         (unsigned)e[0], (unsigned)e[1], (unsigned)e[2], (unsigned)e[3]);
					s_payload_phase = phase;
				}
			}
		}
		if (s_payload_phase >= 0) {
			int offL = (s_payload_phase >> 4) & 0xF;
			int offR = s_payload_phase & 0xF;
			int16_t *out16 = (int16_t *)s_mgr.bufs.raw_buf;
			for (size_t f = 0; f < nframes; f++) {  /* in-place: reads ahead */
				out16[2 * f] = (int16_t)h[4 * f + offL];
				out16[2 * f + 1] = (int16_t)h[4 * f + offR];
			}
			conv_in_bytes = nframes * 2 * sizeof(int16_t);
			conv_depth = AUDIO_BIT_DEPTH_16;
			/* Extracted data is ALREADY s16 stereo at the pad's 44.1kHz —
			 * exactly the engine format. Copy directly; the generic
			 * convert/resample path is bypassed (suspected of interleaving
			 * zeros on this 16-bit input; also pure overhead here). */
			size_t n = (conv_in_bytes < dst_bytes) ? conv_in_bytes : dst_bytes;
			util_safe_memcpy(dst, dst_bytes, s_mgr.bufs.raw_buf, n);
			{
				static unsigned s_dbg_dst2 = 0;
				if ((s_dbg_dst2++ & 0x3FU) == 0U && n >= 8) {
					const int16_t *s = (const int16_t *)dst;
					ESP_LOGW(TAG, "DBG-I2SCAP direct out=%u dst16=[%d,%d,%d,%d]",
					         (unsigned)n, (int)s[0], (int)s[1], (int)s[2], (int)s[3]);
				}
			}
			return n;
		} else {
			return 0;  /* phase unknown yet — deliver silence, not garbage */
		}
	}

	size_t out = convert_and_resample_to_dst(s_mgr.bufs.raw_buf,
	                                         conv_in_bytes,
	                                         conv_depth,
	                                         s_mgr.cfg.sample_rate,
	                                         dst,
	                                         dst_bytes);
	{
		/* DBG-I2SCAP: dump what the engine actually receives. */
		static unsigned s_dbg_dst = 0;
		if ((s_dbg_dst++ & 0x3FU) == 0U && out >= 8) {
			const int16_t *s = (const int16_t *)dst;
			ESP_LOGW(TAG, "DBG-I2SCAP fill raw=%u out=%u dst16=[%d,%d,%d,%d]",
			         (unsigned)read_bytes, (unsigned)out,
			         (int)s[0], (int)s[1], (int)s[2], (int)s[3]);
		}
	}
	return out;
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
			ESP_LOGE(TAG, "DBG-I2SCAP rxtest: rx enable failed %d", (int)en);
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
	size_t read_bytes = total_read;
	if (out_bytes) { *out_bytes = total_read; }

	/* Restore prior state so a normal stream isn't left half-enabled. */
	if (!was_enabled) {
		i2s_channel_disable(s_mgr.i2s_rx);
		s_mgr.i2s_enabled = false;
	}

	ESP_LOGW(TAG, "DBG-I2SCAP rxtest: ret=%d read_bytes=%u timeout=%ums",
	         (int)ret, (unsigned)read_bytes, (unsigned)timeout_ms);
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
	/* DBG-I2SCAP: trace start attempts while diagnosing I2S capture. */
	ESP_LOGW(TAG, "DBG-I2SCAP i2s_manager_start: initialized=%d running=%d rx=%p",
	         (int)s_mgr.initialized, (int)s_mgr.running, (void *)s_mgr.i2s_rx);
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

	s_payload_phase = -1;  /* capture phase shifts per enable — re-detect */
	s_mgr.running = true;
	return ESP_OK;
}

esp_err_t i2s_manager_stop(void)
{
	ESP_LOGW(TAG, "DBG-I2SCAP i2s_manager_stop: initialized=%d running=%d",
	         (int)s_mgr.initialized, (int)s_mgr.running);
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
