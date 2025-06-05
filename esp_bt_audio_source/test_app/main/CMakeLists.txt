set(SOURCES
    "test_app_main.c"
    "bt_source_stubs.c"
    "audio_pipeline_test.c"  # Add the new file here
    "bt_pairing_test.c"
    "bt_a2dp_test.c"
    "bt_pairing_esp_test.c"
    "esp32_bt_shim.c"
    "test_main.c"
    "i2s_audio_test.c"
    "i2s_channel_test.c"
    "audio_test_helpers.c"
    "command_interface_mock.c"
    "bt_streaming_mock.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        bt_mock  # Use bt_mock component instead of local files
        bluetooth
        audio
        unity
        command_interface
)

# Add the compile definition properly after component registration
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING BT_USE_MOCKS)

# Unity configuration for this component - only use fixture, not memory
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UNITY_INCLUDE_CONFIG_H
    UNITY_EXTENSION_FIXTURE=1
    UNITY_FIXTURE_NO_EXTRAS=1  # Add this to prevent needing memory.h
)
