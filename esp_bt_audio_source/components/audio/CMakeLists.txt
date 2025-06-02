idf_component_register(
    SRCS "i2s_audio.c" "audio_pipeline.c" "pcm_processing.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
