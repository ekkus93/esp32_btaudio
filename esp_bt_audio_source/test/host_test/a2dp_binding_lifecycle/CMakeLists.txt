cmake_minimum_required(VERSION 3.16)
project(a2dp_binding_lifecycle_sanitizers C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(HOST_TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PROJECT_DIR "${HOST_TEST_DIR}/../.." ABSOLUTE)
get_filename_component(REPO_DIR "${PROJECT_DIR}/.." ABSOLUTE)
set(UNITY_DIR "${REPO_DIR}/esp_i2s_source/test/third_party/unity/src")

if(NOT EXISTS "${UNITY_DIR}/unity.c" OR NOT EXISTS "${UNITY_DIR}/unity.h")
    message(FATAL_ERROR "Vendored Unity sources not found at ${UNITY_DIR}")
endif()

add_compile_definitions(UNIT_TEST ESP_PLATFORM_UNDEFINED)
add_compile_options(
    -UESP_PLATFORM
    -fsanitize=address,undefined
    -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer
    -g
)
add_link_options(
    -fsanitize=address,undefined
    -fno-sanitize-recover=undefined
)

include_directories(
    "${UNITY_DIR}"
    "${HOST_TEST_DIR}/mocks/include"
    "${HOST_TEST_DIR}/mocks"
    "${HOST_TEST_DIR}/include"
    "${PROJECT_DIR}/test/component/bt_mock/include"
    "${PROJECT_DIR}/test/component/test_common/include"
    "${PROJECT_DIR}/components/command_interface/include"
    "${PROJECT_DIR}/components/bt_manager/include"
    "${PROJECT_DIR}/components/util_safe/include"
    "${PROJECT_DIR}/components/nvs_storage"
    "${HOST_TEST_DIR}/esp_idf_stubs/bt/common/osi/include"
    "${PROJECT_DIR}/components/audio_processor/include"
    "${PROJECT_DIR}/components/platform_shim"
)

add_library(unity_local STATIC "${UNITY_DIR}/unity.c")

add_library(util_safe_host OBJECT
    "${PROJECT_DIR}/components/util_safe/util_safe.c"
)

add_library(platform_shim_host OBJECT
    "${PROJECT_DIR}/components/platform_shim/platform_sync_host.c"
    "${PROJECT_DIR}/components/platform_shim/platform_timing_host.c"
    "${PROJECT_DIR}/components/platform_shim/platform_memory_host.c"
    "${PROJECT_DIR}/components/platform_shim/platform_storage_host.c"
)

set(BT_MANAGER_PRODUCTION_SOURCES
    "${PROJECT_DIR}/components/bt_manager/bt_manager.c"
    "${PROJECT_DIR}/components/bt_manager/bt_manager_ops.c"
    "${PROJECT_DIR}/components/bt_manager/bt_manager_mocks.c"
    "${PROJECT_DIR}/components/bt_manager/bt_pairing_store.c"
    "${PROJECT_DIR}/components/bt_manager/bt_scan.c"
    "${PROJECT_DIR}/components/bt_manager/bt_connection.c"
    "${PROJECT_DIR}/components/bt_manager/bt_events_gap.c"
    "${PROJECT_DIR}/components/bt_manager/bt_events_a2dp.c"
    "${PROJECT_DIR}/components/bt_manager/bt_events_avrc.c"
)

set(BT_MANAGER_COMMON_MOCK_SOURCES
    "${HOST_TEST_DIR}/mocks/mock_a2dp.c"
    "${HOST_TEST_DIR}/mocks/mock_avrc.c"
    "${HOST_TEST_DIR}/mocks/mock_gap.c"
    "${HOST_TEST_DIR}/mocks/nvs_storage_mock.c"
    "${HOST_TEST_DIR}/mocks/mock_audio_and_btstate.c"
    "${HOST_TEST_DIR}/mocks/bt_manager_test_hooks.c"
    "${HOST_TEST_DIR}/mocks/fake_esp_err.c"
)

add_executable(test_bt_ctx_lock
    "${HOST_TEST_DIR}/test_bt_ctx_lock.c"
    ${BT_MANAGER_PRODUCTION_SOURCES}
    ${BT_MANAGER_COMMON_MOCK_SOURCES}
)
target_link_libraries(test_bt_ctx_lock PRIVATE
    unity_local
    util_safe_host
    platform_shim_host
    pthread
)

add_executable(test_bt_manager_connection_pairing_events
    "${HOST_TEST_DIR}/test_bt_manager_connection_pairing_events.c"
    ${BT_MANAGER_PRODUCTION_SOURCES}
    ${BT_MANAGER_COMMON_MOCK_SOURCES}
    "${HOST_TEST_DIR}/mocks/fake_log.c"
)
target_compile_definitions(test_bt_manager_connection_pairing_events PRIVATE
    CONFIG_BT_MOCK_TESTING=1
)
target_link_libraries(test_bt_manager_connection_pairing_events PRIVATE
    unity_local
    util_safe_host
    platform_shim_host
    pthread
)
