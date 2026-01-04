/**
 * Beep manager: generate short sine-wave beeps into the shared audio_queue.
 *
 * The audio_processor remains the arbiter: it decides when a beep may run
 * and when it must stop. This module only generates audio frames and enqueues
 * them with caller-supplied audio format parameters. Tag accounting is handled
 * locally via a rolling tag_id that increments per enqueued chunk.
 */

#include "beep_manager.h"

#include <math.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_queue.h"
#include "esp_heap_caps.h"

static const char *TAG = "beep_manager";

#define BEEP_MAX_DURATION_MS     20000U
#define BEEP_DEFAULT_DURATION_MS 50U
#define BEEP_FADE_MS             10U
#define BEEP_DEFAULT_FREQ_HZ     1000.0
#define BEEP_DEFAULT_AMPLITUDE   7500U

#ifdef ESP_PLATFORM
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
#define BEEP_ENTER_CRITICAL() portENTER_CRITICAL(&s_lock)
#define BEEP_EXIT_CRITICAL() portEXIT_CRITICAL(&s_lock)
#else
static int s_lock = 0;
#define BEEP_ENTER_CRITICAL() ((void)s_lock)
#define BEEP_EXIT_CRITICAL() ((void)s_lock)
#endif
static bool s_stop_requested = false;
static uint16_t s_tag_id = 0;
static beep_state_t s_state = BEEP_STATE_STOPPED;
static bool s_initialized = false;
static beep_done_cb_t s_done_cb = NULL;
static void *s_done_ctx = NULL;

static size_t bytes_per_sample(audio_bit_depth_t bit_depth)
{
	switch (bit_depth) {
	case AUDIO_BIT_DEPTH_16:
		return sizeof(int16_t);
	case AUDIO_BIT_DEPTH_32:
		return sizeof(int32_t);
	default:
		return 0;
	}
}

static double fade_env(size_t frame_idx, size_t total_frames, size_t fade_frames)
{
	if (fade_frames == 0 || total_frames <= (fade_frames * 2U)) {
		return 1.0;
	}

	if (frame_idx < fade_frames) {
		double t = (double)frame_idx / (double)fade_frames;
		return 0.5 * (1.0 - cos(M_PI * t));
	}

	if ((total_frames - frame_idx) <= fade_frames) {
		size_t tail = total_frames - frame_idx;
		double t = (double)tail / (double)fade_frames;
		return 0.5 * (1.0 - cos(M_PI * t));
	}

	return 1.0;
}

esp_err_t beep_manager_init(void)
{
	if (s_initialized) {
		return ESP_OK;
	}

	if (!audio_chunk_pool_init()) {
		return ESP_ERR_NO_MEM;
	}

	s_stop_requested = false;
	__atomic_store_n(&s_tag_id, 0, __ATOMIC_SEQ_CST);
	BEEP_ENTER_CRITICAL();
	s_state = BEEP_STATE_STOPPED;
	BEEP_EXIT_CRITICAL();
	s_initialized = true;
	return ESP_OK;
}

void beep_manager_deinit(void)
{
	BEEP_ENTER_CRITICAL();
	s_state = BEEP_STATE_STOPPED;
	BEEP_EXIT_CRITICAL();
	s_stop_requested = false;
	s_initialized = false;
}

void beep_manager_set_done_callback(beep_done_cb_t cb, void *ctx)
{
	s_done_cb = cb;
	s_done_ctx = ctx;
}

