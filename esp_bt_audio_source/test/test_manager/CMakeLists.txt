cmake_minimum_required(VERSION 3.16)

# IDF 6.0 moved test_utils out of tools/unit-test-app (migration guide, tools.rst).
if(EXISTS "$ENV{IDF_PATH}/tools/test_apps/components/test_utils")
    set(IDF_TEST_UTILS_DIR "$ENV{IDF_PATH}/tools/test_apps/components")
else()
    set(IDF_TEST_UTILS_DIR "$ENV{IDF_PATH}/tools/unit-test-app/components")
endif()

set(EXTRA_COMPONENT_DIRS
    "${IDF_TEST_UTILS_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/util_safe"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_manager)
