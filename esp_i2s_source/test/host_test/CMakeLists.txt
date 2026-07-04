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

# --- Sanity: proves the harness builds and CTest runs (INFRA-1c) ---
add_executable(test_sanity test_sanity.c)
target_link_libraries(test_sanity unity)
add_test(NAME test_sanity COMMAND test_sanity)

# SIG-1a: signal_gen producers (pure sample math)
add_executable(test_signal_gen test_signal_gen.c ${SIGNAL_GEN_DIR}/signal_gen.c)
target_include_directories(test_signal_gen PRIVATE ${SIGNAL_GEN_DIR}/include)
target_link_libraries(test_signal_gen unity m)
add_test(NAME test_signal_gen COMMAND test_signal_gen)

# SIG-1b: pcm_ring SPSC ring
add_executable(test_pcm_ring test_pcm_ring.c ${I2S_OUT_DIR}/pcm_ring.c)
target_include_directories(test_pcm_ring PRIVATE ${I2S_OUT_DIR}/include)
target_link_libraries(test_pcm_ring unity)
add_test(NAME test_pcm_ring COMMAND test_pcm_ring)

# Further suites appended by LINK-1a (test_bt_link_parser), RADIO-1a
# (test_playlist_icy), RADIO-2b (test_resampler), etc.
