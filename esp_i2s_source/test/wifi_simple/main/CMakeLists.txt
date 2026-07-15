idf_component_register(
    SRCS "simple_wifi_test.c"
    PRIV_REQUIRES
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
    INCLUDE_DIRS "."
)