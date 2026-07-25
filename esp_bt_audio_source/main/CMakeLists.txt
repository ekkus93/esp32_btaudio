set(main_requires
    # bt_stack_stub listed before bt: its esp_bt.h wrapper must win the
    # header search over the real components/bt one. Always listed (not
    # gated on CONFIG_APP_NO_BLOBS) — see the comment in
    # components/bt_manager/CMakeLists.txt for why a conditional REQUIRES
    # here would always evaluate false regardless of the real Kconfig value.
    bt_stack_stub
    bt
    nvs_flash
    nvs_storage
    esp_ringbuf
    driver
    util_safe)

idf_component_register(
    SRCS
        "main.c"
    REQUIRES
        ${main_requires}
    PRIV_REQUIRES
        bt_manager
        command_interface
        audio_processor
)
