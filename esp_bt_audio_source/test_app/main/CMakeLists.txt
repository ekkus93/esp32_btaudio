set(SOURCES
    "main.c"
    "i2s_audio_test.c"
    "audio_pipeline_test.c"
    "pcm_format_test.c"
    "i2s_channel_test.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES driver audio unity
)
