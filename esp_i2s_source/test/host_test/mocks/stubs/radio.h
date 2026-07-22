/* Stub radio.h for host tests */
#ifndef STUB_RADIO_H
#define STUB_RADIO_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define RADIO_URL_MAX 256

typedef enum {
    RADIO_STATE_STOPPED = 0,
    RADIO_STATE_STARTING,
    RADIO_STATE_BUFFERING,
    RADIO_STATE_RUNNING,
    RADIO_STATE_STOPPING,
    RADIO_STATE_FAULTED,
    RADIO_STATE_FAULTED_JOIN_PENDING,
} radio_state_t;

typedef enum {
    RADIO_CODEC_NONE = 0,
    RADIO_CODEC_MP3,
    RADIO_CODEC_AAC,
} radio_codec_t;

typedef struct {
    bool            playing;
    bool            buffering;
    radio_codec_t   codec;
    char            station[64];
    char            title[128];
    char            url[RADIO_URL_MAX];
    int             bitrate_kbps;
    uint64_t        bytes_in;
    uint32_t        ring_used;
    uint32_t        ring_cap;
    int             reconnects;
    int             dec_rate;
    int             dec_channels;
    uint32_t        pcm_used;
    uint32_t        pcm_cap;
    int             decode_errors;
    int             prebuffer_ms;
} radio_status_t;

esp_err_t radio_init(size_t compressed_ring_bytes);
void radio_get_status(radio_status_t *out);
esp_err_t radio_play_async(const char *url);
esp_err_t radio_stop_async(void);
radio_state_t radio_get_state(void);
bool radio_audio_ready(void);
size_t radio_pcm_read(int16_t *buf, size_t frames);
const char *radio_codec_str(radio_codec_t codec);

#endif
