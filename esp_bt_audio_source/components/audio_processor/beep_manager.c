/**
 * Beep manager: stateful beep overlay for ring-buffer audio pipeline.
 *
 * The audio_processor remains the arbiter: it decides when a beep may run
 * and when it must stop. This module generates samples and mixes them into
 * caller-provided buffers via the overlay API.
 */

#include "beep_manager.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"

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
static bool s_initialized = false;
static beep_done_cb_t s_done_cb = NULL;
static void *s_done_ctx = NULL;

/**
 * Beep overlay state for ring buffer architecture (CODE_REVIEW6 Phase 3.3)
 * 
 * WHY: Ring buffer needs stateful beep overlay (not one-shot generation+enqueue)
 * HOW: Track phase, remaining frames, total duration for fade envelope
 * CORRECTNESS: Protected by spinlock, reset on stop/completion
 */
typedef struct {
	bool active;
	double phase;              /* Current sine wave phase (0 to 2π) */
	double phase_inc;          /* Phase increment per sample */
	uint64_t frames_generated; /* Frames generated so far */
	uint64_t total_frames;     /* Total frames for this beep */
	size_t fade_frames;        /* Fade in/out duration in frames */
	uint16_t amplitude_16;     /* Amplitude for 16-bit samples */
	uint32_t amplitude_32;     /* Amplitude for 32-bit samples */
} beep_overlay_state_t;

static beep_overlay_state_t s_overlay = {0};

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
	s_initialized = true;
	return ESP_OK;
}

void beep_manager_deinit(void)
{
	BEEP_ENTER_CRITICAL();
	s_overlay.active = false;
	BEEP_EXIT_CRITICAL();
	s_initialized = false;
}

void beep_manager_set_done_callback(beep_done_cb_t callback, void *ctx)
{
	s_done_cb = callback;
	s_done_ctx = ctx;
}

esp_err_t beep_manager_play(const beep_request_t *req, const audio_config_t *cfg)
{
	if (!s_initialized) {
		esp_err_t init_ret = beep_manager_init();
		if (init_ret != ESP_OK) {
			return init_ret;
		}
	}

	return beep_overlay_start(req, cfg);
}

void beep_manager_stop(void)
{
	BEEP_ENTER_CRITICAL();
	s_overlay.active = false;
	BEEP_EXIT_CRITICAL();
}

beep_state_t beep_manager_get_state(void)
{
	return beep_overlay_is_active() ? BEEP_STATE_PLAYING : BEEP_STATE_STOPPED;
}

/**
 * Clamp 32-bit value to 16-bit signed range (CODE_REVIEW6 Phase 3.3)
 */
static inline int16_t clamp_int16(int32_t val)
{
	if (val > INT16_MAX) return INT16_MAX;
	if (val < INT16_MIN) return INT16_MIN;
	return (int16_t)val;
}

/**
 * Clamp 64-bit value to 32-bit signed range (CODE_REVIEW6 Phase 3.3)
 */
static inline int32_t clamp_int32(int64_t val)
{
	if (val > INT32_MAX) return INT32_MAX;
	if (val < INT32_MIN) return INT32_MIN;
	return (int32_t)val;
}

esp_err_t beep_overlay_start(const beep_request_t *req, const audio_config_t *cfg)
{
	if (req == NULL || cfg == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	const size_t sample_bytes = bytes_per_sample(cfg->bit_depth);
	if (sample_bytes == 0) {
		return ESP_ERR_NOT_SUPPORTED;
	}
	
	BEEP_ENTER_CRITICAL();
	if (s_overlay.active) {
		BEEP_EXIT_CRITICAL();
		return ESP_ERR_INVALID_STATE;
	}
	BEEP_EXIT_CRITICAL();
	
	/* Calculate beep parameters */
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
		return ESP_ERR_INVALID_ARG;
	}
	
	const uint64_t total_frames = ((uint64_t)duration_ms * (uint64_t)sample_rate) / 1000ULL;
	if (total_frames == 0ULL) {
		return ESP_ERR_INVALID_SIZE;
	}
	
	const size_t fade_frames = ((uint64_t)sample_rate * (uint64_t)BEEP_FADE_MS) / 1000ULL;
	const double two_pi = 2.0 * M_PI;
	const double phase_inc = two_pi * freq_hz / (double)sample_rate;
	
	/* Initialize overlay state */
	BEEP_ENTER_CRITICAL();
	s_overlay.active = true;
	s_overlay.phase = 0.0;
	s_overlay.phase_inc = phase_inc;
	s_overlay.frames_generated = 0;
	s_overlay.total_frames = total_frames;
	s_overlay.fade_frames = fade_frames;
	s_overlay.amplitude_16 = amplitude_16;
	s_overlay.amplitude_32 = amplitude_32;
	BEEP_EXIT_CRITICAL();
	
	ESP_LOGI(TAG, "beep_overlay_start: dur=%ums freq=%.1fHz amp=%u frames=%llu",
	         duration_ms, freq_hz, amplitude_16, total_frames);
	
	return ESP_OK;
}

