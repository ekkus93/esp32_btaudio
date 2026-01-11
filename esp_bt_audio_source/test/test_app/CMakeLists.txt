# ESP-IDF BT Audio Source Test App
cmake_minimum_required(VERSION 3.16)

# Set extra components directory
set(EXTRA_COMPONENT_DIRS
    "../../components"
    "../../test/component"
    "${CMAKE_SOURCE_DIR}/components"
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set project name
project(esp_bt_audio_source_test)

# Optionally add compile definitions for testing mode
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CONFIG_BT_MOCK_TESTING)
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ESP_PLATFORM)

# Add bt_mock component to include directories
idf_component_get_property(bt_mock_dir bt_mock COMPONENT_DIR)
target_include_directories(${PROJECT_NAME}.elf PUBLIC 
    "${bt_mock_dir}/include"
)

# Surface only the production headers and the local test mocks required by the
# on-device Unity firmware. Host-only mock directories are intentionally
# excluded here to prevent them from shadowing the real ESP-IDF headers.
target_include_directories(${PROJECT_NAME}.elf PUBLIC
    "${CMAKE_SOURCE_DIR}/../../main/include"
    "${CMAKE_SOURCE_DIR}/components/test_mocks/include"
)

# Ensure components expose their unit-test hooks when building the Unity firmware.
idf_component_get_property(bt_manager_lib bt_manager COMPONENT_LIB)
target_compile_definitions(${bt_manager_lib} PRIVATE UNIT_TEST)

# Ensure linker pulls in the command_interface symbols when resolving
# references from bt_mock by explicitly linking the component libraries
# in the required order.
idf_component_get_property(bt_mock_lib bt_mock COMPONENT_LIB)
idf_component_get_property(command_interface_lib command_interface COMPONENT_LIB)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE ${bt_mock_lib} ${command_interface_lib})

# Ensure the test-only stubs are linked so weak symbols are available for
# on-device test images that don't include the full main application.
idf_component_get_property(bt_test_stubs_lib bt_test_stubs COMPONENT_LIB)
target_link_libraries(${PROJECT_NAME}.elf PRIVATE ${bt_test_stubs_lib})

# Add device-side Unity test that exercises the real command parser.
# The component's own CMakeLists already registers the `main/` sources. Do
# not add them again here or the linker will see duplicate symbols.

# For on-device Unity images we need some production implementation
# symbols (audio processor) available so tests link. Add only the
# specific source file rather than the entire `main/` tree to avoid
# duplicate `app_main` and other entry symbols.
target_sources(${PROJECT_NAME}.elf PRIVATE
    "${CMAKE_SOURCE_DIR}/../../main/audio_processor.c"
    # Minimal dependencies required by audio_processor.c
    "${CMAKE_SOURCE_DIR}/../../main/audio_queue.c"
    "${CMAKE_SOURCE_DIR}/../../main/audio_util.c"
    "${CMAKE_SOURCE_DIR}/../../main/mem_util.c"
    "${CMAKE_SOURCE_DIR}/../../main/play_manager.c"
    "${CMAKE_SOURCE_DIR}/../../main/i2s_manager.c"
    "${CMAKE_SOURCE_DIR}/../../main/beep_manager.c"
    "${CMAKE_SOURCE_DIR}/../../main/synth_manager.c"
)

