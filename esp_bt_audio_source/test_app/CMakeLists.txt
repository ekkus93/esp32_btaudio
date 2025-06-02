# ESP-IDF Component CMake file for test application

# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add the audio component from our parent directory
set(EXTRA_COMPONENT_DIRS "../components")

# Add this near the top of the file to enable mocks during testing
add_compile_definitions(CONFIG_BT_MOCK_TESTING)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_bt_audio_source_test)
