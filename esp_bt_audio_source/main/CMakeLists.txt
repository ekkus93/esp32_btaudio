idf_component_register(SRCS "audio_processor.c" "bt_connection_manager.c" "bt_source_component.c" "bt_streaming_manager.c" "main.c" "bt_app_core.c"
                    INCLUDE_DIRS "include" "../components/components/unity/unity/extras/memory/src"
                    REQUIRES bt nvs_flash nvs_storage esp_ringbuf driver)
