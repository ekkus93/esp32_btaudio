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
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/nvs_storage
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bluetooth/include
)

# util_safe helpers are needed by bt_manager/nvs_storage users in host builds
add_library(util_safe_host OBJECT ../../components/util_safe/util_safe.c)
target_include_directories(util_safe_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)

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
target_link_libraries(test_commands unity util_safe_host)

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
add_test(NAME test_bluetooth COMMAND $<TARGET_FILE:test_bluetooth>)

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
target_include_directories(test_audio_processor BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include)
# Link in a dedicated host-only stub for the production audio_processor
# implementation. This avoids pulling in the full production source (which
# contains device-specific dependencies) and prevents duplicate symbol
# collisions with other host mocks (e.g. mock_audio_and_btstate.c).
target_sources(test_audio_processor PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_audio_processor PRIVATE mocks/nvs_storage_mock.c)
target_compile_definitions(test_audio_processor PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor unity m)
add_test(NAME test_audio_processor COMMAND $<TARGET_FILE:test_audio_processor>)

add_executable(test_nvs_storage test_nvs_storage.c
    mocks/nvs_storage_mock.c)
target_link_libraries(test_nvs_storage unity)
add_test(NAME test_nvs_storage COMMAND $<TARGET_FILE:test_nvs_storage>)

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

## Real audio_processor host test: link production source with host mocks
add_executable(test_audio_processor_real test_audio_processor_real.c
    ../../main/audio_processor.c
    mocks/esp_heap_caps_mock.c
    mocks/mock_i2s.c
    mocks/mock_gap.c
    mocks/mock_audio_and_btstate.c
    mocks/nvs_storage_mock.c)
# Add foundational host-side RTOS and helper mocks so the production
# audio_processor implementation can link in a host build.
target_sources(test_audio_processor_real PRIVATE
    mocks/fake_task.c
    mocks/fake_queue.c
    mocks/fake_semphr.c
    mocks/fake_ringbuf.c
    mocks/fake_timer.c
    mocks/fake_esp_err.c)
target_sources(test_audio_processor_real PRIVATE
    mocks/fake_log.c
    mocks/fake_debug_helpers.c)
target_include_directories(test_audio_processor_real PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_include_directories(test_audio_processor_real PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include)
target_compile_definitions(test_audio_processor_real PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST CONFIG_SPIRAM=1)
target_link_libraries(test_audio_processor_real unity m)
add_test(NAME test_audio_processor_real COMMAND $<TARGET_FILE:test_audio_processor_real>)

# Include the tag-alignment unit test in the real audio_processor host runner so
# it runs alongside other audio processor integration tests.
## The tag-alignment test is built as a separate native host test so it can
## exercise the real production audio_processor implementation with a
## focused test harness. This avoids colliding with the main runner's
## Unity entrypoint which would cause duplicate `main`/`setUp` symbols.
add_executable(test_audio_tag_alignment test_audio_tag_alignment.c
    ../../main/audio_processor.c
    mocks/esp_heap_caps_mock.c
    mocks/mock_i2s.c
    mocks/mock_gap.c
    mocks/mock_audio_and_btstate.c
    mocks/nvs_storage_mock.c)
target_sources(test_audio_tag_alignment PRIVATE
    mocks/fake_task.c
    mocks/fake_queue.c
    mocks/fake_semphr.c
    mocks/fake_ringbuf.c
    mocks/fake_timer.c
    mocks/fake_esp_err.c
    mocks/fake_log.c
    mocks/fake_debug_helpers.c)
target_include_directories(test_audio_tag_alignment PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_include_directories(test_audio_tag_alignment PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include)
target_compile_definitions(test_audio_tag_alignment PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST CONFIG_SPIRAM=1 CONFIG_AUDIO_TAG_DIAGNOSTICS)
target_link_libraries(test_audio_tag_alignment unity m)
add_test(NAME test_audio_tag_alignment COMMAND $<TARGET_FILE:test_audio_tag_alignment>)

# Idle I2S failure recovery test (uses production audio_processor + mocks)
add_executable(test_audio_processor_idle_i2s test_audio_processor_idle_i2s.c
    ../../main/audio_processor.c
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
target_include_directories(test_audio_processor_idle_i2s PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include)
target_compile_definitions(test_audio_processor_idle_i2s PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST CONFIG_SPIRAM=1)
target_link_libraries(test_audio_processor_idle_i2s unity m)
add_test(NAME test_audio_processor_idle_i2s COMMAND $<TARGET_FILE:test_audio_processor_idle_i2s>)

# List ownership tests
add_executable(test_list_ownership test_list_ownership.c
    ../../components/components/bt/common/osi/allocator.c
    ../../components/components/bt/common/osi/list.c)
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/components/bt/common/osi/include)
target_link_libraries(test_list_ownership unity)
add_test(NAME test_list_ownership COMMAND $<TARGET_FILE:test_list_ownership>)

# Host coverage for audio_i2s (std-channel API)
add_executable(test_audio_i2s_host
    test_audio_i2s_host.c
    ../../components/audio/audio_i2s.c
    mocks/mock_i2s_std.c)
target_include_directories(test_audio_i2s_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio/include)
target_compile_definitions(test_audio_i2s_host PRIVATE UNIT_TEST)
target_link_libraries(test_audio_i2s_host unity)
add_test(NAME test_audio_i2s_host COMMAND $<TARGET_FILE:test_audio_i2s_host>)
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
target_link_libraries(test_pairing_confirm unity util_safe_host)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_confirm PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_confirm PRIVATE mocks/audio_processor_host_stub.c)
add_test(NAME test_pairing_confirm COMMAND $<TARGET_FILE:test_pairing_confirm>)

add_executable(test_connect_name test_connect_name.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_connect_name PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_connect_name PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_connect_name unity util_safe_host)
target_sources(test_connect_name PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_connect_name PRIVATE mocks/audio_processor_host_stub.c)
add_test(NAME test_connect_name COMMAND $<TARGET_FILE:test_connect_name>)

add_executable(test_pairing_enter_pin test_pairing_enter_pin.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_enter_pin unity util_safe_host)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/audio_processor_host_stub.c)
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
target_link_libraries(test_pairing_edge_cases unity util_safe_host)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/audio_processor_host_stub.c)
add_test(NAME test_pairing_edge_cases COMMAND $<TARGET_FILE:test_pairing_edge_cases>)

