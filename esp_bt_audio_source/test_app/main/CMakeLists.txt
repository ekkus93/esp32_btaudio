set(SOURCES
    "main.c"
    "i2s_audio_test.c"
    "audio_pipeline_test.c"
    "pcm_format_test.c"
    "i2s_channel_test.c"
    "bt_a2dp_test.c"
    "bt_source_mock.c"
    "command_interface_mock.c"  # Update the filename here
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "../../main"  # Add path to main app headers
    REQUIRES driver audio unity bt
)
