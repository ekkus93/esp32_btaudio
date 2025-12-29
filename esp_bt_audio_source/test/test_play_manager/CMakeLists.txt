cmake_minimum_required(VERSION 3.10)

project(play_manager_tests C)

add_compile_definitions(UNIT_TEST CONFIG_BT_MOCK_TESTING)
add_compile_options(-UESP_PLATFORM)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../host_test/mocks/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/include
)

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

set(TEST_SOURCES
    test_play_manager.c
    shim_play_manager_stubs.c
    ../../main/play_manager.c
    ../../main/audio_queue.c
    ../host_test/mocks/fake_queue.c
    ../host_test/mocks/fake_semphr.c
    ../host_test/mocks/fake_task.c
    ../host_test/mocks/fake_timer.c
    ../host_test/mocks/fake_esp_err.c
    ../host_test/mocks/fake_log.c
    ../host_test/mocks/fake_debug_helpers.c
)

add_executable(test_play_manager ${TEST_SOURCES})
target_link_libraries(test_play_manager unity m)

enable_testing()
add_test(NAME test_play_manager COMMAND $<TARGET_FILE:test_play_manager>)
