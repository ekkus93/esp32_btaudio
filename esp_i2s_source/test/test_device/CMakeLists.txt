# ESP-IDF I2S Source Device Test App
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/signal_gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/tone"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/i2s_out"
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_i2s_source_device_test)
