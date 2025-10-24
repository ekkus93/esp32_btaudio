# Main component for the test application (test_app)# Main component for the test application



# Register the component and specify dependencies# Register the component and specify dependencies

idf_component_register(idf_component_register(

    SRCS    SRCS

        "main.c"        "main.c"

        "test_main.c"        "test_main.c"

        "test_app_main.c"        "test_app_main.c"

        "audio_pipeline_test.c"        "bt_source_test.c"

        "audio_processor_test.c"        "bt_a2dp_test.c"

        "audio_test_main.c"        "bt_pairing_test.c"

        "bt_mock_setup.c"        "test_pairing_commands.c"

        "bt_pairing_persistence_test.c"        "test_pairing_adapters.c"

        "bt_source_mock.c"        "bt_source_stubs.c"

        "bt_source_test.c"        "bt_source_mock.c"

        "bt_test_setup.c"        "audio_processor_stub.c"

        "command_interface_test.c"        "bt_test_setup.c"

        "esp32_bt_shim.c"        "unity_config.c"

        "i2s_channel_test.c"        "bt_pairing_persistence_test.c"

        "test_bt_scan.c"        "bt_mock_setup.c"  # Update this filename

        "test_pairing_commands.c"    INCLUDE_DIRS 

        "unity_config.c"        "."

    INCLUDE_DIRS         "include"

        "."    REQUIRES 

        "include"        bt_mock

    REQUIRES         bluetooth

        bt_mock        bt_manager

        bluetooth        command_interface

        bt_manager        nvs_storage

        command_interface        unity

        nvs_storage        test_utils

        unity    PRIV_REQUIRES

        test_utils        driver

    PRIV_REQUIRES)

        driver

)# Component-specific compile definitions

target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)

# Component-specific compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
