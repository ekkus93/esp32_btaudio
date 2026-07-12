set(priv_requires
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

if(CONFIG_APP_NO_BLOBS)
    list(APPEND priv_requires bt_stack_stub)
endif()

idf_component_register(
    SRCS
        "command_interface.c"
        "commands.c"
        "commands_helpers.c"
        "cmd_handlers_system.c"
        "cmd_handlers_audio.c"
        "cmd_handlers_bt.c"
        "cmd_handlers_debug.c"
        "uart_audio_frame.c"
        "uart_audio_rx.c"
        "uart_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES ${public_requires}
    PRIV_REQUIRES ${priv_requires}
    PRIV_INCLUDE_DIRS
)

# Prefer project-local path when building as a sub-project (e.g. test_app).
if(EXISTS "${PROJECT_DIR}/../components/bt_manager/include")
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${PROJECT_DIR}/../components/bt_manager/include")
else()
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/components/bt_manager/include")
endif()

# bt_core include lives next to this component when used from the project root;
# also support the relative path when building from a subproject.
if(EXISTS "${PROJECT_DIR}/../components/bt_core/include")
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${PROJECT_DIR}/../components/bt_core/include")
else()
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../components/bt_core/include")
endif()

