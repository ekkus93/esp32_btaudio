set(SOURCES
    "main.c"
    "bt_a2dp_test.c"
    "bt_pairing_test.c"
    "bt_mock_devices.c"
    "test_config.c"
    "bt_source_stubs.c"  # Add this new file
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "../../main"
    # Add ESP32-specific libraries that we're using 
    REQUIRES driver esp_common nvs_flash unity bt esp_hw_support
)

# Add the compile definition properly after component registration
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING BT_USE_MOCKS)