# Pending-state tracking tests for pairing flows
add_executable(test_pairing_pending test_pairing_pending.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_pending PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_pending PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_pending unity util_safe_host)
add_test(NAME test_pairing_pending COMMAND $<TARGET_FILE:test_pairing_pending>)

# Event stress test: validates ordering and basic rate-limiting instrumentation
add_executable(test_event_stress test_event_stress.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(test_event_stress unity util_safe_host)
add_test(NAME test_event_stress COMMAND $<TARGET_FILE:test_event_stress>)
# Enable SEQ/TS emission in host-mode for this test target
target_compile_definitions(test_event_stress PRIVATE TEST_INCLUDE_SEQ)
# Ensure commands.c links with bt_manager and nvs storage for host tests
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_event_stress PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_event_stress PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_event_stress PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_event_stress PRIVATE mocks/audio_processor_host_stub.c)

# Pairing event sequence hardening stress test
add_executable(test_pairing_seq_hardening test_pairing_seq_hardening.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(test_pairing_seq_hardening unity util_safe_host)
add_test(NAME test_pairing_seq_hardening COMMAND $<TARGET_FILE:test_pairing_seq_hardening>)
# Enable SEQ/TS emission in host-mode for this test target
target_compile_definitions(test_pairing_seq_hardening PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/audio_processor_host_stub.c)

add_executable(test_bt_connection_manager
    test_bt_connection_manager.c
    ../../main/bt_connection_manager.c
    mocks/mock_a2dp.c
    mocks/mock_gap.c
)
target_include_directories(test_bt_connection_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include
)
target_link_libraries(test_bt_connection_manager unity util_safe_host)
add_test(NAME test_bt_connection_manager COMMAND $<TARGET_FILE:test_bt_connection_manager>)

# Small test to lock mock-connection helper behavior: ensure the mock
# helpers can be called without explicit manager init and the test-visible
# getter (bt_get_connection_state) reflects the changes.
add_executable(test_mock_connection_helpers test_mock_connection_helpers.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_audio_and_btstate.c)
# Provide test hooks used by bt_manager wrappers (forced-failure hooks)
target_sources(test_mock_connection_helpers PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_mock_connection_helpers PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_mock_connection_helpers unity util_safe_host)
add_test(NAME test_mock_connection_helpers COMMAND $<TARGET_FILE:test_mock_connection_helpers>)

# Helper binary: run the same burst and print the mock UART TX buffer for parsing
add_executable(dump_event_stress_output dump_event_stress_output.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(dump_event_stress_output m util_safe_host)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_manager.c)
target_compile_definitions(dump_event_stress_output PRIVATE TEST_INCLUDE_SEQ)
target_sources(dump_event_stress_output PRIVATE mocks/nvs_storage_mock.c)
target_sources(dump_event_stress_output PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(dump_event_stress_output PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(dump_event_stress_output PRIVATE mocks/audio_processor_host_stub.c)
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
target_link_libraries(test_pairing_adapter_runner unity util_safe_host)
add_test(NAME test_pairing_adapter_runner COMMAND $<TARGET_FILE:test_pairing_adapter_runner>)
# Enable SEQ/TS emission for this runner so we can validate ordering/timestamps
target_compile_definitions(test_pairing_adapter_runner PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/audio_processor_host_stub.c)

# Create a consolidated test runner
add_custom_target(run_tests
    COMMAND test_commands
    COMMAND test_bluetooth
    COMMAND test_nvs_storage
    DEPENDS test_commands test_bluetooth
)
