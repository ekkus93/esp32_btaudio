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
    ${CMAKE_CURRENT_SOURCE_DIR}/../component/bt_mock/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../component/test_common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/command_interface/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/nvs_storage
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
)

# util_safe helpers are needed by bt_manager/nvs_storage users in host builds
add_library(util_safe_host OBJECT
    ../../components/util_safe/util_safe.c)
target_include_directories(util_safe_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)

# Shared command-interface handlers for host tests
add_library(command_interface_host OBJECT
    ../../components/command_interface/commands_helpers.c
    ../../components/command_interface/cmd_handlers_system.c
    ../../components/command_interface/cmd_handlers_audio.c
    ../../components/command_interface/cmd_handlers_files.c
    ../../components/command_interface/cmd_handlers_bt.c)

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
target_sources(test_commands PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_commands PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_commands PRIVATE mocks/shim_audio_queue.c)
target_sources(test_commands PRIVATE mocks/fake_esp_err.c)
target_sources(test_commands PRIVATE mocks/mock_a2dp.c)
target_sources(test_commands PRIVATE mocks/mock_avrc.c)
target_link_libraries(test_commands unity util_safe_host command_interface_host)

# Enable CTest and register the test executables so `ctest` can discover them
enable_testing()
add_test(NAME test_commands COMMAND $<TARGET_FILE:test_commands>)

add_executable(test_bluetooth test_bluetooth.c 
    ../../components/bt_manager/bt_manager.c
    mocks/mock_i2s.c)
# Ensure test hooks are linked for bt_manager tests
target_sources(test_bluetooth PRIVATE mocks/bt_manager_test_hooks.c)
# Link with math library for sin function
target_link_libraries(test_bluetooth unity m util_safe_host)
target_sources(test_bluetooth PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_bluetooth PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_bluetooth PRIVATE mocks/fake_esp_err.c)
target_sources(test_bluetooth PRIVATE mocks/mock_a2dp.c)
target_sources(test_bluetooth PRIVATE mocks/mock_avrc.c)
add_test(NAME test_bluetooth COMMAND $<TARGET_FILE:test_bluetooth>)

# AVRCP/A2DP init order test for bt_manager (uses test-only profile init hook)
add_executable(test_bt_manager_profiles
    test_bt_manager_profiles.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c
    mocks/nvs_storage_mock.c
    mocks/mock_audio_and_btstate.c
    mocks/bt_manager_test_hooks.c
    mocks/fake_esp_err.c
    mocks/fake_log.c)
target_compile_definitions(test_bt_manager_profiles PRIVATE UNIT_TEST)
target_link_libraries(test_bt_manager_profiles unity util_safe_host)
add_test(NAME test_bt_manager_profiles COMMAND $<TARGET_FILE:test_bt_manager_profiles>)

## Audio processor test is intentionally a device/component Unity test only.
## The test file lives under `test/component/` and is picked up by the
## on-device test runner. Building it as a native host executable links the
## production `audio_processor` implementation which differs from the
## on-device behavior and causes linking issues (missing device-only symbols).
## To keep host tests lightweight and avoid duplicating production
## implementation in the host build, we do not build `test_audio_processor`
## as a native host test.

## If you need a native host variant, add a separate host-specific
## wrapper that provides the required host stubs or links the correct
## mocks. For now, the test remains device-only.

# Host-variant: build a native host runner for `test_audio_processor` that
# uses light-weight host mocks for interfaces not available on the host.
# This keeps a fast native test while exercising the high-level test logic.
add_executable(test_audio_processor ../../test/component/test_audio_processor.c
    mocks/mock_i2s.c
    mocks/test_main_wrapper.c)
