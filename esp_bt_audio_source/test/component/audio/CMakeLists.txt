idf_component_register(
    SRCS "audio_i2s.c" "audio_pipeline.c" "i2s_audio.c" "pcm_processing.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
