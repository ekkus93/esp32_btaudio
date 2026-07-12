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

#ifdef __cplusplus
}
#endif
