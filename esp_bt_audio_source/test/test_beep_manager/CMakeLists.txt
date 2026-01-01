cmake_minimum_required(VERSION 3.16)

# Minimal Unity firmware app focused on beep_manager behavior.
set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(beep_manager_test)
