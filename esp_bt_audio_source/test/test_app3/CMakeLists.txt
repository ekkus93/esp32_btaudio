cmake_minimum_required(VERSION 3.16)

# Minimal test app that builds with the project's components and
# includes the audio component tests. This mirrors existing test_app
# layout and enables `CONFIG_BT_MOCK_TESTING` for test-only wrappers.

# Use the production audio stack and utility components from the parent project.
# Keep the dependency set minimal to avoid dragging the Bluetooth stack into this test image.
set(EXTRA_COMPONENT_DIRS
	"${CMAKE_SOURCE_DIR}/../../components/audio_processor"
	"${CMAKE_SOURCE_DIR}/../../components/util_safe"
	"${CMAKE_SOURCE_DIR}/../../components/nvs_storage"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_app3)

# The test flags are provided via test_app3/sdkconfig.defaults.
# Add test main sources (this project will pick up component tests too)
# The `test/component/audio/test` file is compiled by target's default component scanning
