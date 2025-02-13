#include "mp3_utils.h"
#include "bluetooth.h"  // Add this include for bluetooth_write_audio()
#include "minimp3.h"
#include "esp_log.h"
#include "esp_err.h"

#define TAG "MP3"

static mp3dec_t mp3d;
static mp3dec_frame_info_t info;
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

esp_err_t process_mp3_data(const uint8_t* data, size_t len) {
    // Initialize decoder if not done already
    static bool initialized = false;
    if (!initialized) {
        mp3dec_init(&mp3d);
        initialized = true;
    }

    // Process MP3 data in chunks
    size_t offset = 0;
    while (offset < len) {
        // Decode one frame
        int samples = mp3dec_decode_frame(&mp3d, data + offset, len - offset, pcm, &info);
        if (samples == 0) {
            if (info.frame_bytes == 0) {
                break;
            }
            offset += info.frame_bytes;
            continue;
        }

        // Send decoded audio to Bluetooth
        if (samples > 0) {
            ESP_LOGD(TAG, "Decoded %d samples, %d Hz, %d channels", 
                     samples, info.hz, info.channels);
            
            uint8_t* audio_data = (uint8_t*)pcm;
            size_t audio_size = samples * info.channels * sizeof(int16_t);
            if (bluetooth_write_audio(audio_data, &audio_size) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write audio data to Bluetooth");
                return ESP_FAIL;
            }
        }

        offset += info.frame_bytes;
    }

    return ESP_OK;
}
