set(priv_requires
    main
    nvs_flash
    command_interface
    util_safe)

if(CONFIG_APP_NO_BLOBS)
    list(APPEND priv_requires bt_stack_stub)
endif()

idf_component_register(
    SRCS "bt_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES bt
    PRIV_REQUIRES ${priv_requires}
)