target_include_directories(test_audio_processor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../test/component)
# Ensure host test sees the same public headers as device builds
target_include_directories(test_audio_processor BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
# Link in a dedicated host-only stub for the production audio_processor
# implementation. This avoids pulling in the full production source (which
# contains device-specific dependencies) and prevents duplicate symbol
# collisions with other host mocks (e.g. mock_audio_and_btstate.c).
target_sources(test_audio_processor PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_audio_processor PRIVATE mocks/shim_audio_queue.c)
target_sources(test_audio_processor PRIVATE mocks/nvs_storage_mock.c)
target_compile_definitions(test_audio_processor PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor unity m util_safe_host)
add_test(NAME test_audio_processor COMMAND $<TARGET_FILE:test_audio_processor>)

add_executable(test_nvs_storage test_nvs_storage.c
    mocks/nvs_storage_mock.c)
target_link_libraries(test_nvs_storage unity)
add_test(NAME test_nvs_storage COMMAND $<TARGET_FILE:test_nvs_storage>)

# Host NVS error-path coverage using real nvs_storage.c with injectable wrappers
add_executable(test_nvs_storage_errors
    test_nvs_storage_errors.c
    ../../components/nvs_storage/nvs_storage.c)
target_link_libraries(test_nvs_storage_errors unity util_safe_host)
add_test(NAME test_nvs_storage_errors COMMAND $<TARGET_FILE:test_nvs_storage_errors>)

# Simple host PSRAM unit tests (uses heap_caps mock)
add_executable(test_psram test_psram.c
    mocks/esp_heap_caps_mock.c
    mocks/audio_alloc_host.c)
target_include_directories(test_psram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_link_libraries(test_psram unity)
add_test(NAME test_psram COMMAND $<TARGET_FILE:test_psram>)

# util_safe host coverage: reuse the component's Unity cases on host to keep
# the fast "all tests" path exercising the safety helpers.
add_executable(test_util_safe test_util_safe_host.c)
target_include_directories(test_util_safe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_link_libraries(test_util_safe unity util_safe_host)
add_test(NAME test_util_safe COMMAND $<TARGET_FILE:test_util_safe>)

# Idle I2S failure recovery test (uses production audio_processor + mocks)
add_executable(test_audio_processor_idle_i2s test_audio_processor_idle_i2s.c
    mocks/audio_processor_host_stub.c
    mocks/shim_audio_queue.c
    mocks/esp_heap_caps_mock.c
    mocks/mock_i2s.c
    mocks/mock_gap.c
    mocks/mock_audio_and_btstate.c
    mocks/nvs_storage_mock.c)
target_sources(test_audio_processor_idle_i2s PRIVATE
    mocks/fake_task.c
    mocks/fake_queue.c
    mocks/fake_semphr.c
    mocks/fake_ringbuf.c
    mocks/fake_timer.c
    mocks/fake_esp_err.c
    mocks/fake_log.c
    mocks/fake_debug_helpers.c)
target_include_directories(test_audio_processor_idle_i2s PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_include_directories(test_audio_processor_idle_i2s PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_audio_processor_idle_i2s PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST CONFIG_SPIRAM=1)
target_link_libraries(test_audio_processor_idle_i2s unity m util_safe_host)
add_test(NAME test_audio_processor_idle_i2s COMMAND $<TARGET_FILE:test_audio_processor_idle_i2s>)

# List ownership tests
add_executable(test_list_ownership test_list_ownership.c
    ../../components/components/bt/common/osi/allocator.c
    ../../components/components/bt/common/osi/list.c)
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include)
target_link_libraries(test_list_ownership unity)
add_test(NAME test_list_ownership COMMAND $<TARGET_FILE:test_list_ownership>)

# Host coverage for production audio_queue helpers
add_executable(test_audio_queue_host
    test_audio_queue_host.c
    ../../components/audio_processor/audio_queue.c
    mocks/fake_queue.c
    mocks/fake_task.c
    mocks/fake_timer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_audio_queue_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_audio_queue_host PRIVATE UNIT_TEST)
target_link_libraries(test_audio_queue_host unity util_safe_host)
add_test(NAME test_audio_queue_host COMMAND $<TARGET_FILE:test_audio_queue_host>)

# Host coverage for play_manager enqueue path (ensures WAVs use queue-owned blocks)
add_executable(test_play_manager_host
    test_play_manager_host.c
    ../../components/audio_processor/play_manager.c
    ../../components/audio_processor/audio_queue.c
    ../../components/audio_processor/audio_util.c
    mocks/fake_queue.c
    mocks/fake_task.c
    mocks/fake_timer.c
    mocks/fake_semphr.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_play_manager_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_play_manager_host PRIVATE UNIT_TEST CONFIG_BT_MOCK_TESTING)
target_link_libraries(test_play_manager_host unity util_safe_host m)
add_test(NAME test_play_manager_host COMMAND $<TARGET_FILE:test_play_manager_host>)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
)
target_compile_definitions(test_dispatch_copy_regression PRIVATE UNIT_TEST)
add_test(NAME test_dispatch_copy_regression COMMAND $<TARGET_FILE:test_dispatch_copy_regression>)

# Host harness for bt_app_core queue handling
add_executable(test_bt_app_core_host
    test_bt_app_core_host.c
    ../../components/bt_manager/bt_app_core.c
    mocks/fake_task.c
    mocks/fake_queue.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    ../../components/util_safe/util_safe.c)
target_link_libraries(test_bt_app_core_host unity)
target_compile_definitions(test_bt_app_core_host PRIVATE UNIT_TEST)
add_test(NAME test_bt_app_core_host COMMAND $<TARGET_FILE:test_bt_app_core_host>)

# Pairing reply flow tests
add_executable(test_pairing_confirm test_pairing_confirm.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_confirm PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_confirm unity util_safe_host command_interface_host)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_confirm PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_confirm PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_confirm PRIVATE mocks/shim_audio_queue.c)
target_sources(test_pairing_confirm PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_confirm COMMAND $<TARGET_FILE:test_pairing_confirm>)

add_executable(test_connect_name test_connect_name.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_connect_name PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_connect_name PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_connect_name unity util_safe_host command_interface_host)
target_sources(test_connect_name PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_connect_name PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_connect_name PRIVATE mocks/shim_audio_queue.c)
target_sources(test_connect_name PRIVATE mocks/fake_esp_err.c)
target_sources(test_connect_name PRIVATE mocks/mock_a2dp.c)
target_sources(test_connect_name PRIVATE mocks/mock_avrc.c)
add_test(NAME test_connect_name COMMAND $<TARGET_FILE:test_connect_name>)

add_executable(test_pairing_enter_pin test_pairing_enter_pin.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_enter_pin unity util_safe_host command_interface_host)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/shim_audio_queue.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/mock_avrc.c)
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
target_link_libraries(test_pairing_edge_cases unity util_safe_host command_interface_host)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/shim_audio_queue.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_edge_cases COMMAND $<TARGET_FILE:test_pairing_edge_cases>)

