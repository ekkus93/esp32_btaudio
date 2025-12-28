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
        "audio_queue.c"
        "audio_processor.c"
        "bt_connection_manager.c"
        "bt_source_component.c"
        "bt_streaming_manager.c"
        "main.c"
        "bt_app_core.c"
    INCLUDE_DIRS
        "include"
        "../components/components/unity/unity/extras/memory/src"
    REQUIRES
        ${main_requires}
    PRIV_REQUIRES
        bluetooth
        bt_manager
        command_interface
)
