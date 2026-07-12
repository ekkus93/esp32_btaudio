# radio — HTTP(S) stream client + esp_audio_codec decode (MP3/AAC/HE-AACv2)
# + resample to 44.1 kHz (RADIO-1/RADIO-2). radio_parse.c is the pure playlist
# + ICY parser (host-tested); the stream task/decoder land in RADIO-1b/RADIO-2.
idf_component_register(
    SRCS "radio_parse.c"
    INCLUDE_DIRS "include")
