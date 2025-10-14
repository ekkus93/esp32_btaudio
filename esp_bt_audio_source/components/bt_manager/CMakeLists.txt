idf_component_register(
    SRCS "bt_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES main nvs_flash bt
)
