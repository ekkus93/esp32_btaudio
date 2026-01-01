cmake_minimum_required(VERSION 3.16)

# Build a minimal unit-test app focused on the play manager module.
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(play_manager_test)
