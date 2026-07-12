cmake_minimum_required(VERSION 3.10)

project(esp_bt_audio_source_tests C)

# Define UNIT_TEST for the test build
add_compile_definitions(UNIT_TEST)

# Undefine ESP_PLATFORM for host tests to ensure host code path is taken
add_compile_definitions(ESP_PLATFORM_UNDEFINED)
add_compile_options(-UESP_PLATFORM)

# Code coverage support (gcov/lcov)
option(ENABLE_COVERAGE "Enable code coverage reporting with gcov/lcov" OFF)
if(ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled - adding --coverage flags")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()

# AddressSanitizer support (fast memory error detection)
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)
if(ENABLE_ASAN)
    if(ENABLE_COVERAGE)
        message(WARNING "AddressSanitizer and coverage are both enabled - this may cause conflicts")
    endif()
    message(STATUS "AddressSanitizer enabled - adding -fsanitize=address flags")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/osi/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/platform_shim
)

# util_safe helpers are needed by bt_manager/nvs_storage users in host builds
add_library(util_safe_host OBJECT
    ../../components/util_safe/util_safe.c)
target_include_directories(util_safe_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)

# platform_shim for host builds (CODE_REVIEW8 P2.2 Phase 1+2+3+4: sync + timing + memory + storage)
add_library(platform_shim_host OBJECT
    ../../components/platform_shim/platform_sync_host.c
    ../../components/platform_shim/platform_timing_host.c
    ../../components/platform_shim/platform_memory_host.c
    ../../components/platform_shim/platform_storage_host.c)
target_include_directories(platform_shim_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/platform_shim)

# Shared command-interface handlers for host tests
add_library(command_interface_host OBJECT
    ../../components/command_interface/command_interface.c
    ../../components/command_interface/commands_helpers.c
    ../../components/command_interface/cmd_handlers_system.c
    ../../components/command_interface/cmd_handlers_audio.c
    ../../components/command_interface/cmd_handlers_bt.c
    ../../components/command_interface/cmd_handlers_debug.c
    ../../components/command_interface/uart_audio.c)

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
    test_commands_parse.c
    test_commands_audio.c
    test_commands_bt.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_sources(test_commands PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_commands PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_commands PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_commands PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_commands PRIVATE mocks/fake_esp_err.c)
target_sources(test_commands PRIVATE mocks/mock_a2dp.c)
target_sources(test_commands PRIVATE mocks/mock_avrc.c)
target_link_libraries(test_commands unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_commands PRIVATE UNIT_TEST)

# Enable CTest and register the test executables so `ctest` can discover them
enable_testing()
add_test(NAME test_commands COMMAND $<TARGET_FILE:test_commands>)

# Command handler audio tests (Phase 1 - error paths and edge cases)
add_executable(test_cmd_handlers_audio test_cmd_handlers_audio.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_cmd_handlers_audio PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_cmd_handlers_audio PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_cmd_handlers_audio PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_cmd_handlers_audio PRIVATE mocks/fake_esp_err.c)
target_sources(test_cmd_handlers_audio PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_cmd_handlers_audio unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_cmd_handlers_audio PRIVATE UNIT_TEST)
add_test(NAME test_cmd_handlers_audio COMMAND $<TARGET_FILE:test_cmd_handlers_audio>)

# Command handler Bluetooth tests (Phase 1 - BT command error paths)
add_executable(test_cmd_handlers_bt test_cmd_handlers_bt.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_cmd_handlers_bt PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_cmd_handlers_bt PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_cmd_handlers_bt PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_cmd_handlers_bt PRIVATE mocks/fake_esp_err.c)
target_sources(test_cmd_handlers_bt PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_cmd_handlers_bt unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_cmd_handlers_bt PRIVATE UNIT_TEST)
add_test(NAME test_cmd_handlers_bt COMMAND $<TARGET_FILE:test_cmd_handlers_bt>)

# Phase 7.1: component integration flows (command -> BT -> audio, pairing persistence)
add_executable(test_integration_flows test_integration_flows.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_integration_flows PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_integration_flows PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_integration_flows PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_integration_flows PRIVATE mocks/fake_esp_err.c)
target_sources(test_integration_flows PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_integration_flows unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_integration_flows PRIVATE UNIT_TEST)
add_test(NAME test_integration_flows COMMAND $<TARGET_FILE:test_integration_flows>)

# Phase 7.2: concurrency/race behavior (command+BT events, audio callback+volume+beep, command flood)
add_executable(test_concurrency test_concurrency.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_concurrency PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_concurrency PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_concurrency PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_concurrency PRIVATE mocks/fake_esp_err.c)
target_sources(test_concurrency PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_concurrency unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_concurrency PRIVATE UNIT_TEST)
add_test(NAME test_concurrency COMMAND $<TARGET_FILE:test_concurrency>)

