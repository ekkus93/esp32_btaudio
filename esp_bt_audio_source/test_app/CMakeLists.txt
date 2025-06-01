# ESP-IDF Component CMake file for test application

cmake_minimum_required(VERSION 3.16)

# Add this line to include components from parent directory
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_bt_audio_source_test)