bool beep_overlay_is_active(void)
{
	bool active;
	BEEP_ENTER_CRITICAL();
	active = s_overlay.active;
	BEEP_EXIT_CRITICAL();
	return active;
}

void beep_overlay_stop(void)
{
	BEEP_ENTER_CRITICAL();
	s_overlay.active = false;
	s_overlay.frames_generated = s_overlay.total_frames;
	BEEP_EXIT_CRITICAL();
}

void beep_overlay_fill(uint8_t *buffer, size_t bytes, const audio_config_t *cfg)
{
	if (buffer == NULL || bytes == 0 || cfg == NULL) {
		return;
	}
	
	BEEP_ENTER_CRITICAL();
	if (!s_overlay.active) {
		BEEP_EXIT_CRITICAL();
		return;  /* No beep active - nothing to mix */
	}
	
	/* Check if beep completed */
	if (s_overlay.frames_generated >= s_overlay.total_frames) {
		s_overlay.active = false;
		BEEP_EXIT_CRITICAL();
		
		/* Notify completion */
		if (s_done_cb != NULL) {
			s_done_cb(s_done_ctx);
		}
		return;
	}
	
	/* Copy state to local variables (minimize critical section) */
	double phase = s_overlay.phase;
	double phase_inc = s_overlay.phase_inc;
	uint64_t frames_generated = s_overlay.frames_generated;
	uint64_t total_frames = s_overlay.total_frames;
	size_t fade_frames = s_overlay.fade_frames;
	uint16_t amplitude_16 = s_overlay.amplitude_16;
	uint32_t amplitude_32 = s_overlay.amplitude_32;
	BEEP_EXIT_CRITICAL();
	
	/* Calculate frame parameters */
	const size_t sample_bytes = bytes_per_sample(cfg->bit_depth);
	if (sample_bytes == 0) {
		return;
	}
	
	const uint32_t channels = (cfg->channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
	const size_t frame_bytes = sample_bytes * (size_t)channels;
	if (frame_bytes == 0) {
		return;
	}
	
	const size_t num_frames = bytes / frame_bytes;
	const size_t frames_to_mix = (frames_generated + num_frames > total_frames) 
	                              ? (size_t)(total_frames - frames_generated) 
	                              : num_frames;
	
	/* Mix beep samples into buffer (CODE_REVIEW6 Phase 3.3 mixing formula) */
	const double two_pi = 2.0 * M_PI;
	
	for (size_t f = 0; f < frames_to_mix; ++f) {
		/* Calculate fade envelope */
		double env = fade_env((size_t)frames_generated + f, (size_t)total_frames, fade_frames);
		double beep_sample = sin(phase) * env;
		
		/* Mix into existing buffer with scaling (base*0.7 + beep*0.5) */
		if (cfg->bit_depth == AUDIO_BIT_DEPTH_16) {
			int16_t beep_val = (int16_t)(beep_sample * (double)amplitude_16);
			int16_t *frame = (int16_t *)(buffer + (f * frame_bytes));
			
			for (uint32_t ch = 0; ch < channels; ++ch) {
				int32_t base = frame[ch];
				int32_t mixed = (base * 7 / 10) + (beep_val * 5 / 10);
				frame[ch] = clamp_int16(mixed);
			}
		} else if (cfg->bit_depth == AUDIO_BIT_DEPTH_32) {
			int32_t beep_val = (int32_t)(beep_sample * (double)amplitude_32);
			int32_t *frame = (int32_t *)(buffer + (f * frame_bytes));
			
			for (uint32_t ch = 0; ch < channels; ++ch) {
				int64_t base = frame[ch];
				int64_t mixed = (base * 7 / 10) + (beep_val * 5 / 10);
				frame[ch] = clamp_int32(mixed);
			}
		}
		
		/* Advance phase */
		phase += phase_inc;
		if (phase >= two_pi) {
			phase -= two_pi;
		}
	}
	
	/* Update state */
	BEEP_ENTER_CRITICAL();
	if (s_overlay.active) {
		s_overlay.phase = phase;
		s_overlay.frames_generated += frames_to_mix;
	}
	BEEP_EXIT_CRITICAL();
}
