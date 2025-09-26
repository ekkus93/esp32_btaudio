# ESP-IDF BT Audio Source Test App
cmake_minimum_required(VERSION 3.16)

# Set extra components directory
set(EXTRA_COMPONENT_DIRS "../components")

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
