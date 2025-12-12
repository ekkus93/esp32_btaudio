cmake_minimum_required(VERSION 3.16)

# Minimal test app that builds with the project's components and
# includes the audio component tests. This mirrors existing test_app
# layout and enables `CONFIG_BT_MOCK_TESTING` for test-only wrappers.

set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components" "${CMAKE_SOURCE_DIR}/../main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_app3)

# The test flags are provided via test_app3/sdkconfig.defaults.
# Add test main sources (this project will pick up component tests too)
# The `components/audio/test` file is compiled by target's default component scanning
