/*
 * radio — internet-radio stream client: esp_http_client + esp-tls, .m3u/.pls
 * resolution, ICY metadata, PSRAM compressed-frame ring -> esp_audio_codec
 * decode (MP3, AAC-LC, HE-AAC, HE-AACv2; plain IDF, no ESP-ADF) -> resample
 * to 44.1 kHz stereo s16 (SPEC §4, §5.3-§5.4). Reconnect w/ backoff.
 *
 * Implemented in RADIO-1 / RADIO-2. Skeleton only for now.
 */
#pragma once
