# ESP-IDF BT Audio Source Test App
cmake_minimum_required(VERSION 3.16)

# Set extra components directory
set(EXTRA_COMPONENT_DIRS "../components" "${CMAKE_SOURCE_DIR}/components")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name
project(esp_bt_audio_source_test)

# Optionally add compile definitions for testing mode
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CONFIG_BT_MOCK_TESTING)

# Add bt_mock component to include directories
idf_component_get_property(bt_mock_dir bt_mock COMPONENT_DIR)
target_include_directories(${PROJECT_NAME}.elf PUBLIC 
    "${bt_mock_dir}/include"
)

# Add host test mocks/include directories so test-only headers (mock_i2s_std.h)
# are available when building the test_app. Also prefer the project's main/include
# to avoid duplicate local test headers shadowing the real public headers.
target_include_directories(${PROJECT_NAME}.elf PUBLIC
    "${CMAKE_SOURCE_DIR}/../main/include"
    "${CMAKE_SOURCE_DIR}/../test/mocks"
    "${CMAKE_SOURCE_DIR}/components/test_mocks/include"
)

# Ensure linker pulls in the command_interface symbols when resolving
# references from bt_mock by explicitly linking the component libraries
# in the required order.
idf_component_get_property(bt_mock_lib bt_mock COMPONENT_LIB)
idf_component_get_property(command_interface_lib command_interface COMPONENT_LIB)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE ${bt_mock_lib} ${command_interface_lib})
