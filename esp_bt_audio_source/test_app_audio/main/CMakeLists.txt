idf_component_register(
    SRCS
        "test_main.c"
        "test_app_main.c"
        "unity_config.c"
        "audio_test_main.c"
        "audio_pipeline_test.c"
        "audio_processor_test.c"
        "audio_test_helpers.c"
        "i2s_audio_test.c"
        "i2s_channel_test.c"
        "i2s_test.c"
        "pcm_format_test.c"
    "../../main/audio_processor.c"
    INCLUDE_DIRS
        "."
        "include"
    REQUIRES
        audio
        driver
        nvs_storage
        esp_ringbuf
        esp_timer
        unity
        test_utils
)

# Audio-focused tests still rely on the mock-enabled build to avoid accessing
# physical peripherals on the DUT. Reuse the CONFIG_BT_MOCK_TESTING define so
# existing code paths remain enabled.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
