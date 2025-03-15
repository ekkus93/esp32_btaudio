set(COMPONENT_ADD_INCLUDEDIRS include)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES audio_sal nvs_flash spi_flash)
set(COMPONENT_PRIV_REQUIRES esp_dispatcher wifi_service esp-adf-libs audio_pipeline audio_sal)

set(COMPONENT_SRCS  ./player_action.c
                    ./wifi_action.c
                    ./nvs_action.c
                    ./partition_action.c)

register_component()