# Pending-state tracking tests for pairing flows
add_executable(test_pairing_pending test_pairing_pending.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_pending PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_pending PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_pending unity util_safe_host)
target_sources(test_pairing_pending PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_pending COMMAND $<TARGET_FILE:test_pairing_pending>)

# Pairing event emission tests using test hook (no UART dependency)
add_executable(test_pairing_event_notifications test_pairing_event_notifications.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_event_notifications unity util_safe_host)
target_sources(test_pairing_event_notifications PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_event_notifications COMMAND $<TARGET_FILE:test_pairing_event_notifications>)

# Event stress test: validates ordering and basic rate-limiting instrumentation
add_executable(test_event_stress test_event_stress.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(test_event_stress unity util_safe_host command_interface_host)
add_test(NAME test_event_stress COMMAND $<TARGET_FILE:test_event_stress>)
# Enable SEQ/TS emission in host-mode for this test target
target_compile_definitions(test_event_stress PRIVATE TEST_INCLUDE_SEQ)
# Ensure commands.c links with bt_manager and nvs storage for host tests
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_event_stress PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_event_stress PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_event_stress PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_event_stress PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_event_stress PRIVATE mocks/shim_audio_queue.c)
target_sources(test_event_stress PRIVATE mocks/fake_esp_err.c)
target_sources(test_event_stress PRIVATE mocks/mock_a2dp.c)
target_sources(test_event_stress PRIVATE mocks/mock_avrc.c)

