/*
 * radio_parse — pure playlist + ICY metadata parsers (RADIO-1a). No ESP-IDF
 * deps; host-tested. RADIO-1b's stream task uses these to resolve a station's
 * .pls/.m3u to a stream URL and to pull the "now playing" title out of the
 * SHOUTcast ICY metadata blocks (SPEC §5.3).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve playlist `text` (.pls, .m3u/extended-m3u, or a bare URL) to the first
 * http(s) stream URL. Writes a NUL-terminated URL into `out`. Returns false if
 * no http(s) URL is found or it doesn't fit in `out_sz`. */
bool radio_playlist_first_url(const char *text, char *out, size_t out_sz);

/* Extract StreamTitle from a SHOUTcast ICY metadata block, e.g.
 *   StreamTitle='Artist - Song';StreamUrl='http://...';
 * Writes the (possibly empty) title into `title`. Trailing NUL padding on the
 * block is tolerated. Returns true iff a StreamTitle field was present. */
bool radio_icy_stream_title(const char *block, char *title, size_t title_sz);

/* --- Incremental ICY demuxer ---------------------------------------------
 * SHOUTcast interleaves metadata into the audio stream: every `metaint` bytes
 * of audio, a single length byte L (block size = L*16) precedes an L*16-byte
 * metadata block. This state machine splits a byte stream fed in arbitrary
 * chunks back into audio (audio_fn) and title updates (title_fn, only when a
 * StreamTitle is present). metaint == 0 means no metadata (all input audio). */
typedef void (*radio_icy_audio_fn)(void *ctx, const unsigned char *data, size_t n);
typedef void (*radio_icy_title_fn)(void *ctx, const char *title);

#define RADIO_ICY_META_MAX 512   /* title lives at block start; rest discarded */

typedef struct {
    int  metaint;
    int  audio_left;   /* audio bytes until the next length byte */
    int  meta_left;    /* metadata bytes still to collect; -1 = expect len byte */
    int  meta_pos;     /* bytes stored in meta_buf so far */
    char meta_buf[RADIO_ICY_META_MAX];
} radio_icy_demux_t;

void radio_icy_demux_init(radio_icy_demux_t *d, int metaint);
void radio_icy_demux_feed(radio_icy_demux_t *d, const unsigned char *in, size_t n,
                          radio_icy_audio_fn audio_fn, radio_icy_title_fn title_fn,
                          void *ctx);

#ifdef __cplusplus
}
#endif
