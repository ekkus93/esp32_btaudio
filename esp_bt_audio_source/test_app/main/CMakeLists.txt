set(SOURCES
    "main.c"
    "i2s_audio_test.c"
    "audio_pipeline_test.c"
    "pcm_format_test.c"
    "i2s_channel_test.c"
    "bt_a2dp_test.c"
    "bt_pairing_test.c"
    "bt_mock_devices.c"
    "audio_test_helpers.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "../../main"
    # Add ESP32-specific libraries that we're using 
    REQUIRES driver esp_common nvs_flash unity bt esp_hw_support
    # Add private requirements for the audio component
    PRIV_REQUIRES audio
)
