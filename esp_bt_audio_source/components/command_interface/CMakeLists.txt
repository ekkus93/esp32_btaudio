set(priv_requires
    # bt_stack_stub listed before bt: its esp_bt.h wrapper must win the
    # header search over the real components/bt one. Always listed (not
    # gated on CONFIG_APP_NO_BLOBS) — see the comment in
    # components/bt_manager/CMakeLists.txt for why a conditional REQUIRES
    # here would always evaluate false regardless of the real Kconfig value.
    bt_stack_stub
    main
    bt
    driver
    esp_app_format
    util_safe
    platform_shim)

set(public_requires
    audio_processor
    bt_manager
    nvs_storage)

idf_component_register(
    SRCS
        "command_interface.c"
        "commands.c"
        "commands_helpers.c"
        "cmd_handlers_system.c"
        "cmd_handlers_audio.c"
        "cmd_handlers_bt.c"
        "cmd_handlers_debug.c"
        "cmd_handlers_hfp.c"
        "uart_audio_frame.c"
        "uart_audio_rx.c"
        "uart_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES ${public_requires}
    PRIV_REQUIRES ${priv_requires}
    PRIV_INCLUDE_DIRS
)
