set(priv_requires
    nvs_flash
    nvs_storage
    command_interface
    util_safe
    audio_processor
    platform_shim
    esp_timer)

idf_component_register(
    SRCS
        "bt_manager.c"
        "bt_manager_ops.c"
        "bt_manager_mocks.c"
        "bt_pairing_store.c"
        "bt_scan.c"
        "bt_connection.c"
        "bt_events_gap.c"
        "bt_events_a2dp.c"
        "bt_events_avrc.c"
        "bt_app_core.c"
        "bt_connection_manager.c"
        "bt_streaming_manager.c"
        "bt_duplex_state_core.c"
        "bt_duplex_state_profile.c"
        "bt_duplex_state_transitions.c"
        "bt_duplex_state_strings.c"
        "bt_duplex_state_events.c"
        "bt_hfp_ag_lifecycle.c"
        "bt_hfp_ag_events.c"
        "bt_hfp_connection.c"
        "bt_hfp_audio.c"
    INCLUDE_DIRS "include"
    # bt_stack_stub listed BEFORE bt: when CONFIG_APP_NO_BLOBS=y its
    # esp_bt.h wrapper (which #include_next-s the real one and patches the
    # BT_CONTROLLER_INIT_CONFIG_DEFAULT() static_assert poison) must win the
    # header search over the real components/bt/include/**/esp_bt.h — CMake
    # searches -I dirs in REQUIRES list order. Always listed (not gated on
    # CONFIG_APP_NO_BLOBS): that symbol is unresolved during CMake's early
    # requirements-expansion pass (runs before sdkconfig.cmake is generated
    # — see idf_build_process() in tools/cmake/build.cmake), so a
    # conditional REQUIRES here would always evaluate false regardless of
    # the real Kconfig value. bt_stack_stub itself is a no-op (no sources,
    # no include dir) unless APP_NO_BLOBS=y, matching the pattern
    # components/bt/CMakeLists.txt uses for its own always-unconditional
    # PRIV_REQUIRES.
    REQUIRES bt_stack_stub bt
    PRIV_REQUIRES ${priv_requires}
)
