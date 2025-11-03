# Main component for the test application (test_app)

# Register the component and specify dependencies
idf_component_register(
    SRCS
        "test_main.c"
        "test_app_main.c"
        "bt_pairing_test.c"
        "bt_a2dp_test.c"
        "bt_mock_setup.c"
    "test_helpers.c"
        "bt_pairing_persistence_test.c"
    "audio_processor_stub.c"
    "audio_processor_beep_stub.c"
    "bt_source_mock.c"
    "bt_source_stubs.c"
    # Provide mock implementations of the bt_source API expected by tests
        "bt_source_test.c"
        "bt_test_setup.c"
        "command_interface_test.c"
        "test_utils_adapters.c"
        "esp32_bt_shim.c"
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
        test_common
        command_interface
        nvs_storage
        unity
        test_utils
    PRIV_REQUIRES
        driver
)

# Component-specific compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
