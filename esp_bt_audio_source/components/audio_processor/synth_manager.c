#include <math.h>
#include <stdint.h>
#include "synth_manager.h"

#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
static double s_synth_phase = 0.0;
static double s_synth_phase_inc = 0.0;
static double s_synth_env = 0.0;
static bool s_synth_fade_active = false;
static int s_synth_fade_dir = 0;
static size_t s_synth_fade_frames_remaining = 0;
static size_t s_synth_fade_frames_total = 0;

static int synth_bytes_per_sample(audio_bit_depth_t bit_depth)
{
	switch (bit_depth) {
	case AUDIO_BIT_DEPTH_16:
		return 2;
	case AUDIO_BIT_DEPTH_24:
		return 4; /* store in 32-bit container */
	case AUDIO_BIT_DEPTH_32:
		return 4;
	default:
		return 0;
	}
}

void synth_manager_reset_state(void)
{
	s_synth_phase = 0.0;
	s_synth_phase_inc = 0.0;
	s_synth_env = 0.0;
	s_synth_fade_active = false;
	s_synth_fade_dir = 0;
	s_synth_fade_frames_remaining = 0;
	s_synth_fade_frames_total = 0;
}

size_t synth_manager_generate_audio(uint8_t* buffer,
									size_t buffer_size,
									const audio_config_t* config,
									bool *force_synth_flag,
									synth_lock_t *lock)
{
	if (buffer == NULL || buffer_size == 0 || config == NULL) {
		return 0;
	}

	const int sample_rate = (config->sample_rate > 0) ? config->sample_rate : 44100;
	const int channels = (config->channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
	int bytes_per_sample = synth_bytes_per_sample(config->bit_depth);
	if (bytes_per_sample <= 0) {
		bytes_per_sample = 2;
	}

	const size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;
	if (frame_bytes == 0) {
		return 0;
	}

	const size_t max_bytes = buffer_size - (buffer_size % frame_bytes);
	if (max_bytes == 0) {
		return 0;
	}

	int tone_hz = 20000;
	if (sample_rate > 0) {
		const int nyquist_guard = (sample_rate / 2) - 1000;
		if (tone_hz > nyquist_guard) {
			tone_hz = nyquist_guard > 1000 ? nyquist_guard : 1000;
		}
	}

	const double two_pi = 2.0 * M_PI;
	const double phase_inc = (sample_rate > 0)
								 ? ((two_pi * (double)tone_hz) / (double)sample_rate)
								 : ((two_pi * (double)tone_hz) / 44100.0);
	const double amplitude = 0.008;

	size_t frames = max_bytes / frame_bytes;
	double phase = s_synth_phase;
	s_synth_phase_inc = phase_inc;

	if (bytes_per_sample == 2) {
		int16_t *out = (int16_t *)buffer;
		const double scale = 32767.0 * amplitude;
		for (size_t f = 0; f < frames; ++f) {
			if (s_synth_fade_active && s_synth_fade_frames_remaining > 0) {
				double delta = 1.0 / (double)s_synth_fade_frames_total;
				if (s_synth_fade_dir > 0) {
					s_synth_env += delta;
					if (s_synth_env >= 1.0) {
						s_synth_env = 1.0;
						s_synth_fade_active = false;
						s_synth_fade_dir = 0;
					}
				} else if (s_synth_fade_dir < 0) {
					s_synth_env -= delta;
					if (s_synth_env <= 0.0) {
						s_synth_env = 0.0;
						s_synth_fade_active = false;
						s_synth_fade_dir = 0;
						if (force_synth_flag != NULL) {
#ifdef ESP_PLATFORM
						if (lock != NULL) {
							portENTER_CRITICAL(lock);
						}
							*force_synth_flag = false;
						if (lock != NULL) {
							portEXIT_CRITICAL(lock);
						}
#else
							(void)lock;
							*force_synth_flag = false;
#endif
						}
					}
				}
				if (s_synth_fade_frames_remaining > 0) {
				s_synth_fade_frames_remaining--;
			}
			}

			double sample = sin(phase) * scale * s_synth_env;
			int16_t s = (int16_t)sample;
			for (int ch = 0; ch < channels; ++ch) {
				*out++ = s;
			}
			phase += phase_inc;
			if (phase >= two_pi) {
				phase -= two_pi;
			}
		}
	} else {
		int32_t *out32 = (int32_t *)buffer;
		const double scale32 = (2147483647.0) * amplitude;
		for (size_t f = 0; f < frames; ++f) {
			if (s_synth_fade_active && s_synth_fade_frames_remaining > 0) {
				double delta = 1.0 / (double)s_synth_fade_frames_total;
				if (s_synth_fade_dir > 0) {
					s_synth_env += delta;
					if (s_synth_env >= 1.0) {
						s_synth_env = 1.0;
						s_synth_fade_active = false;
						s_synth_fade_dir = 0;
					}
				} else if (s_synth_fade_dir < 0) {
					s_synth_env -= delta;
					if (s_synth_env <= 0.0) {
						s_synth_env = 0.0;
						s_synth_fade_active = false;
						s_synth_fade_dir = 0;
						if (force_synth_flag != NULL) {
#ifdef ESP_PLATFORM
						if (lock != NULL) {
							portENTER_CRITICAL(lock);
						}
							*force_synth_flag = false;
						if (lock != NULL) {
							portEXIT_CRITICAL(lock);
						}
#else
							(void)lock;
							*force_synth_flag = false;
#endif
						}
					}
				}
				if (s_synth_fade_frames_remaining > 0) {
				s_synth_fade_frames_remaining--;
			}
			}
			double sample = sin(phase) * scale32 * s_synth_env;
			int32_t s = (int32_t)sample;
			for (int ch = 0; ch < channels; ++ch) {
				*out32++ = s;
			}
			phase += phase_inc;
			if (phase >= two_pi) {
				phase -= two_pi;
			}
		}
	}

	s_synth_phase = phase;
	return frames * frame_bytes;
}

#else

void synth_manager_reset_state(void) {}
size_t synth_manager_generate_audio(uint8_t* buffer,
									size_t buffer_size,
									const audio_config_t* config,
									bool *force_synth_flag,
									synth_lock_t *lock)
{
	(void)buffer;
	(void)buffer_size;
	(void)config;
	(void)force_synth_flag;
	(void)lock;
	return 0;
}

#endif
