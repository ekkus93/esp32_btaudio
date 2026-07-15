cmake_minimum_required(VERSION 3.10)

project(esp_i2s_source_tests C)

# Host build: pure logic compiled off-device (SPEC §7). Mirrors the
# esp_bt_audio_source host_test harness pattern.
add_compile_definitions(UNIT_TEST)

# Code coverage (gcov/lcov) — used by DOC-1b's run_all_tests-style entry.
option(ENABLE_COVERAGE "Enable code coverage reporting with gcov/lcov" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()

# AddressSanitizer
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Unity test framework
# Prefer a local Unity install (e.g. install to /usr/local/share/cmake/unity/
# or set CMAKE_PREFIX_PATH). Falls back to FetchContent from GitHub only if
# find_package(unity) fails, so offline builds work without requiring internet.
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

enable_testing()

# --- Component source roots (added per task as host-testable code lands) ---
set(SIGNAL_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/signal_gen)
set(I2S_OUT_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/i2s_out)
set(BT_LINK_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/bt_link)
set(RADIO_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/../../components/radio)
set(WIFI_MGR_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../../components/wifi_mgr)
set(CTRL_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../../components/ctrl)

# --- Sanity: proves the harness builds and CTest runs (INFRA-1c) ---
add_executable(test_sanity test_sanity.c)
target_link_libraries(test_sanity unity)
add_test(NAME test_sanity COMMAND test_sanity)

# SIG-1a: signal_gen producers (pure sample math)
add_executable(test_signal_gen test_signal_gen.c ${SIGNAL_GEN_DIR}/signal_gen.c)
target_include_directories(test_signal_gen PRIVATE ${SIGNAL_GEN_DIR}/include)
target_link_libraries(test_signal_gen unity m)
add_test(NAME test_signal_gen COMMAND test_signal_gen)

# WEB-1d: tone controllable test-tone source
add_executable(test_tone test_tone.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/tone/tone.c
    ${SIGNAL_GEN_DIR}/signal_gen.c)
target_include_directories(test_tone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/tone/include
    ${SIGNAL_GEN_DIR}/include)
target_link_libraries(test_tone unity m)
add_test(NAME test_tone COMMAND test_tone)

# SIG-1b: pcm_ring SPSC ring
add_executable(test_pcm_ring test_pcm_ring.c ${I2S_OUT_DIR}/pcm_ring.c)
target_include_directories(test_pcm_ring PRIVATE ${I2S_OUT_DIR}/include)
target_link_libraries(test_pcm_ring unity)
add_test(NAME test_pcm_ring COMMAND test_pcm_ring)

# SIG-1b: i2s_out pure pump (drain / zero-fill / stats)
add_executable(test_i2s_out_pump test_i2s_out_pump.c
    ${I2S_OUT_DIR}/i2s_out_pump.c ${I2S_OUT_DIR}/pcm_ring.c)
target_include_directories(test_i2s_out_pump PRIVATE ${I2S_OUT_DIR}/include)
target_link_libraries(test_i2s_out_pump unity)
add_test(NAME test_i2s_out_pump COMMAND test_i2s_out_pump)

# S3 pre-I2S volume: pure gain scaler
add_executable(test_i2s_out_gain test_i2s_out_gain.c ${I2S_OUT_DIR}/i2s_out_gain.c)
target_include_directories(test_i2s_out_gain PRIVATE ${I2S_OUT_DIR}/include)
target_link_libraries(test_i2s_out_gain unity)
add_test(NAME test_i2s_out_gain COMMAND test_i2s_out_gain)

# LINK-1a: bt_link line/response parser
add_executable(test_bt_link_parser test_bt_link_parser.c ${BT_LINK_DIR}/bt_link_parser.c)
target_include_directories(test_bt_link_parser PRIVATE ${BT_LINK_DIR}/include)
target_link_libraries(test_bt_link_parser unity)
add_test(NAME test_bt_link_parser COMMAND test_bt_link_parser)

# LINK-1b: bt_link session state machine
add_executable(test_bt_link_session test_bt_link_session.c
    ${BT_LINK_DIR}/bt_link_session.c ${BT_LINK_DIR}/bt_link_parser.c)
target_include_directories(test_bt_link_session PRIVATE ${BT_LINK_DIR}/include)
target_link_libraries(test_bt_link_session unity)
add_test(NAME test_bt_link_session COMMAND test_bt_link_session)

# WIFI-1a: wifi_sm STA/AP fallback state machine
add_executable(test_wifi_sm test_wifi_sm.c ${WIFI_MGR_DIR}/wifi_sm.c)
target_include_directories(test_wifi_sm PRIVATE ${WIFI_MGR_DIR}/include)
target_link_libraries(test_wifi_sm unity)
add_test(NAME test_wifi_sm COMMAND test_wifi_sm)

# RADIO-1a: playlist (.pls/.m3u) + ICY metadata parsers
add_executable(test_radio_parse test_radio_parse.c ${RADIO_DIR}/radio_parse.c)
target_include_directories(test_radio_parse PRIVATE ${RADIO_DIR}/include)
target_link_libraries(test_radio_parse unity)
add_test(NAME test_radio_parse COMMAND test_radio_parse)

# RADIO-1c: station store CRUD
add_executable(test_station_store test_station_store.c ${RADIO_DIR}/station_store.c)
target_include_directories(test_station_store PRIVATE ${RADIO_DIR}/include)
target_link_libraries(test_station_store unity)
add_test(NAME test_station_store COMMAND test_station_store)

# RADIO-2b: resampler math
add_executable(test_radio_resampler test_radio_resampler.c ${RADIO_DIR}/radio_resampler.c)
target_include_directories(test_radio_resampler PRIVATE ${RADIO_DIR}/include)
target_link_libraries(test_radio_resampler unity)
add_test(NAME test_radio_resampler COMMAND test_radio_resampler)

# CTRL-1a: ctrl_cfg MAC validator (NVS glue is ESP_PLATFORM-guarded out on host)
add_executable(test_ctrl_cfg test_ctrl_cfg.c ${CTRL_DIR}/ctrl_cfg.c)
target_include_directories(test_ctrl_cfg PRIVATE ${CTRL_DIR}/include)
target_link_libraries(test_ctrl_cfg unity)
add_test(NAME test_ctrl_cfg COMMAND test_ctrl_cfg)

# CTRL-1b: boot orchestrator state machine (pure)
add_executable(test_ctrl_sm test_ctrl_sm.c ${CTRL_DIR}/ctrl_sm.c)
target_include_directories(test_ctrl_sm PRIVATE ${CTRL_DIR}/include)
target_link_libraries(test_ctrl_sm unity)
add_test(NAME test_ctrl_sm COMMAND test_ctrl_sm)

# RH-S3-03: radio lifecycle — task creation failure paths
add_executable(test_radio_lifecycle test_radio_lifecycle.c
    ${RADIO_DIR}/radio.c
    ${RADIO_DIR}/radio_parse.c
    ${RADIO_DIR}/radio_resampler.c
    mocks/freertos_event_group.c
    mocks/fake_task.c
    mocks/fake_semphr.c
    mocks/fake_queue.c
    mocks/fake_esp_err.c)
target_include_directories(test_radio_lifecycle PRIVATE
    ${RADIO_DIR}/include
    ${I2S_OUT_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include)
target_compile_definitions(test_radio_lifecycle PRIVATE UNIT_TEST)
target_link_libraries(test_radio_lifecycle unity)
add_test(NAME test_radio_lifecycle COMMAND test_radio_lifecycle)

# RH-S3-06: pack_s16_msb — signed sample packing without UB
add_executable(test_pack_s16_msb test_pack_s16_msb.c)
target_link_libraries(test_pack_s16_msb unity)
add_test(NAME test_pack_s16_msb COMMAND test_pack_s16_msb)

# RH-S3-01: bt_link request lifecycle — ownership safety
add_executable(test_bt_link_lifecycle test_bt_link_lifecycle.c
    ${BT_LINK_DIR}/bt_link.c
    ${BT_LINK_DIR}/bt_link_session.c
    ${BT_LINK_DIR}/bt_link_parser.c
    mocks/freertos_event_group.c
    mocks/fake_task.c
    mocks/fake_semphr.c
    mocks/fake_esp_err.c
    mocks/fake_uart.c
    mocks/fake_queue.c)
target_include_directories(test_bt_link_lifecycle PRIVATE
    ${BT_LINK_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include)
target_compile_definitions(test_bt_link_lifecycle PRIVATE UNIT_TEST ESP_PLATFORM)
target_link_libraries(test_bt_link_lifecycle unity)
add_test(NAME test_bt_link_lifecycle COMMAND test_bt_link_lifecycle)

# RH-S3-10: ctrl_init() startup ordering — mutex before web UI
add_executable(test_ctrl_init test_ctrl_init.c
    ${CTRL_DIR}/ctrl.c
    ${CTRL_DIR}/ctrl_cfg.c
    ${CTRL_DIR}/ctrl_sm.c
    mocks/fake_semphr.c
    mocks/fake_task.c
    mocks/fake_esp_err.c
    mocks/stubs/ctrl_cfg_host.c
    mocks/stubs/ctrl_device_stubs.c)
target_include_directories(test_ctrl_init PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/stubs  # stubs take precedence
    ${CTRL_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/include)
target_compile_definitions(test_ctrl_init PRIVATE UNIT_TEST)
target_link_libraries(test_ctrl_init unity)
add_test(NAME test_ctrl_init COMMAND test_ctrl_init)
