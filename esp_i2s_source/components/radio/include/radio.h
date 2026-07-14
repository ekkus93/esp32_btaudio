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

/* Radio lifecycle state (RH-S3-02). */
typedef enum {
    RADIO_STATE_STOPPED = 0,
    RADIO_STATE_STARTING,
    RADIO_STATE_RUNNING,
    RADIO_STATE_STOPPING,
    RADIO_STATE_FAULTED,
} radio_state_t;

#define RADIO_URL_MAX      256
#define RADIO_NAME_MAX      64
#define RADIO_TITLE_MAX    160

/* Last-error reason codes for the status snapshot. */
typedef enum {
    RADIO_ERR_NONE = 0,
    RADIO_ERR_HTTP_CLIENT_ALLOC,
    RADIO_ERR_UNSUPPORTED_CONTENT,
    RADIO_ERR_DECODER_OPEN_FAILED,
    RADIO_ERR_NETWORK_OPEN_FAILED,
    RADIO_ERR_DECODER_STALLED,
    RADIO_ERR_STOP_TIMEOUT,
    RADIO_ERR_RESAMPLER_STALLED,
} radio_err_t;

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
    /* session state (RH-S3-02) */
    uint32_t      generation;     /* monotonically increasing generation ID */
    radio_state_t state;
    radio_err_t   last_error;
    char          last_error_detail[64];  /* content-type or error context */
} radio_status_t;

/* Command queue for async radio operations (RH-S3-09). */

/* Resolve `playlist_or_url` and start streaming. Queued for async execution
 * by the command worker. Returns ESP_OK if queued, ESP_ERR_TIMEOUT if queue
 * full. Check radio_get_status() for the actual play result. */
esp_err_t radio_play_async(const char *playlist_or_url);

/* Stop streaming and drain the ring. Queued for async execution by the
 * command worker. Returns ESP_OK if queued, ESP_ERR_TIMEOUT if queue full. */
esp_err_t radio_stop_async(void);

/* Internal — synchronous play/stop (called by command worker or tests).
 * Blocks until both stream and decoder tasks are created (play), or both
 * workers exited (stop). */
esp_err_t radio_play_sync(const char *playlist_or_url);
esp_err_t radio_stop_sync(void);

/* True while a stream is active (UI/telemetry sense). */
bool radio_is_playing(void);

/* True only when a stream is active AND its PCM cushion is primed. The I2S
 * feeder routes radio audio on this (not radio_is_playing()), so startup and
 * rebuffer windows emit silence/tone instead of a starving ring. */
bool radio_audio_ready(void);

/* Snapshot the current state. */
void radio_get_status(radio_status_t *out);

/* Return the current lifecycle state. */
radio_state_t radio_get_state(void);

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

/* Release the compressed-frame ring and internal sync. Call before process exit. */
void radio_deinit(void);

const char *radio_codec_str(radio_codec_t c);

#ifdef __cplusplus
}
#endif