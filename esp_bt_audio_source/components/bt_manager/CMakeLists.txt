set(priv_requires
    main
    nvs_flash
    bt
    bluetooth
    command_interface)

if(CONFIG_APP_NO_BLOBS)
    list(APPEND priv_requires bt_stack_stub)
endif()

idf_component_register(
    SRCS "bt_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
)