add_executable(test_bluetooth test_bluetooth.c 
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_i2s.c)
# Ensure test hooks are linked for bt_manager tests
target_sources(test_bluetooth PRIVATE mocks/bt_manager_test_hooks.c)
# Link with math library for sin function
target_link_libraries(test_bluetooth unity m util_safe_host platform_shim_host)
target_sources(test_bluetooth PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_bluetooth PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_bluetooth PRIVATE mocks/fake_esp_err.c)
target_sources(test_bluetooth PRIVATE mocks/mock_gap.c)
target_sources(test_bluetooth PRIVATE mocks/mock_a2dp.c)
target_sources(test_bluetooth PRIVATE mocks/mock_avrc.c)
target_compile_definitions(test_bluetooth PRIVATE UNIT_TEST)
add_test(NAME test_bluetooth COMMAND $<TARGET_FILE:test_bluetooth>)

# AVRCP/A2DP init order test for bt_manager (uses test-only profile init hook)
add_executable(test_bt_manager_profiles
    test_bt_manager_profiles.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c
    mocks/mock_gap.c
    mocks/nvs_storage_mock.c
    mocks/mock_audio_and_btstate.c
    mocks/bt_manager_test_hooks.c
    mocks/fake_esp_err.c
    mocks/fake_log.c)