esp_err_t beep_manager_play(const beep_request_t *req, const audio_config_t *cfg)
{
	if (req == NULL || cfg == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (!s_initialized) {
		esp_err_t init_ret = beep_manager_init();
		if (init_ret != ESP_OK) {
			return init_ret;
		}
	}

	const size_t sample_bytes = bytes_per_sample(cfg->bit_depth);
	if (sample_bytes == 0) {
		ESP_LOGW(TAG, "unsupported bit depth %d", (int)cfg->bit_depth);
		return ESP_ERR_NOT_SUPPORTED;
	}

	const uint32_t channels = (cfg->channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
	const size_t frame_bytes = sample_bytes * (size_t)channels;
	if (frame_bytes == 0) {
		return ESP_ERR_INVALID_ARG;
	}

	BEEP_ENTER_CRITICAL();
	if (s_state == BEEP_STATE_PLAYING) {
		BEEP_EXIT_CRITICAL();
		return ESP_ERR_INVALID_STATE;
	}
	s_state = BEEP_STATE_PLAYING;
	BEEP_EXIT_CRITICAL();
	s_stop_requested = false;

	uint32_t duration_ms = req->duration_ms;
	if (duration_ms == 0) {
		duration_ms = BEEP_DEFAULT_DURATION_MS;
	} else if (duration_ms > BEEP_MAX_DURATION_MS) {
		duration_ms = BEEP_MAX_DURATION_MS;
	}

	double freq_hz = (req->freq_hz > 0.0) ? req->freq_hz : BEEP_DEFAULT_FREQ_HZ;
	uint16_t amplitude_16 = (req->amplitude > 0U) ? req->amplitude : BEEP_DEFAULT_AMPLITUDE;
	uint32_t amplitude_32 = ((uint32_t)amplitude_16) << 16;

	const uint32_t sample_rate = (uint32_t)cfg->sample_rate;
	if (sample_rate == 0U) {
		ESP_LOGW(TAG, "invalid sample rate");
		BEEP_ENTER_CRITICAL();
		s_state = BEEP_STATE_STOPPED;
		BEEP_EXIT_CRITICAL();
		return ESP_ERR_INVALID_ARG;
	}

	const uint64_t total_frames = ((uint64_t)duration_ms * (uint64_t)sample_rate) / 1000ULL;
	if (total_frames == 0ULL) {
		BEEP_ENTER_CRITICAL();
		s_state = BEEP_STATE_STOPPED;
		BEEP_EXIT_CRITICAL();
		return ESP_ERR_INVALID_SIZE;
	}

	const size_t fade_frames = ((uint64_t)sample_rate * (uint64_t)BEEP_FADE_MS) / 1000ULL;
	const double two_pi = 2.0 * M_PI;
	const double phase_inc = two_pi * freq_hz / (double)sample_rate;
	double phase = 0.0;

	const size_t frames_per_chunk = frame_bytes > 0 ? (AUDIO_CHUNK_BLOCK_BYTES / frame_bytes) : 0;
	if (frames_per_chunk == 0) {
		BEEP_ENTER_CRITICAL();
		s_state = BEEP_STATE_STOPPED;
		BEEP_EXIT_CRITICAL();
		return ESP_ERR_INVALID_SIZE;
	}

	uint8_t *chunk = heap_caps_malloc((size_t)AUDIO_CHUNK_BLOCK_BYTES, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
	if (chunk == NULL) {
		ESP_LOGW(TAG, "beep_manager_play: failed to allocate chunk buffer");
		BEEP_ENTER_CRITICAL();
		s_state = BEEP_STATE_STOPPED;
		BEEP_EXIT_CRITICAL();
		return ESP_ERR_NO_MEM;
	}
	uint64_t frames_generated = 0;
	bool enqueued_any = false;

	while (frames_generated < total_frames) {
		if (__atomic_load_n(&s_stop_requested, __ATOMIC_RELAXED)) {
			break;
		}

		size_t frames_this = (size_t)(total_frames - frames_generated);
		if (frames_this > frames_per_chunk) {
			frames_this = frames_per_chunk;
		}

		for (size_t f = 0; f < frames_this; ++f) {
			double env = fade_env((size_t)frames_generated + f, (size_t)total_frames, fade_frames);
			double sample = sin(phase) * env;
			if (cfg->bit_depth == AUDIO_BIT_DEPTH_16) {
				int16_t s = (int16_t)(sample * (double)amplitude_16);
				int16_t *out16 = (int16_t *)(chunk + (f * frame_bytes));
				for (uint32_t ch = 0; ch < channels; ++ch) {
					out16[ch] = s;
				}
			} else {
				int32_t s = (int32_t)(sample * (double)amplitude_32);
				int32_t *out32 = (int32_t *)(chunk + (f * frame_bytes));
				for (uint32_t ch = 0; ch < channels; ++ch) {
					out32[ch] = s;
				}
			}
			phase += phase_inc;
			if (phase >= two_pi) {
				phase -= two_pi;
			}
		}

		size_t chunk_bytes = frames_this * frame_bytes;
		uint16_t tag_id = __atomic_fetch_add(&s_tag_id, 1, __ATOMIC_SEQ_CST);
		if (!audio_chunk_enqueue_bytes_with_id(chunk, chunk_bytes, AUDIO_SOURCE_TAG_BEEP, tag_id)) {
			ESP_LOGW(TAG, "beep enqueue failed tag_id=%u", (unsigned)tag_id);
			break;
		}

		frames_generated += frames_this;
		taskYIELD();
		enqueued_any = true;
	}

	BEEP_ENTER_CRITICAL();
	s_state = BEEP_STATE_STOPPED;
	BEEP_EXIT_CRITICAL();

	if (s_done_cb != NULL) {
		s_done_cb(s_done_ctx);
	}

	__atomic_store_n(&s_stop_requested, false, __ATOMIC_RELAXED);
	heap_caps_free(chunk);
	return enqueued_any ? ESP_OK : ESP_ERR_NO_MEM;
}

void beep_manager_stop(void)
{
	__atomic_store_n(&s_stop_requested, true, __ATOMIC_RELAXED);
}

beep_state_t beep_manager_get_state(void)
{
	beep_state_t st;
	BEEP_ENTER_CRITICAL();
	st = s_state;
	BEEP_EXIT_CRITICAL();
	return st;
}
