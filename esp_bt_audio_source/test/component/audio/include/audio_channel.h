#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert stereo PCM data to mono by averaging the channels
 *
 * @param stereo_buffer Interleaved stereo PCM data (L R L R ...)
 * @param mono_buffer Output buffer for mono PCM data
 * @param frame_count Number of stereo frames (pairs of L/R samples)
 */
void convert_stereo_to_mono(int16_t *stereo_buffer, int16_t *mono_buffer, int frame_count);

/**
 * @brief Convert mono PCM data to stereo by duplicating samples
 *
 * @param mono_buffer Mono PCM data
 * @param stereo_buffer Output buffer for interleaved stereo PCM data
 * @param frame_count Number of mono samples
 */
void convert_mono_to_stereo(int16_t *mono_buffer, int16_t *stereo_buffer, int frame_count);

#ifdef __cplusplus
}
#endif
