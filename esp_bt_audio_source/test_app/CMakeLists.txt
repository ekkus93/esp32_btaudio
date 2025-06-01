# ESP-IDF Component CMake file for test application

cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_bt_audio_source_test)

# Add components directory to include path
set(EXTRA_COMPONENT_DIRS "../components")
