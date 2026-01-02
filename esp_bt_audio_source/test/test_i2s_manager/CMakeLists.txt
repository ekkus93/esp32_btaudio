cmake_minimum_required(VERSION 3.16)

# Unity firmware app to exercise i2s_manager behavior on-device.
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/util_safe"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(i2s_manager_test)
