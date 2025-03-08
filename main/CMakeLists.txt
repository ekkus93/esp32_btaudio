idf_component_register(SRCS "esp32_btaudio.c" 
                      "radio.c"
                      "spiffs_utils.c"
                      "wifi.c"
                      "ping.c"
                      "bluetooth/bt_app_core.c"
                      "bluetooth/bt_app_conn.c" 
                      "bluetooth/bt_app_av.c"
                      "bluetooth/bt_app_audio.c"
                      "bluetooth/bt_app_global.c"
                    INCLUDE_DIRS "." "../include"
                    REQUIRES bt esp_wifi nvs_flash spiffs
                    PRIV_REQUIRES esp_http_client esp_driver_gpio esp_driver_uart)

# Add size optimization
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)