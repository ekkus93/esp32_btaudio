idf_component_register(
    SRCS "main.c" "clock_diag.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_psram
             i2s_out tone bt_link wifi_mgr cmd_console web_ui radio ctrl
             runtime_capabilities
    # DBG diagnostics only (PCNT freq meter): drop with the DBG blocks in main.c
    PRIV_REQUIRES esp_timer esp_driver_pcnt)
