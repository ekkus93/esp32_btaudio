cmake_minimum_required(VERSION 3.16)

# Build a minimal unit-test app focused solely on the audio queue module.
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/util_safe"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(audio_queue_test)