target_sources(test_bt_manager_profiles PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_compile_definitions(test_bt_manager_profiles PRIVATE UNIT_TEST)
target_link_libraries(test_bt_manager_profiles unity util_safe_host platform_shim_host)
add_test(NAME test_bt_manager_profiles COMMAND $<TARGET_FILE:test_bt_manager_profiles>)

# Phase 5.1: bt_manager edge cases (profile init partial failures, connection, pairing)
add_executable(test_bt_manager_edge_cases
    test_bt_manager_edge_cases.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c
    mocks/mock_gap.c
    mocks/nvs_storage_mock.c
    mocks/mock_audio_and_btstate.c
    mocks/bt_manager_test_hooks.c
    mocks/fake_esp_err.c
    mocks/fake_log.c)
target_sources(test_bt_manager_edge_cases PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_compile_definitions(test_bt_manager_edge_cases PRIVATE UNIT_TEST)
target_link_libraries(test_bt_manager_edge_cases unity util_safe_host platform_shim_host)
add_test(NAME test_bt_manager_edge_cases COMMAND $<TARGET_FILE:test_bt_manager_edge_cases>)

# Phase 5.2: bt_connection_manager edge cases (reconnect retry, NULL callbacks, state persistence)
add_executable(test_bt_connection_manager_edge_cases
    test_bt_connection_manager_edge_cases.c
    ../../components/bt_manager/bt_connection_manager.c
    mocks/mock_a2dp.c
    mocks/mock_gap.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/nvs_storage_mock.c)
target_include_directories(test_bt_connection_manager_edge_cases PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
)
target_compile_definitions(test_bt_connection_manager_edge_cases PRIVATE UNIT_TEST CONFIG_BT_MOCK_TESTING=1)
target_link_libraries(test_bt_connection_manager_edge_cases unity util_safe_host platform_shim_host)
add_test(NAME test_bt_connection_manager_edge_cases COMMAND $<TARGET_FILE:test_bt_connection_manager_edge_cases>)

# Phase 5.3: bt_streaming_manager.c edge cases (7 tests)
add_executable(test_bt_streaming_manager_edge_cases
    test_bt_streaming_manager_edge_cases.c
    ../../components/bt_manager/bt_streaming_manager.c
    mocks/mock_a2dp.c
    mocks/mock_gap.c
    mocks/fake_log.c
    mocks/fake_task.c
    mocks/audio_processor_host_stub.c
    mocks/mock_audio_and_btstate.c
)
target_include_directories(test_bt_streaming_manager_edge_cases PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
)
target_compile_definitions(test_bt_streaming_manager_edge_cases PRIVATE UNIT_TEST CONFIG_BT_MOCK_TESTING=1)
target_link_libraries(test_bt_streaming_manager_edge_cases unity util_safe_host platform_shim_host)
add_test(NAME test_bt_streaming_manager_edge_cases COMMAND $<TARGET_FILE:test_bt_streaming_manager_edge_cases>)



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
target_sources(test_audio_processor PRIVATE mocks/nvs_storage_mock.c)
target_compile_definitions(test_audio_processor PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor unity m util_safe_host)
add_test(NAME test_audio_processor COMMAND $<TARGET_FILE:test_audio_processor>)

add_executable(test_nvs_storage test_nvs_storage.c
    mocks/nvs_storage_mock.c)
target_link_libraries(test_nvs_storage unity platform_shim_host util_safe_host)
add_test(NAME test_nvs_storage COMMAND $<TARGET_FILE:test_nvs_storage>)

# Host NVS error-path coverage using real nvs_storage.c with injectable wrappers
add_executable(test_nvs_storage_errors
    test_nvs_storage_errors.c
    ../../components/nvs_storage/nvs_storage.c)
target_link_libraries(test_nvs_storage_errors unity util_safe_host platform_shim_host)
add_test(NAME test_nvs_storage_errors COMMAND $<TARGET_FILE:test_nvs_storage_errors>)

# UT-1: direct coverage of the REAL host storage KV store (platform_storage_host.c),
# which the mock-backed nvs suites shadow (0% baseline). Links the real object.
add_executable(test_platform_storage_host test_platform_storage_host.c)
target_link_libraries(test_platform_storage_host unity platform_shim_host)
add_test(NAME test_platform_storage_host COMMAND $<TARGET_FILE:test_platform_storage_host>)

# UT-3: DEBUG subcommand handlers (cmd_handlers_debug.c had no host test — 8/13
# handlers never invoked). Links the real handler + commands_helpers; the test file
# provides capture stubs for the weak cmd_send_response/cmd_send_event_pair and the
# g_cmd_mock_* globals (cmd_handlers_bt.c is intentionally not linked).
add_executable(test_cmd_handlers_debug test_cmd_handlers_debug.c
    ../../components/command_interface/cmd_handlers_debug.c
    ../../components/command_interface/commands_helpers.c)
target_link_libraries(test_cmd_handlers_debug unity util_safe_host platform_shim_host)
add_test(NAME test_cmd_handlers_debug COMMAND $<TARGET_FILE:test_cmd_handlers_debug>)

# UT-9: cmd_status_to_name enum coverage (was the only non-weak fn in
# command_interface.c at 14.3% func). Links just command_interface.c.
add_executable(test_cmd_status_to_name test_cmd_status_to_name.c
    ../../components/command_interface/command_interface.c)
target_link_libraries(test_cmd_status_to_name unity)
add_test(NAME test_cmd_status_to_name COMMAND $<TARGET_FILE:test_cmd_status_to_name>)

# UT-4: nvs_storage domain layer against the REAL platform store (wrappers intact),
# covering the typed accessors + paired-device add/remove/shift logic that the
# mock-backed nvs suites skip (47.7% baseline). Distinct from test_nvs_storage_errors,
# which overrides the weak wrappers for fault injection.
add_executable(test_nvs_storage_domain test_nvs_storage_domain.c
    ../../components/nvs_storage/nvs_storage.c)
target_link_libraries(test_nvs_storage_domain unity util_safe_host platform_shim_host)
add_test(NAME test_nvs_storage_domain COMMAND $<TARGET_FILE:test_nvs_storage_domain>)

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

# Audio processor core logic tests (Phase 6.1 - source priority + source switch stats)
add_executable(test_audio_processor_core_logic
    test_audio_processor_core_logic.c
    ../../components/audio_processor/audio_processor.c
    ../../components/audio_processor/audio_processor_state.c
    mocks/audio_processor_core_logic_stubs.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/fake_semphr.c)
target_include_directories(test_audio_processor_core_logic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_compile_definitions(test_audio_processor_core_logic PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor_core_logic unity util_safe_host platform_shim_host)
add_test(NAME test_audio_processor_core_logic COMMAND $<TARGET_FILE:test_audio_processor_core_logic>)

# UARTAUDIO step 1: UART audio frame parser (pure module, no mocks needed)
add_executable(test_uart_audio_frame test_uart_audio_frame.c
    ../../components/command_interface/uart_audio_frame.c)
target_link_libraries(test_uart_audio_frame unity)
target_compile_definitions(test_uart_audio_frame PRIVATE UNIT_TEST)
add_test(NAME test_uart_audio_frame COMMAND $<TARGET_FILE:test_uart_audio_frame>)

# I2S link payload phase detector/extractor (pure module, no mocks needed).
# Guards the S3->WROOM32 capture fixes from the 2026-07-11 bring-up.
add_executable(test_i2s_frame_extract test_i2s_frame_extract.c
    ../../components/audio_processor/i2s_frame_extract.c)
target_include_directories(test_i2s_frame_extract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_link_libraries(test_i2s_frame_extract unity)
target_compile_definitions(test_i2s_frame_extract PRIVATE UNIT_TEST)
add_test(NAME test_i2s_frame_extract COMMAND $<TARGET_FILE:test_i2s_frame_extract>)

# UARTAUDIO step 2: 2x stereo upsampler (lives in uart_source.c, needs its mocks)
add_executable(test_uart_upsample2x test_uart_upsample2x.c
    ../../components/audio_processor/uart_source.c
    ../../components/audio_processor/audio_ringbuffer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_uart_upsample2x PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_uart_upsample2x PRIVATE UNIT_TEST)
target_link_libraries(test_uart_upsample2x unity util_safe_host platform_shim_host)
add_test(NAME test_uart_upsample2x COMMAND $<TARGET_FILE:test_uart_upsample2x>)

# UARTAUDIO step 5: UARTAUDIO command plumbing + cmd_process streaming gate
add_executable(test_uart_audio_cmd test_uart_audio_cmd.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_uart_audio_cmd PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_uart_audio_cmd PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_uart_audio_cmd PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_uart_audio_cmd PRIVATE mocks/fake_esp_err.c)
target_sources(test_uart_audio_cmd PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_uart_audio_cmd unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_uart_audio_cmd PRIVATE UNIT_TEST)
add_test(NAME test_uart_audio_cmd COMMAND $<TARGET_FILE:test_uart_audio_cmd>)

# Dual-UART command interface (secondary command port routing)
add_executable(test_cmd_dual_uart test_cmd_dual_uart.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_cmd_dual_uart PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_cmd_dual_uart PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_cmd_dual_uart PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_cmd_dual_uart PRIVATE mocks/fake_esp_err.c)
target_sources(test_cmd_dual_uart PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_cmd_dual_uart unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_cmd_dual_uart PRIVATE UNIT_TEST)
add_test(NAME test_cmd_dual_uart COMMAND $<TARGET_FILE:test_cmd_dual_uart>)

# UARTAUDIO step 6: reader RX pump (real parser + real uart_source, no mocks)
add_executable(test_uart_audio_rx test_uart_audio_rx.c
    ../../components/command_interface/uart_audio_rx.c
    ../../components/command_interface/uart_audio_frame.c
    ../../components/audio_processor/uart_source.c
    ../../components/audio_processor/audio_ringbuffer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_uart_audio_rx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_uart_audio_rx PRIVATE UNIT_TEST)
target_link_libraries(test_uart_audio_rx unity util_safe_host platform_shim_host)
add_test(NAME test_uart_audio_rx COMMAND $<TARGET_FILE:test_uart_audio_rx>)

# UARTAUDIO step 3: uart_source lifecycle (staging ring + fill + stats)
add_executable(test_uart_source test_uart_source.c
    ../../components/audio_processor/uart_source.c
    ../../components/audio_processor/audio_ringbuffer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_uart_source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_uart_source PRIVATE UNIT_TEST)
target_link_libraries(test_uart_source unity util_safe_host platform_shim_host)
add_test(NAME test_uart_source COMMAND $<TARGET_FILE:test_uart_source>)

# TEST-4: commands_helpers.c — cmd_safe_copy/append, copy_truncated_identifier, parse_log_level
add_executable(test_commands_helpers test_commands_helpers.c
    ../../components/command_interface/commands_helpers.c
    mocks/fake_esp_err.c)
target_include_directories(test_commands_helpers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/command_interface/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/command_interface
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/platform_shim)
target_link_libraries(test_commands_helpers unity util_safe_host platform_shim_host)
target_compile_definitions(test_commands_helpers PRIVATE UNIT_TEST)
add_test(NAME test_commands_helpers COMMAND $<TARGET_FILE:test_commands_helpers>)

# TEST-3: audio_processor_state.c — audio_bytes_per_sample, audio_get_runtime_work_bytes
add_executable(test_audio_processor_state
    test_audio_processor_state.c
    ../../components/audio_processor/audio_processor_state.c
    mocks/audio_processor_core_logic_stubs.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/fake_semphr.c)
target_include_directories(test_audio_processor_state PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_compile_definitions(test_audio_processor_state PRIVATE UNIT_TEST)
target_link_libraries(test_audio_processor_state unity util_safe_host platform_shim_host)
add_test(NAME test_audio_processor_state COMMAND $<TARGET_FILE:test_audio_processor_state>)

# Audio processor read-path tests (Phase 6.2 - ring drop/underrun/null bytes_read)
add_executable(test_audio_processor_read
    test_audio_processor_read.c
    ../../components/audio_processor/audio_processor_read.c
    ../../components/audio_processor/audio_processor_state.c
    ../../components/audio_processor/audio_processor_diag.c
    ../../components/audio_processor/audio_ringbuffer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_audio_processor_read PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_compile_definitions(test_audio_processor_read PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor_read unity util_safe_host platform_shim_host)
add_test(NAME test_audio_processor_read COMMAND $<TARGET_FILE:test_audio_processor_read>)

# Audio processor diagnostics/status tests (Phase 6.4)
add_executable(test_audio_processor_diag
    test_audio_processor_diag.c
    ../../components/audio_processor/audio_processor.c
    ../../components/audio_processor/audio_processor_diag.c
    ../../components/audio_processor/audio_processor_state.c
    mocks/audio_processor_core_logic_stubs.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/fake_semphr.c)
target_include_directories(test_audio_processor_diag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_compile_definitions(test_audio_processor_diag PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor_diag unity util_safe_host platform_shim_host)
add_test(NAME test_audio_processor_diag COMMAND $<TARGET_FILE:test_audio_processor_diag>)

# Audio processor beep edge case tests (Phase 3.1 - TDD)
add_executable(test_audio_processor_beep_edge_cases test_audio_processor_beep_edge_cases.c
    ../../components/audio_processor/audio_processor_beep.c
    ../../components/audio_processor/audio_processor_state.c
    mocks/esp_heap_caps_mock.c
    mocks/nvs_storage_mock.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/fake_debug_helpers.c
    mocks/fake_task.c
    mocks/fake_queue.c
    mocks/fake_semphr.c)
target_include_directories(test_audio_processor_beep_edge_cases PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_audio_processor_beep_edge_cases PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_audio_processor_beep_edge_cases unity m util_safe_host)
add_test(NAME test_audio_processor_beep_edge_cases COMMAND $<TARGET_FILE:test_audio_processor_beep_edge_cases>)

# Beep manager edge case tests (Phase 3.2 - TDD)
add_executable(test_beep_manager_edge_cases test_beep_manager_edge_cases.c
    ../../components/audio_processor/beep_manager.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_beep_manager_edge_cases PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_beep_manager_edge_cases PRIVATE UNIT_TEST)
target_link_libraries(test_beep_manager_edge_cases unity m)
add_test(NAME test_beep_manager_edge_cases COMMAND $<TARGET_FILE:test_beep_manager_edge_cases>)

# I2S Manager configuration error tests (Phase 4.1 - TDD)
add_executable(test_i2s_manager_config_errors test_i2s_manager_config_errors.c
    ../../components/audio_processor/i2s_manager.c
    ../../components/audio_processor/audio_util.c
    mocks/mock_i2s_std.c
    mocks/fake_queue.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_i2s_manager_config_errors PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_i2s_manager_config_errors PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_i2s_manager_config_errors unity m util_safe_host)
add_test(NAME test_i2s_manager_config_errors COMMAND $<TARGET_FILE:test_i2s_manager_config_errors>)

# I2S Manager runtime error tests (Phase 4.2 - TDD)
add_executable(test_i2s_manager_runtime_errors test_i2s_manager_runtime_errors.c
    ../../components/audio_processor/i2s_manager.c
    ../../components/audio_processor/audio_util.c
    mocks/mock_i2s_std.c
    mocks/fake_queue.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_i2s_manager_runtime_errors PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_i2s_manager_runtime_errors PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_i2s_manager_runtime_errors unity m util_safe_host)
add_test(NAME test_i2s_manager_runtime_errors COMMAND $<TARGET_FILE:test_i2s_manager_runtime_errors>)

# I2S Manager cleanup error tests (Phase 4.3 - TDD)
add_executable(test_i2s_manager_cleanup_errors test_i2s_manager_cleanup_errors.c
    ../../components/audio_processor/i2s_manager.c
    ../../components/audio_processor/audio_util.c
    mocks/mock_i2s_std.c
    mocks/fake_queue.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_i2s_manager_cleanup_errors PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_i2s_manager_cleanup_errors PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_i2s_manager_cleanup_errors unity m util_safe_host)
add_test(NAME test_i2s_manager_cleanup_errors COMMAND $<TARGET_FILE:test_i2s_manager_cleanup_errors>)

# I2S Manager mock queue tests (Phase 4.4 - TDD)
add_executable(test_i2s_manager_mock_queue test_i2s_manager_mock_queue.c
    ../../components/audio_processor/i2s_manager.c
    ../../components/audio_processor/audio_util.c
    mocks/mock_i2s_std.c
    mocks/fake_queue.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_i2s_manager_mock_queue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_i2s_manager_mock_queue PRIVATE CONFIG_BT_MOCK_TESTING UNIT_TEST)
target_link_libraries(test_i2s_manager_mock_queue unity m util_safe_host)
add_test(NAME test_i2s_manager_mock_queue COMMAND $<TARGET_FILE:test_i2s_manager_mock_queue>)

# List ownership tests
add_executable(test_list_ownership test_list_ownership.c
    ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/osi/allocator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/osi/list.c)
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/osi/include)
target_link_libraries(test_list_ownership unity)
add_test(NAME test_list_ownership COMMAND $<TARGET_FILE:test_list_ownership>)

# Host coverage for audio ring buffer (SPSC for audio engine architecture)
add_executable(test_audio_ringbuffer
    test_audio_ringbuffer.c
    ../../components/audio_processor/audio_ringbuffer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_audio_ringbuffer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_audio_ringbuffer PRIVATE UNIT_TEST)
target_link_libraries(test_audio_ringbuffer unity util_safe_host platform_shim_host pthread)
add_test(NAME test_audio_ringbuffer COMMAND $<TARGET_FILE:test_audio_ringbuffer>)

# Host stress tests for audio engine (ring buffer + sources)
add_executable(test_audio_engine_stress
    test_audio_engine_stress.c
    ../../components/audio_processor/audio_ringbuffer.c
    mocks/fake_log.c
    mocks/fake_esp_err.c
    mocks/esp_heap_caps_mock.c)
target_include_directories(test_audio_engine_stress PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include)
target_compile_definitions(test_audio_engine_stress PRIVATE UNIT_TEST)
target_link_libraries(test_audio_engine_stress unity util_safe_host platform_shim_host)
add_test(NAME test_audio_engine_stress COMMAND $<TARGET_FILE:test_audio_engine_stress>)

# allocator.c and list.c depend on bt_common.h which lives in the bt common include path
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common)
target_include_directories(test_list_ownership PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/include)

# Osi allocator debug tests (host)
add_executable(test_osi_allocator test_bt_osi_allocator.c 
    ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/osi/allocator.c)
target_compile_definitions(test_osi_allocator PRIVATE HEAP_MEMORY_DEBUG)
target_include_directories(test_osi_allocator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/osi/include)
# allocator.c depends on bt_common.h which lives in the bt common include path
target_include_directories(test_osi_allocator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common)
target_include_directories(test_osi_allocator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/esp_idf_stubs/bt/common/include)
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
target_link_libraries(test_bt_app_core_host unity platform_shim_host)
target_compile_definitions(test_bt_app_core_host PRIVATE UNIT_TEST)
add_test(NAME test_bt_app_core_host COMMAND $<TARGET_FILE:test_bt_app_core_host>)

# Pairing reply flow tests
add_executable(test_pairing_confirm test_pairing_confirm.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_confirm PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_confirm unity util_safe_host command_interface_host platform_shim_host)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_confirm PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_confirm PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_confirm PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_confirm PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_confirm PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_confirm COMMAND $<TARGET_FILE:test_pairing_confirm>)

add_executable(test_connect_name test_connect_name.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_connect_name PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_connect_name PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_connect_name PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_connect_name PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_connect_name PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_connect_name PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_connect_name PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_connect_name PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_connect_name unity util_safe_host command_interface_host platform_shim_host)
target_sources(test_connect_name PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_connect_name PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_connect_name PRIVATE mocks/fake_esp_err.c)
target_sources(test_connect_name PRIVATE mocks/mock_a2dp.c)
target_sources(test_connect_name PRIVATE mocks/mock_avrc.c)
add_test(NAME test_connect_name COMMAND $<TARGET_FILE:test_connect_name>)

add_executable(test_pairing_enter_pin test_pairing_enter_pin.c
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_enter_pin unity util_safe_host command_interface_host platform_shim_host)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_enter_pin PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_enter_pin PRIVATE mocks/audio_processor_host_stub.c)
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
target_link_libraries(test_pairing_edge_cases unity util_safe_host command_interface_host platform_shim_host)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_edge_cases PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_edge_cases PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_edge_cases COMMAND $<TARGET_FILE:test_pairing_edge_cases>)

# Pending-state tracking tests for pairing flows
add_executable(test_pairing_pending test_pairing_pending.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_pending PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_pending PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_pending PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_pending PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_pending PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_pending PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_pending PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_pending PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_pending unity util_safe_host platform_shim_host)
target_sources(test_pairing_pending PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_gap.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_pending PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_pending COMMAND $<TARGET_FILE:test_pairing_pending>)

# Pairing event emission tests using test hook (no UART dependency)
add_executable(test_pairing_event_notifications test_pairing_event_notifications.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_event_notifications PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_event_notifications PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_event_notifications PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_event_notifications PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_event_notifications PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_event_notifications PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_pairing_event_notifications unity util_safe_host platform_shim_host)
target_sources(test_pairing_event_notifications PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_gap.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_event_notifications PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_event_notifications COMMAND $<TARGET_FILE:test_pairing_event_notifications>)

# Event stress test: validates ordering and basic rate-limiting instrumentation
add_executable(test_event_stress test_event_stress.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(test_event_stress unity util_safe_host command_interface_host platform_shim_host)
add_test(NAME test_event_stress COMMAND $<TARGET_FILE:test_event_stress>)
# Enable SEQ/TS emission in host-mode for this test target
target_compile_definitions(test_event_stress PRIVATE TEST_INCLUDE_SEQ)
# Ensure commands.c links with bt_manager and nvs storage for host tests
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_event_stress PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_event_stress PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_event_stress PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_event_stress PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_event_stress PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_event_stress PRIVATE mocks/fake_esp_err.c)
target_sources(test_event_stress PRIVATE mocks/mock_a2dp.c)
target_sources(test_event_stress PRIVATE mocks/mock_avrc.c)

# Pairing event sequence hardening stress test
add_executable(test_pairing_seq_hardening test_pairing_seq_hardening.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c)
target_link_libraries(test_pairing_seq_hardening unity util_safe_host command_interface_host platform_shim_host)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_avrc.c)
add_test(NAME test_pairing_seq_hardening COMMAND $<TARGET_FILE:test_pairing_seq_hardening>)

# Enable SEQ/TS emission in host-mode for this test target
target_compile_definitions(test_pairing_seq_hardening PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_seq_hardening PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_seq_hardening PRIVATE mocks/fake_esp_err.c)

add_executable(test_bt_connection_manager
    test_bt_connection_manager.c
    ../../components/bt_manager/bt_connection_manager.c
    mocks/mock_a2dp.c
    mocks/mock_gap.c
    mocks/nvs_storage_mock.c
)
target_include_directories(test_bt_connection_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
)
target_link_libraries(test_bt_connection_manager unity util_safe_host platform_shim_host)
add_test(NAME test_bt_connection_manager COMMAND $<TARGET_FILE:test_bt_connection_manager>)

add_executable(test_bt_streaming_manager
    test_bt_streaming_manager.c
    ../../components/bt_manager/bt_streaming_manager.c
    mocks/mock_a2dp.c
    mocks/mock_audio_and_btstate.c
    mocks/audio_processor_host_stub.c
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
    mocks/mock_gap.c
    mocks/mock_avrc.c)
# Provide test hooks used by bt_manager wrappers (forced-failure hooks)
target_sources(test_mock_connection_helpers PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_mock_connection_helpers PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_mock_connection_helpers PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_mock_connection_helpers PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_mock_connection_helpers PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_mock_connection_helpers PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_mock_connection_helpers PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_mock_connection_helpers PRIVATE mocks/nvs_storage_mock.c)
target_link_libraries(test_mock_connection_helpers unity util_safe_host platform_shim_host)
target_sources(test_mock_connection_helpers PRIVATE mocks/fake_esp_err.c)
add_test(NAME test_mock_connection_helpers COMMAND $<TARGET_FILE:test_mock_connection_helpers>)

# Helper binary: run the same burst and print the mock UART TX buffer for parsing
add_executable(dump_event_stress_output dump_event_stress_output.c
    ../../components/command_interface/commands.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_link_libraries(dump_event_stress_output m util_safe_host command_interface_host platform_shim_host)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(dump_event_stress_output PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_compile_definitions(dump_event_stress_output PRIVATE TEST_INCLUDE_SEQ)
target_sources(dump_event_stress_output PRIVATE mocks/nvs_storage_mock.c)
target_sources(dump_event_stress_output PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(dump_event_stress_output PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(dump_event_stress_output PRIVATE mocks/audio_processor_host_stub.c)
target_sources(dump_event_stress_output PRIVATE mocks/fake_esp_err.c)
target_sources(dump_event_stress_output PRIVATE mocks/mock_gap.c)
add_test(NAME dump_event_stress_output COMMAND $<TARGET_FILE:dump_event_stress_output>)

## Adapter runner: compile test_bluetooth pairing test and the adapter code so we can
# exercise the host-mode adapter instrumentation (`test_pairing_adapters.c`) as
# a native host binary. This helps capture the `TEST_ADAPT` logs and in-memory
# event queue behavior without flashing a device.
set(TEST_PAIRING_ADAPTER_SRC ../../test_bluetooth/main/test_pairing_adapters.c)

add_executable(test_pairing_adapter_runner
    ../../test_bluetooth/main/test_pairing_commands.c
    ${TEST_PAIRING_ADAPTER_SRC}
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c
)
target_sources(test_pairing_adapter_runner PRIVATE test_pairing_adapter_main.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_manager.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_pairing_adapter_runner unity util_safe_host command_interface_host platform_shim_host)
add_test(NAME test_pairing_adapter_runner COMMAND $<TARGET_FILE:test_pairing_adapter_runner>)
# Enable SEQ/TS emission for this runner so we can validate ordering/timestamps
target_compile_definitions(test_pairing_adapter_runner PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_gap.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_avrc.c)

# Audio resampler stream unit tests (stateful streaming resampler)
add_executable(test_audio_resampler_stream
    test_audio_resampler_stream.c
    ../../components/audio_processor/audio_resampler_stream.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_audio_resampler_stream PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/util_safe/include)
target_compile_definitions(test_audio_resampler_stream PRIVATE UNIT_TEST)
target_link_libraries(test_audio_resampler_stream util_safe_host unity m)
add_test(NAME test_audio_resampler_stream COMMAND $<TARGET_FILE:test_audio_resampler_stream>)

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

# Synth manager host unit tests (exercise CONFIG_AUDIO_USE_SYNTH_SOURCE path)
add_executable(test_synth_manager
    test_synth_manager/test_synth_manager.c
    test_synth_manager/stub_audio_config.c
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

# Phase 5.4: bt_scan edge cases (NEW MODULE - device discovery, scan start/stop, state sync)
add_executable(test_bt_scan
    test_bt_scan.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c
    mocks/nvs_storage_mock.c
    mocks/mock_audio_and_btstate.c
    mocks/bt_manager_test_hooks.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_bt_scan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
)
target_compile_definitions(test_bt_scan PRIVATE UNIT_TEST)
target_link_libraries(test_bt_scan unity util_safe_host platform_shim_host)
add_test(NAME test_bt_scan COMMAND $<TARGET_FILE:test_bt_scan>)

# Phase 5.5: bt_manager connection/pairing/events edge cases (14 tests)
add_executable(test_bt_manager_connection_pairing_events
    test_bt_manager_connection_pairing_events.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c
    mocks/mock_gap.c
    mocks/nvs_storage_mock.c
    mocks/mock_audio_and_btstate.c
    mocks/bt_manager_test_hooks.c
    mocks/fake_esp_err.c
    mocks/fake_log.c
)
target_include_directories(test_bt_manager_connection_pairing_events PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_manager/include
)
target_compile_definitions(test_bt_manager_connection_pairing_events PRIVATE UNIT_TEST CONFIG_BT_MOCK_TESTING=1)
target_link_libraries(test_bt_manager_connection_pairing_events unity util_safe_host platform_shim_host)
add_test(NAME test_bt_manager_connection_pairing_events COMMAND $<TARGET_FILE:test_bt_manager_connection_pairing_events>)

# ============================================================================
# Section 8.3 Tests - Untested Supporting Code (Phase 8.3 - Comprehensive)
# ============================================================================

# Audio span log tests (circular buffer diagnostic logging)
add_executable(test_audio_span_log
    test_audio_span_log.c
    ../../components/audio_processor/audio_span_log.c
    mocks/fake_log.c
    mocks/fake_esp_err.c)
target_include_directories(test_audio_span_log PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/audio_processor/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/platform_shim)
target_compile_definitions(test_audio_span_log PRIVATE UNIT_TEST)
target_link_libraries(test_audio_span_log unity platform_shim_host)
add_test(NAME test_audio_span_log COMMAND $<TARGET_FILE:test_audio_span_log>)

# Platform shim tests (memory, timing wrappers)
add_executable(test_platform_shim
    test_platform_shim.c
    mocks/fake_log.c)
target_include_directories(test_platform_shim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/platform_shim)
target_compile_definitions(test_platform_shim PRIVATE UNIT_TEST)
target_link_libraries(test_platform_shim unity platform_shim_host)
add_test(NAME test_platform_shim COMMAND $<TARGET_FILE:test_platform_shim>)

# PHASE-5c: boot-time auto-connect logic (dependency contract + condition matrix)
# This test is intentionally lightweight: it tests the individual building blocks
# (nvs_storage_get_last_connected_mac, bt_manager_is_autostart_enabled, bt_connect)
# without bringing in the full bt_manager stack.
add_executable(test_autoconnect test_autoconnect.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_autoconnect PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_autoconnect PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_autoconnect PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_autoconnect PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_autoconnect PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_autoconnect PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_autoconnect PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_autoconnect PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_autoconnect PRIVATE mocks/fake_esp_err.c)
target_sources(test_autoconnect PRIVATE mocks/mock_gap.c)
target_sources(test_autoconnect PRIVATE mocks/mock_a2dp.c)
target_sources(test_autoconnect PRIVATE mocks/mock_avrc.c)
# bt_connection.c is intentionally excluded: test_autoconnect.c provides its own
# bt_connect stub to count calls without the real BT stack.
target_link_libraries(test_autoconnect unity util_safe_host platform_shim_host)
add_test(NAME test_autoconnect COMMAND $<TARGET_FILE:test_autoconnect>)

# TEST-2: cmd_handlers_system.c — STATUS fallback, zero-callbacks, HELP, VERSION
add_executable(test_cmd_handlers_system test_cmd_handlers_system.c
    ../../components/command_interface/commands.c
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
    mocks/mock_uart.c
    mocks/mock_gap.c
    mocks/mock_a2dp.c
    mocks/mock_avrc.c)
target_sources(test_cmd_handlers_system PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_cmd_handlers_system PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_cmd_handlers_system PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_cmd_handlers_system PRIVATE mocks/fake_esp_err.c)
target_sources(test_cmd_handlers_system PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_cmd_handlers_system unity util_safe_host command_interface_host platform_shim_host)
target_compile_definitions(test_cmd_handlers_system PRIVATE UNIT_TEST)
add_test(NAME test_cmd_handlers_system COMMAND $<TARGET_FILE:test_cmd_handlers_system>)

# TEST-1: bt_pairing_store.c — parse_mac, prepare_for_initiation, NULL safety, host returns
add_executable(test_bt_pairing_store test_bt_pairing_store.c
    ../../components/bt_manager/bt_manager.c)
target_sources(test_bt_pairing_store PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_bt_pairing_store PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_bt_pairing_store PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_bt_pairing_store PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_bt_pairing_store PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_bt_pairing_store PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_bt_pairing_store PRIVATE mocks/bt_manager_test_hooks.c)
target_sources(test_bt_pairing_store PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_bt_pairing_store PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_bt_pairing_store PRIVATE mocks/fake_esp_err.c)
target_sources(test_bt_pairing_store PRIVATE mocks/mock_gap.c)
target_sources(test_bt_pairing_store PRIVATE mocks/mock_a2dp.c)
target_sources(test_bt_pairing_store PRIVATE mocks/mock_avrc.c)
target_link_libraries(test_bt_pairing_store unity util_safe_host platform_shim_host)
add_test(NAME test_bt_pairing_store COMMAND $<TARGET_FILE:test_bt_pairing_store>)

# Valgrind integration verification test
# This test intentionally leaks memory to verify --valgrind flag works
add_executable(test_valgrind_check test_valgrind_check.c)
target_link_libraries(test_valgrind_check unity)
add_test(NAME test_valgrind_check COMMAND $<TARGET_FILE:test_valgrind_check>)

# Create a consolidated test runner
add_custom_target(run_tests
    COMMAND test_commands
    COMMAND test_bluetooth
    COMMAND test_nvs_storage_errors
    COMMAND test_nvs_storage
    DEPENDS test_commands test_bluetooth test_nvs_storage_errors test_nvs_storage
)
