idf_component_register(
    SRCS 
        "test_app_main.c"
        "test_bt_scan.c"
        "bt_source_mock.c"    # Add mock implementation
        "command_interface.c"  # Add command interface implementation
    INCLUDE_DIRS 
        "."
        "../../main"  # Add path to main project header files
    REQUIRES 
        unity 
        freertos
)
