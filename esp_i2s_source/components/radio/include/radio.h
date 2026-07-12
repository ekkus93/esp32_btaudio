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
} radio_status_t;

/* Allocate the PSRAM compressed-frame ring and internal sync. Call once. */
esp_err_t radio_init(size_t ring_bytes);

/* Resolve `playlist_or_url` (.pls/.m3u fetched + resolved, else used directly)
 * and start streaming into the ring. Replaces any current stream. */
esp_err_t radio_play(const char *playlist_or_url);

/* Stop streaming and drain the ring. */
void radio_stop(void);

/* Snapshot the current state. */
void radio_get_status(radio_status_t *out);

/* Consumer side (RADIO-2 decoder): pull up to `len` compressed bytes from the
 * ring. Non-blocking; returns the number of bytes copied (0 if empty). */
size_t radio_read(uint8_t *dst, size_t len);

const char *radio_codec_str(radio_codec_t c);

#ifdef __cplusplus
}
#endif
