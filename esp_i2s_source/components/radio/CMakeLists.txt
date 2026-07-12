# radio — HTTP(S) stream client + esp_audio_codec decode (MP3/AAC/HE-AACv2)
# + resample to 44.1 kHz (RADIO-1/RADIO-2). radio_parse.c is the pure playlist
# + ICY parser (host-tested); radio.c is the stream task (RADIO-1b); the
# decoder lands in RADIO-2.
idf_component_register(
    SRCS "radio_parse.c" "radio.c" "station_store.c" "stations.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls
    PRIV_REQUIRES mbedtls nvs_flash)
