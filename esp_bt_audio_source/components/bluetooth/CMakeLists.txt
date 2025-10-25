set(requires
    esp_common
    nvs_flash
    bt)

if(CONFIG_APP_NO_BLOBS)
    list(APPEND requires bt_stack_stub)
endif()

idf_component_register(
    SRCS "bt_source.c"
    INCLUDE_DIRS "include"
    REQUIRES ${requires}
)
