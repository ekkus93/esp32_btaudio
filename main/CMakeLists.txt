idf_component_register(SRCS "esp32_btaudio.c" 
                      "radio.c"
                      "spiffs_utils.c"
                      "wifi.c"
                      "ping_utils.c"
                      "bluetooth/bt_app_a2dp.c"
                      "bluetooth/bt_app_audio.c"
                      "bluetooth/bt_app_av.c"
                      "bluetooth/bt_app_conn.c" 
                      "bluetooth/bt_app_core.c"
                      "bluetooth/bt_app_discovery.c"
                      "bluetooth/bt_app_gap.c"
                      "bluetooth/bt_app_global.c"
                      "bluetooth/bt_app_init.c"
                      "bluetooth/bt_app_monitor.c"
                      "bluetooth/bt_app_name.c"
                      "bluetooth/bt_app_utils.c"
                    INCLUDE_DIRS "." "../include"
                    REQUIRES bt esp_wifi nvs_flash spiffs audio_pipeline audio_stream
                    PRIV_REQUIRES esp_http_client esp_driver_gpio esp_driver_uart audio_pipeline esp_peripherals esp-adf-libs bluetooth_service
                    freertos)

# Add size optimization
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)