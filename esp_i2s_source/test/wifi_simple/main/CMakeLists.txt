cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "simple_wifi_test.c"
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif
    INCLUDE_DIRS "."
)