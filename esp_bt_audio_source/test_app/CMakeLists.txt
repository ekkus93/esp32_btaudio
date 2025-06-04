# ESP-IDF Component CMake file for test application

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set mock testing flag globally for all components
add_compile_definitions(CONFIG_BT_MOCK_TESTING)
add_compile_definitions(BT_USE_MOCKS)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_bt_audio_source_test)
