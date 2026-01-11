idf_component_register(
    SRCS 
        "bt_mock_devices.c"
        "bt_mock.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES
        "esp_common"
        "bt_manager"
        "log"
        "util_safe"
    PRIV_REQUIRES
        "command_interface"
        "util_safe"
)

# Add compile definition to enable test features
target_compile_definitions(${COMPONENT_LIB} PUBLIC BT_USE_MOCKS CONFIG_BT_MOCK_TESTING)
