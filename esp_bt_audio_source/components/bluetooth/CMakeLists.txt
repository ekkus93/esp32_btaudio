idf_component_register(
    SRCS "bt_source.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp_common" "nvs_flash" "bt"
)
