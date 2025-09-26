cmake_minimum_required(VERSION 3.5)

project(esp_bt_audio_source_tests C)

# Define UNIT_TEST for the test build
add_compile_definitions(UNIT_TEST)

# Undefine ESP_PLATFORM for host tests to ensure host code path is taken
add_compile_definitions(ESP_PLATFORM_UNDEFINED)
add_compile_options(-UESP_PLATFORM)

# Add include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/command_interface/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/nvs_storage
)

# Find the unity package for unit testing
find_package(unity QUIET)
if(NOT unity_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG v2.5.2
    )
    FetchContent_MakeAvailable(unity)
endif()

# Add test executables
add_executable(test_commands test_commands.c 
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_sources(test_commands PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_commands unity)

add_executable(test_bluetooth test_bluetooth.c 
    ../../components/bt_manager/bt_manager.c
    mocks/mock_i2s.c)
# Link with math library for sin function
target_link_libraries(test_bluetooth unity m)

# Create a consolidated test runner
add_custom_target(run_tests
    COMMAND test_commands
    COMMAND test_bluetooth
    DEPENDS test_commands test_bluetooth
)