# Pairing event sequence hardening stress test
add_executable(test_pairing_seq_hardening test_pairing_seq_hardening.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(test_pairing_seq_hardening unity util_safe_host command_interface_host)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/shim_audio_queue.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_seq_hardening COMMAND $<TARGET_FILE:test_pairing_seq_hardening>)

# Ensure beep-flush behavior: host test
add_executable(test_beep_flush test_beep_flush.c)
target_sources(test_beep_flush PRIVATE
    mocks/audio_processor_host_stub.c
    mocks/shim_audio_queue.c
    mocks/esp_heap_caps_mock.c
    mocks/nvs_storage_mock.c)
target_include_directories(test_beep_flush PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_include_directories(test_beep_flush PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_link_libraries(test_beep_flush unity util_safe_host)
add_test(NAME test_beep_flush COMMAND $<TARGET_FILE:test_beep_flush>)
# Enable SEQ/TS emission in host-mode for this test target
target_compile_definitions(test_pairing_seq_hardening PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/shim_audio_queue.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/fake_esp_err.c)

add_executable(test_bt_connection_manager
    test_bt_connection_manager.c
    ../../components/bt_manager/bt_connection_manager.c
    mocks/mock_a2dp.c
    mocks/mock_gap.c
)
target_include_directories(test_bt_connection_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
)
target_link_libraries(test_bt_connection_manager unity util_safe_host)
add_test(NAME test_bt_connection_manager COMMAND $<TARGET_FILE:test_bt_connection_manager>)

add_executable(test_bt_streaming_manager
    test_bt_streaming_manager.c
    ../../components/bt_manager/bt_streaming_manager.c
    mocks/mock_a2dp.c
    mocks/mock_audio_and_btstate.c
    mocks/audio_processor_host_stub.c
    mocks/shim_audio_queue.c
    mocks/fake_task.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    ../../components/util_safe/util_safe.c
)
target_include_directories(test_bt_streaming_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)
target_compile_definitions(test_bt_streaming_manager PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_bt_streaming_manager unity m)
add_test(NAME test_bt_streaming_manager COMMAND $<TARGET_FILE:test_bt_streaming_manager>)

# Small test to lock mock-connection helper behavior: ensure the mock
# helpers can be called without explicit manager init and the test-visible
# getter (bt_get_connection_state) reflects the changes.
add_executable(test_mock_connection_helpers test_mock_connection_helpers.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_audio_and_btstate.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
# Provide test hooks used by bt_manager wrappers (forced-failure hooks)
target_sources(test_mock_connection_helpers PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_mock_connection_helpers PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_mock_connection_helpers unity util_safe_host)
target_sources(test_mock_connection_helpers PRIVATE mocks/fake_esp_err.c)
add_test(NAME test_mock_connection_helpers COMMAND $<TARGET_FILE:test_mock_connection_helpers>)

# Helper binary: run the same burst and print the mock UART TX buffer for parsing
add_executable(dump_event_stress_output dump_event_stress_output.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_link_libraries(dump_event_stress_output m util_safe_host command_interface_host)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_manager.c)
target_compile_definitions(dump_event_stress_output PRIVATE TEST_INCLUDE_SEQ)
target_sources(dump_event_stress_output PRIVATE mocks/nvs_storage_mock.c)
target_sources(dump_event_stress_output PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(dump_event_stress_output PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(dump_event_stress_output PRIVATE mocks/audio_processor_host_stub.c)
target_sources(dump_event_stress_output PRIVATE mocks/shim_audio_queue.c)
target_sources(dump_event_stress_output PRIVATE mocks/fake_esp_err.c)
add_test(NAME dump_event_stress_output COMMAND $<TARGET_FILE:dump_event_stress_output>)

## Adapter runner: compile test_app pairing test and the adapter code so we can
# exercise the host-mode adapter instrumentation (`test_pairing_adapters.c`) as
# a native host binary. This helps capture the `TEST_ADAPT` logs and in-memory
# event queue behavior without flashing a device.
set(TEST_PAIRING_ADAPTER_SRC ../../test_app/main/test_pairing_adapters.c)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_PAIRING_ADAPTER_SRC}")
    set(TEST_PAIRING_ADAPTER_SRC ../../test_app2/main/test_pairing_adapters.c)
endif()

add_executable(test_pairing_adapter_runner
    ../../test_app/main/test_pairing_commands.c
    ${TEST_PAIRING_ADAPTER_SRC}
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c
)
target_sources(test_pairing_adapter_runner PRIVATE test_pairing_adapter_main.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_pairing_adapter_runner unity util_safe_host command_interface_host)
add_test(NAME test_pairing_adapter_runner COMMAND $<TARGET_FILE:test_pairing_adapter_runner>)
# Enable SEQ/TS emission for this runner so we can validate ordering/timestamps
target_compile_definitions(test_pairing_adapter_runner PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/shim_audio_queue.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_avrc.c)

# Beep manager host unit tests (moved under host_test/test_beep_manager)
add_executable(test_beep_manager
    test_beep_manager/test_beep_manager.c
    mocks/shim_audio_queue.c
    ../../components/audio_processor/beep_manager.c
    mocks/fake_queue.c
    mocks/fake_semphr.c
    mocks/fake_task.c
    mocks/fake_timer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    ../../components/util_safe/util_safe.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_beep_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_beep_manager PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_beep_manager unity m pthread)
add_test(NAME test_beep_manager COMMAND $<TARGET_FILE:test_beep_manager>)

# I2S manager host unit tests
add_executable(test_i2s_manager
    test_i2s_manager/test_i2s_manager.c
    mocks/shim_audio_queue.c
    ../../components/audio_processor/i2s_manager.c
    ../../components/audio_processor/audio_util.c
    mocks/fake_queue.c
    mocks/fake_semphr.c
    mocks/fake_task.c
    mocks/fake_timer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_i2s_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_i2s_manager PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_i2s_manager unity m util_safe_host)
add_test(NAME test_i2s_manager COMMAND $<TARGET_FILE:test_i2s_manager>)

# Audio util host unit tests
add_executable(test_audio_util
    test_audio_util/test_audio_util.c
    ../../components/audio_processor/audio_util.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_audio_util PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_audio_util PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_util unity m util_safe_host)
add_test(NAME test_audio_util COMMAND $<TARGET_FILE:test_audio_util>)

# Play manager host unit tests
add_executable(test_play_manager
    test_play_manager/test_play_manager.c
    test_play_manager/shim_play_manager_stubs.c
    ../../components/audio_processor/play_manager.c
    ../../components/audio_processor/audio_queue.c
    mocks/esp_heap_caps_mock.c
    mocks/fake_queue.c
    mocks/fake_semphr.c
    mocks/fake_task.c
    mocks/fake_timer.c
    mocks/fake_esp_err.c
    mocks/fake_log.c
    mocks/fake_debug_helpers.c)
target_include_directories(test_play_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_play_manager PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_play_manager unity m util_safe_host)
add_test(NAME test_play_manager COMMAND $<TARGET_FILE:test_play_manager>)

# Synth manager host unit tests (exercise CONFIG_AUDIO_USE_SYNTH_SOURCE path)
add_executable(test_synth_manager
    test_synth_manager/test_synth_manager.c
    ../../components/audio_processor/synth_manager.c)
target_include_directories(test_synth_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_synth_manager PRIVATE CONFIG_AUDIO_USE_SYNTH_SOURCE UNIT_TEST)
target_link_libraries(test_synth_manager unity m)
add_test(NAME test_synth_manager COMMAND $<TARGET_FILE:test_synth_manager>)

# mem_util host unit tests
# mem_util host unit tests (now using util_safe)
add_executable(test_mem_util
    test_mem_util/test_mem_util.c
    ../../components/util_safe/util_safe.c)
target_include_directories(test_mem_util PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_link_libraries(test_mem_util unity)
add_test(NAME test_mem_util COMMAND $<TARGET_FILE:test_mem_util>)

# Create a consolidated test runner
add_custom_target(run_tests
    COMMAND test_commands
    COMMAND test_bluetooth
    COMMAND test_nvs_storage_errors
    COMMAND test_nvs_storage
    DEPENDS test_commands test_bluetooth test_nvs_storage_errors test_nvs_storage
)
