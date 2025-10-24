# Main component for the test application (test_app)

# Register the component and specify dependencies
idf_component_register(
    SRCS
        "main.c"
        "test_main.c"
        "test_app_main.c"
        "audio_pipeline_test.c"
        "audio_processor_test.c"
        "audio_test_main.c"
        "bt_mock_setup.c"
        "bt_pairing_persistence_test.c"
        "bt_source_mock.c"
        "bt_source_test.c"
        "bt_test_setup.c"
        "command_interface_test.c"
        "esp32_bt_shim.c"
        "i2s_channel_test.c"
        "test_bt_scan.c"
        "test_pairing_commands.c"
        "unity_config.c"
    INCLUDE_DIRS 
        "."
        "include"
    REQUIRES 
        bt_mock
        bluetooth
        bt_manager
        command_interface
        nvs_storage
        unity
        test_utils
    PRIV_REQUIRES
        driver
)

# Component-specific compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
