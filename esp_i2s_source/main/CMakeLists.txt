idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_psram
             i2s_out signal_gen bt_link wifi_mgr web_ui radio
    # DBG diagnostics only (PCNT freq meter): drop with the DBG blocks in main.c
    PRIV_REQUIRES esp_timer esp_driver_pcnt)
