# Main component for the test application

# Register the component and specify dependencies
idf_component_register(
    SRCS
        "main.c"
        "test_main.c"
        "test_app_main.c"
        "bt_source_test.c"
        "bt_a2dp_test.c"
        "bt_pairing_test.c"
        "test_pairing_commands.c"
        "test_pairing_adapters.c"
        "bt_source_stubs.c"
        "bt_source_mock.c"
        "audio_processor_stub.c"
        "bt_test_setup.c"
        "unity_config.c"
        "bt_pairing_persistence_test.c"
        "bt_mock_setup.c"  # Update this filename
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
