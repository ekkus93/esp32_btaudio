idf_component_register(
    SRCS
        "test_synth_manager.c"
        "../../../main/synth_manager.c"
    INCLUDE_DIRS
        "."
        "../../../main/include"
    PRIV_REQUIRES
        unity
        freertos
        driver
        esp_driver_i2s
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_AUDIO_USE_SYNTH_SOURCE=1)
