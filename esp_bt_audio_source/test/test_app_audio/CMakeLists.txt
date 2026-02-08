# ESP-IDF BT Audio Source Audio Test App
cmake_minimum_required(VERSION 3.16)

# Share only the component directories required for the audio-focused tests so
# we avoid pulling the Bluetooth stack (and its large dependencies) into this
# build. The production audio component lives alongside the main firmware, and
# we still expose the existing mock helpers that ship with the audio tests.
set(EXTRA_COMPONENT_DIRS
    "../../components/audio_processor"
    "../../components/util_safe"
    "../../components/nvs_storage"
    "../test_app/components"
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
)

# NOTE: SPIFFS and WAV playback removed in Version 0.3.0 (Phase 5).
# The partition table no longer contains a SPIFFS partition.
# This test suite now only contains PSRAM-focused tests.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_bt_audio_source_audio_test)

# The audio tests still rely on the mock-enabled build configuration so they can
# run against the fake Bluetooth stack when necessary.
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CONFIG_BT_MOCK_TESTING)

if(DEFINED UNITY_AUDIO_TEST_GROUP_OVERRIDE AND NOT "${UNITY_AUDIO_TEST_GROUP_OVERRIDE}" STREQUAL "")
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE UNITY_AUDIO_TEST_GROUP_OVERRIDE="${UNITY_AUDIO_TEST_GROUP_OVERRIDE}")
endif()

# Note: test_app_audio uses a test-only command_interface component placed under
# test_app_audio/components/test_command_interface. We intentionally avoid
# exposing the project's bt_manager/bluetooth components to keep the test
# image small.

target_include_directories(${PROJECT_NAME}.elf PUBLIC
    "${CMAKE_SOURCE_DIR}/components/test_command_interface/include"
)
