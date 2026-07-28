#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hfp_i2s_output.h"

static bool s_accept;
static uint32_t s_expected_generation;
static unsigned s_calls;
static uint32_t s_last_generation;
static size_t s_last_samples;
static int16_t s_last_pcm[HFP_I2S_CVSD_MAX_INPUT_SAMPLES];

void mock_hfp_audio_i2s_reset(void)
{
    s_accept = true;
    s_expected_generation = 0U;
    s_calls = 0U;
    s_last_generation = 0U;
    s_last_samples = 0U;
    memset(s_last_pcm, 0, sizeof(s_last_pcm));
}

void mock_hfp_audio_i2s_set_accept(bool accept)
{
    s_accept = accept;
}

void mock_hfp_audio_i2s_set_expected_generation(uint32_t generation)
{
    s_expected_generation = generation;
}

unsigned mock_hfp_audio_i2s_calls(void)
{
    return s_calls;
}

uint32_t mock_hfp_audio_i2s_last_generation(void)
{
    return s_last_generation;
}

size_t mock_hfp_audio_i2s_last_samples(void)
{
    return s_last_samples;
}

const int16_t *mock_hfp_audio_i2s_last_pcm(void)
{
    return s_last_pcm;
}

bool hfp_i2s_output_push_cvsd(const int16_t *samples_8k,
                              size_t sample_count,
                              uint32_t generation)
{
    s_calls++;
    s_last_generation = generation;
    s_last_samples = sample_count;
    if (samples_8k != NULL && sample_count <= HFP_I2S_CVSD_MAX_INPUT_SAMPLES) {
        memcpy(s_last_pcm, samples_8k, sample_count * sizeof(samples_8k[0]));
    }
    if (s_expected_generation != 0U && generation != s_expected_generation) {
        return false;
    }
    return s_accept;
}
