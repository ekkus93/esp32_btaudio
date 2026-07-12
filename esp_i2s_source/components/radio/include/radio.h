/*
 * radio — internet-radio stream client (RADIO-1b): esp_http_client + esp-tls,
 * .m3u/.pls resolution, ICY metadata, and a PSRAM compressed-frame ring the
 * decoder (RADIO-2) drains. Reconnect with backoff; telemetry. See SPEC §5.3.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADIO_CODEC_UNKNOWN = 0,
    RADIO_CODEC_MP3,
    RADIO_CODEC_AAC,
} radio_codec_t;

#define RADIO_URL_MAX      256
#define RADIO_NAME_MAX      64
#define RADIO_TITLE_MAX    160

typedef struct {
    bool          playing;
    bool          buffering;      /* playing but PCM cushion not yet primed */
    radio_codec_t codec;
    int           http_status;
    int           bitrate_kbps;   /* icy-br, 0 if absent */
    char          url[RADIO_URL_MAX];
    char          station[RADIO_NAME_MAX];  /* icy-name */
    char          title[RADIO_TITLE_MAX];   /* current ICY StreamTitle */
    uint64_t      bytes_in;       /* audio bytes fetched (ex-metadata) */
    uint32_t      ring_used;
    uint32_t      ring_cap;
    uint32_t      reconnects;
    uint32_t      overflow_drops; /* bytes dropped on ring overflow */
    /* decoder (RADIO-2a) */
    int           dec_rate;       /* decoded sample rate (Hz), 0 if not decoding */
    int           dec_channels;
    uint32_t      pcm_used;       /* decoded PCM ring fill (bytes) */
    uint32_t      pcm_cap;
    uint32_t      decode_errors;
    int           prebuffer_ms;   /* jitter cushion before playback starts */
} radio_status_t;

/* Allocate the PSRAM compressed-frame ring and internal sync. Call once. */
esp_err_t radio_init(size_t ring_bytes);

/* Resolve `playlist_or_url` (.pls/.m3u fetched + resolved, else used directly)
 * and start streaming into the ring. Replaces any current stream. */
esp_err_t radio_play(const char *playlist_or_url);

/* Stop streaming and drain the ring. */
void radio_stop(void);

/* True while a stream is active (UI/telemetry sense). */
bool radio_is_playing(void);

/* True only when a stream is active AND its PCM cushion is primed. The I2S
 * feeder routes radio audio on this (not radio_is_playing), so startup and
 * rebuffer windows emit silence/tone instead of a starving ring. */
bool radio_audio_ready(void);

/* Snapshot the current state. */
void radio_get_status(radio_status_t *out);

/* Jitter-cushion prebuffer depth (ms), persisted in NVS. set clamps to a valid
 * range that fits the PCM ring; default ~3000 ms. Takes effect on the next
 * prebuffer gate (immediately if the ring later drains, or on the next play). */
void radio_set_prebuffer_ms(int ms);
int  radio_get_prebuffer_ms(void);

/* Pull up to `len` compressed bytes from the network ring (used internally by
 * the decoder). Non-blocking; returns bytes copied (0 if empty). */
size_t radio_read(uint8_t *dst, size_t len);

/* Consumer side (I2S feeder, RADIO-2c): pull up to `frames` decoded 44.1 kHz
 * stereo s16 frames from the decoded-PCM ring. Returns frames copied. */
size_t radio_pcm_read(int16_t *dst, size_t frames);

const char *radio_codec_str(radio_codec_t c);

#ifdef __cplusplus
}
#endif
