# radio — HTTP(S) stream client + esp_audio_codec decode (MP3/AAC/HE-AACv2)
# + resample to 44.1 kHz (RADIO-1/RADIO-2). radio_parse.c is the pure playlist
# + ICY parser (host-tested); radio.c is the lifecycle/control core
# (RADIO-1b), split across radio_ring.c (ring buffers), radio_stream.c
# (HTTP/ICY fetch), and radio_decode.c (decoder, RADIO-2) — see
# radio_internal.h for the shared state contract between them.
idf_component_register(
    SRCS "radio_parse.c" "radio.c" "radio_ring.c" "radio_stream.c" "radio_decode.c"
         "station_store.c" "stations.c" "stations_persist_core.c" "radio_resampler.c"
         "url_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls
    PRIV_REQUIRES mbedtls nvs_flash espressif__esp_audio_codec lwip esp_timer)
