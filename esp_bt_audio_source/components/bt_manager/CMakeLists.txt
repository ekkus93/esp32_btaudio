set(priv_requires
    nvs_flash
    nvs_storage
    command_interface
    util_safe
    audio_processor)

if(CONFIG_APP_NO_BLOBS)
    list(APPEND priv_requires bt_stack_stub)
endif()

idf_component_register(
    SRCS
        "bt_manager.c"
        "bt_pairing_store.c"
        "bt_scan.c"
        "bt_connection.c"
        "bt_events_gap.c"
        "bt_events_a2dp.c"
        "bt_events_avrc.c"
        "bt_app_core.c"
        "bt_connection_manager.c"
        "bt_streaming_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES bt
    PRIV_REQUIRES ${priv_requires}
)
