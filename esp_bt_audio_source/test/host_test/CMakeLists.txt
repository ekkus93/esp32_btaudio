cmake_minimum_required(VERSION 3.10)

project(esp_bt_audio_source_tests C)

# Define UNIT_TEST for the test build
add_compile_definitions(UNIT_TEST)

# Undefine ESP_PLATFORM for host tests to ensure host code path is taken
add_compile_definitions(ESP_PLATFORM_UNDEFINED)
add_compile_options(-UESP_PLATFORM)

# Add include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/command_interface/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/nvs_storage
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bluetooth/include
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
    ../../components/bt_manager/bt_manager.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_sources(test_commands PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_commands PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_commands unity)

# Enable CTest and register the test executables so `ctest` can discover them
enable_testing()
add_test(NAME test_commands COMMAND $<TARGET_FILE:test_commands>)

add_executable(test_bluetooth test_bluetooth.c 
    ../../components/bt_manager/bt_manager.c
    mocks/mock_i2s.c)
# Ensure test hooks are linked for bt_manager tests
target_sources(test_bluetooth PRIVATE mocks/bt_manager_test_hooks.c)
# Link with math library for sin function
target_link_libraries(test_bluetooth unity m)
add_test(NAME test_bluetooth COMMAND $<TARGET_FILE:test_bluetooth>)

# Audio processor unit tests (host)
add_executable(test_audio_processor ../../test/component/test_audio_processor.c
    ../../components/audio_processor/audio_processor.c
    mocks/mock_i2s.c
    mocks/test_main_wrapper.c)
target_include_directories(test_audio_processor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../test/component)
# Ensure this target uses the full test shim (which defines audio_config_t)
# rather than the lightweight mock header. Put the shim first on the include
# path for this target only.
## Ensure `test_audio_processor` uses the real component header from main/include
## (so it will compile against the real `audio_processor.c`). Put the
## production include path before mocks/includes for this target only.
target_include_directories(test_audio_processor BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include)
target_link_libraries(test_audio_processor unity m)
add_test(NAME test_audio_processor COMMAND $<TARGET_FILE:test_audio_processor>)

add_executable(test_nvs_storage test_nvs_storage.c
    mocks/nvs_storage_mock.c)
target_link_libraries(test_nvs_storage unity)
add_test(NAME test_nvs_storage COMMAND $<TARGET_FILE:test_nvs_storage>)

# List ownership tests
add_executable(test_list_ownership test_list_ownership.c
    ../../components/components/bt/common/osi/allocator.c
    ../../components/components/bt/common/osi/list.c)
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include)
target_link_libraries(test_list_ownership unity)
add_test(NAME test_list_ownership COMMAND $<TARGET_FILE:test_list_ownership>)
# allocator.c and list.c depend on bt_common.h which lives in the bt common include path
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common)
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/include)

# Osi allocator debug tests (host)
add_executable(test_osi_allocator test_bt_osi_allocator.c 
    ../../components/components/bt/common/osi/allocator.c)
target_compile_definitions(test_osi_allocator PRIVATE HEAP_MEMORY_DEBUG)
target_include_directories(test_osi_allocator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include)
# allocator.c depends on bt_common.h which lives in the bt common include path
target_include_directories(test_osi_allocator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common)
target_include_directories(test_osi_allocator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/include)
target_link_libraries(test_osi_allocator unity)
add_test(NAME test_osi_allocator COMMAND $<TARGET_FILE:test_osi_allocator>)

# Dispatch copy regression test
add_executable(test_dispatch_copy_regression test_dispatch_copy_regression.c
    mocks/bt_app_core_host_stub.c
)
target_link_libraries(test_dispatch_copy_regression unity m)
target_include_directories(test_dispatch_copy_regression PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bluetooth/include
)
target_compile_definitions(test_dispatch_copy_regression PRIVATE UNIT_TEST)
add_test(NAME test_dispatch_copy_regression COMMAND $<TARGET_FILE:test_dispatch_copy_regression>)

# Pairing reply flow tests
add_executable(test_pairing_confirm test_pairing_confirm.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_confirm PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_confirm unity)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_confirm PRIVATE mocks/bt_manager_test_hooks.c)
add_test(NAME test_pairing_confirm COMMAND $<TARGET_FILE:test_pairing_confirm>)

add_executable(test_connect_name test_connect_name.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_connect_name PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_connect_name PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_connect_name unity)
add_test(NAME test_connect_name COMMAND $<TARGET_FILE:test_connect_name>)

add_executable(test_pairing_enter_pin test_pairing_enter_pin.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_enter_pin unity)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/bt_manager_test_hooks.c)
add_test(NAME test_pairing_enter_pin COMMAND $<TARGET_FILE:test_pairing_enter_pin>)

add_executable(test_pairing_negative test_pairing_negative.c
    mocks/mock_gap.c)
target_link_libraries(test_pairing_negative unity)
target_sources(test_pairing_negative PRIVATE mocks/nvs_storage_mock.c)
add_test(NAME test_pairing_negative COMMAND $<TARGET_FILE:test_pairing_negative>)

# Edge-case pairing tests (invalid MACs, missing pending request behavior)
add_executable(test_pairing_edge_cases test_pairing_edge_cases.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_edge_cases unity)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/bt_manager_test_hooks.c)
add_test(NAME test_pairing_edge_cases COMMAND $<TARGET_FILE:test_pairing_edge_cases>)

# Create a consolidated test runner
add_custom_target(run_tests
    COMMAND test_commands
    COMMAND test_bluetooth
    COMMAND test_nvs_storage
    DEPENDS test_commands test_bluetooth
)
