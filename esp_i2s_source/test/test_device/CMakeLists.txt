# ESP-IDF I2S Source Device Test App
cmake_minimum_required(VERSION 3.16)

# IDF 6.0 moved test_utils out of tools/unit-test-app (migration guide, tools.rst).
if(EXISTS "$ENV{IDF_PATH}/tools/test_apps/components/test_utils")
    set(IDF_TEST_UTILS_DIR "$ENV{IDF_PATH}/tools/test_apps/components")
else()
    set(IDF_TEST_UTILS_DIR "$ENV{IDF_PATH}/tools/unit-test-app/components")
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/signal_gen"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/tone"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/i2s_out"
    "${IDF_TEST_UTILS_DIR}"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_i2s_source_device_test)
