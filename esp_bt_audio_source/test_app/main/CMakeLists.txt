# Main component for the test application

# Register the component and specify dependencies
idf_component_register(
    SRCS 
        "main.c"
        "test_main.c"
        "test_app_main.c"
        "audio_processor_test.c"
        "../components/test_audio_processor.c"
        "bt_source_test.c"
        "bt_a2dp_test.c"
        "bt_pairing_test.c"
        "bt_source_stubs.c"
        "bt_source_mock.c"
    "bt_test_setup.c"
        "bt_pairing_persistence_test.c"
        "audio_pipeline_test.c"
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
        audio
        unity
)

# Component-specific compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
