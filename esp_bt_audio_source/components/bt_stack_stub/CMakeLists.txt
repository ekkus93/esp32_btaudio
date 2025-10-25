if(NOT CONFIG_APP_NO_BLOBS)
    return()
endif()

idf_component_register(
    SRCS "bt_stub.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES bt
)
