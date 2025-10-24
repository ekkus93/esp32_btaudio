# ESP-IDF BT Audio Source Audio Test App
cmake_minimum_required(VERSION 3.16)

# Share only the component directories required for the audio-focused tests so
# we avoid pulling the Bluetooth stack (and its large dependencies) into this
# build. The production audio component lives alongside the main firmware, and
# we still expose the existing mock helpers that ship with the audio tests.
set(EXTRA_COMPONENT_DIRS
    "../components/audio"
    "../components/nvs_storage"
    "../test_app/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_bt_audio_source_audio_test)

# The audio tests still rely on the mock-enabled build configuration so they can
# run against the fake Bluetooth stack when necessary.
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CONFIG_BT_MOCK_TESTING)
