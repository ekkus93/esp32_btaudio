#include <string.h>
#include "esp_log.h"
#include "audio_channel.h"

static const char *TAG = "AUDIO_CHANNEL";
/* TAG may be unused in certain build configurations; reference it to avoid warnings */
static void __attribute__((unused)) _audio_channel_suppress_unused_tag(void) { (void)TAG; }

// Convert stereo to mono by averaging channels
void convert_stereo_to_mono(int16_t *stereo_buffer, int16_t *mono_buffer, int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        // Get left and right channel samples
        int32_t left = stereo_buffer[i * 2];
        int32_t right = stereo_buffer[i * 2 + 1];
        
        // Average the channels and store in mono buffer
        // Using 32-bit integers to avoid overflow before division
        mono_buffer[i] = (int16_t)((left + right) / 2);
    }
}

// Convert mono to stereo by duplicating channels
void convert_mono_to_stereo(int16_t *mono_buffer, int16_t *stereo_buffer, int frame_count) {
    for (int i = 0; i < frame_count; i++) {
        // Duplicate mono sample to both stereo channels
        stereo_buffer[i * 2] = mono_buffer[i];
        stereo_buffer[i * 2 + 1] = mono_buffer[i];
    }
}
