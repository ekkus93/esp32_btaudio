idf_component_register(
    SRCS
        "test_main.c"
        "test_app_main.c"
        "unity_config.c"
        "audio_test_main.c"
        "audio_pipeline_test.c"
        # "audio_processor_test.c"  # Removed: old WAV playback tests (WAV/SPIFFS removed Phase 5)
        "audio_i2s.c"
        "audio_test_helpers.c"
        "bt_manager_stub.c"
        "i2s_audio.c"
        "i2s_audio_test.c"
        "i2s_channel_test.c"
        "i2s_test.c"
        "pcm_format_test.c"
        "i2s_test_stubs.c"
        "pcm_processing.c"
        "test_psram_integration.c"
        "test_psram_fragmentation.c"
        "test_psram_audio_stress.c"
        "test_cooperative_shutdown.c"
        "../../../components/audio_processor/audio_processor.c"
        "../../../components/audio_processor/audio_processor_state.c"
        "../../../components/audio_processor/audio_ringbuffer.c"
        "../../../components/audio_processor/audio_util.c"
        "../../../components/audio_processor/audio_resampler_stream.c"
        "../../../components/audio_processor/i2s_manager.c"
        "../../../components/audio_processor/i2s_frame_extract.c"
        "../../../components/audio_processor/beep_manager.c"
        "../../../components/audio_processor/audio_processor_beep.c"
        # "../../../components/audio_processor/audio_processor_wav.c"  # Removed: Phase 5 WAV cleanup
        "../../../components/audio_processor/audio_processor_read.c"
        "../../../components/audio_processor/audio_processor_diag.c"
        "../../../components/audio_processor/synth_manager.c"
    INCLUDE_DIRS
        "."
        "include"
        "../../../components/audio_processor/include"
    REQUIRES
        # driver component omitted for tests so we can provide test-only stubs
        nvs_storage
        esp_ringbuf
        esp_timer
        spiffs
        unity
        test_utils
    PRIV_REQUIRES
        test_command_interface
        esp_psram
)

# Provide access to driver headers for test-only stubs without linking against
# the full driver component (the stubs replace the runtime implementation).
idf_component_get_property(driver_dir driver COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${driver_dir}/include")
idf_component_get_property(esp_driver_i2s_dir esp_driver_i2s COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${esp_driver_i2s_dir}/include")
idf_component_get_property(esp_driver_gpio_dir esp_driver_gpio COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${esp_driver_gpio_dir}/include")

# Audio-focused tests still rely on the mock-enabled build to avoid accessing
# physical peripherals on the DUT. Reuse the CONFIG_BT_MOCK_TESTING define so
# existing code paths remain enabled.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)

if(DEFINED UNITY_AUDIO_TEST_GROUP_OVERRIDE AND NOT "${UNITY_AUDIO_TEST_GROUP_OVERRIDE}" STREQUAL "")
    # Ensure the main component test sources are compiled with the override so
    # the runtime banner and group-shortcut code path are enabled.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE UNITY_AUDIO_TEST_GROUP_OVERRIDE="${UNITY_AUDIO_TEST_GROUP_OVERRIDE}")
endif()

# Warn at build time if the PSRAM integration test gate is enabled but SPIRAM
# is not enabled in the project's sdkconfig. This helps avoid confusion where
# tests are compiled-in but will be skipped at runtime.
if(DEFINED CONFIG_TEST_APP_AUDIO_PSRAM_TESTS)
    if(CONFIG_TEST_APP_AUDIO_PSRAM_TESTS)
        if(NOT DEFINED CONFIG_SPIRAM OR NOT CONFIG_SPIRAM)
            message(FATAL_ERROR "CONFIG_TEST_APP_AUDIO_PSRAM_TESTS is enabled but CONFIG_SPIRAM is not set. Enable CONFIG_SPIRAM or disable TEST_APP_AUDIO_PSRAM_TESTS to proceed.")
        endif()
    endif()
endif()
