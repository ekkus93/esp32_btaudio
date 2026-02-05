/**
 * Stub for s_audio_config needed by synth_source_fill() in test context
 * 
 * WHY: synth_source_fill() accesses extern s_audio_config
 * HOW: Provide a dummy config that tests can ignore (they call synth_manager_generate_audio directly)
 * CORRECTNESS: Only used by synth_source_fill(), not by test cases
 */

#include "audio_processor.h"

/* Stub audio config for synth_source_fill() */
audio_config_t s_audio_config = {
    .sample_rate = AUDIO_SAMPLE_RATE_48K,
    .bit_depth = AUDIO_BIT_DEPTH_16,
    .channels = AUDIO_CHANNEL_STEREO,
    .volume = 80,
    .mute = false,
    .i2s_port = 0,
    .i2s_bclk_pin = -1,
    .i2s_ws_pin = -1,
    .i2s_din_pin = -1,
    .i2s_dout_pin = -1
};
