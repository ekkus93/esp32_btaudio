idf_component_register(
    SRCS
        "test_manager.c"
        "../../../components/audio_processor/beep_manager.c"
        "../../../components/audio_processor/i2s_manager.c"
        "../../../components/audio_processor/audio_util.c"
        "../../../components/audio_processor/synth_manager.c"
    INCLUDE_DIRS
        "."
        "../../../components/audio_processor/include"
    PRIV_REQUIRES
        unity
        driver
        esp_timer
        esp_driver_i2s
        freertos
        util_safe
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_AUDIO_USE_SYNTH_SOURCE=1)
