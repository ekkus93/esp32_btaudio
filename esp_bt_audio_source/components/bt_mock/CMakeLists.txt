idf_component_register(
    SRCS 
        "bt_mock_devices.c"
        "bt_mock.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES
        "esp_common"
        "bluetooth"
        "log"
    PRIV_REQUIRES
        "command_interface"
)

# Add compile definition to enable test features
target_compile_definitions(${COMPONENT_LIB} PUBLIC BT_USE_MOCKS CONFIG_BT_MOCK_TESTING)
