# Main component for the test application

# Register the component and specify dependencies
idf_component_register(
    SRCS
        "main.c"
        "test_main.c"
        "test_app_main.c"
        "bt_a2dp_test.c"
        "bt_pairing_test.c"
        "bt_pairing_esp_test.c"
        "test_pairing_adapters.c"
    # Bring over common test setup and pairing helpers used by several tests
    "bt_mock_setup.c"
    "bt_test_setup.c"
    "test_helpers.c"
    "test_pairing_commands.c"
    "auto_run_tests.c"
    "test_bt_connection.c"
    "command_parser_test.c"
    "bt_source_stubs.c"
    "bt_source_mock.c"
    "bt_connection_shim.c"
        "bt_streaming_mock.c"
        "audio_processor_stub.c"
    "audio_processor_beep_stub.c"
    # audio_test_helpers.c moved to test_app_audio/main - excluded
    # i2s_audio_test.c moved to test_app_audio/main - excluded
    # i2s_test.c moved to test_app_audio/main - excluded
    # pcm_format_test.c moved to test_app_audio/main - excluded
        "esp32_bt_shim.c"
        "unity_config.c"
    INCLUDE_DIRS 
        "."
        "include"
    REQUIRES 
        bt_mock
        bt_manager
        test_common
        command_interface
        nvs_storage
        util_safe
        unity
        test_utils
    PRIV_REQUIRES
        driver
        nvs_flash
)

# Component-specific compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
