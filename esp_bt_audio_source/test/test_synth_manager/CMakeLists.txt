cmake_minimum_required(VERSION 3.16)

# Unity firmware app to exercise synth_manager behavior.
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/util_safe"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(synth_manager_test)
