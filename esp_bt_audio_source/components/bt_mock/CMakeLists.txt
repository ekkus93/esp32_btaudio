idf_component_register(
    SRCS 
        "bt_mock_devices.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        bluetooth
)

# Create a public header directory 
target_include_directories(${COMPONENT_LIB} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)
