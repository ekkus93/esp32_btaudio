# Main component for the consolidated test application (test_bluetooth)
# Merges test_app, test_app2, and test_app3

# Notes: Using test_app approach (real components, not mocks)  
# test_app2-specific mocks (bt_connection_shim, bt_streaming_mock, command_interface_mock) are excluded

# Register the component and specify dependencies
idf_component_register(
    SRCS
        # Shared test infrastructure
        "test_main.c"
        "test_app_main.c"
        "main.c"
        "unity_config.c"
        "test_config.c"
        "test_helpers.c"
        
        # Test setup and mocks (shared)
        "bt_mock_setup.c"
        "bt_test_setup.c"
        "bt_source_mock.c"
        "bt_source_stubs.c"
        "bt_manager_api_mock.c"
        "esp32_bt_shim.c"
        "auto_run_tests.c"
        
        # Test files (shared)
        "bt_a2dp_test.c"
        "bt_pairing_test.c"
        "command_parser_test.c"
        "test_pairing_commands.c"
        
        # Tests from test_app (unique)
        "bt_pairing_persistence_test.c"
        "bt_source_test.c"
        "command_interface_test.c"
        "test_bt_scan.c"
        "test_utils_adapters.c"
        
        # Tests from test_app2 (unique)
        "bt_pairing_esp_test.c"
        "test_bt_connection.c"
        "test_pairing_adapters.c"
        
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
