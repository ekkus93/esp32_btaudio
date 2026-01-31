set(main_requires
    bt
    nvs_flash
    nvs_storage
    esp_ringbuf
    driver
    util_safe)

if(CONFIG_APP_NO_BLOBS)
    list(APPEND main_requires bt_stack_stub)
endif()

idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "../components/components/unity/unity/extras/memory/src"
    REQUIRES
        ${main_requires}
    PRIV_REQUIRES
        bt_manager
        command_interface
        audio_processor
)
