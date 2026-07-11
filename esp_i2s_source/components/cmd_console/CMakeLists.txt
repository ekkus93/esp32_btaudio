# console — USB-Serial-JTAG line reader + command dispatch (WIFI-1c).
idf_component_register(
    SRCS "console.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_usb_serial_jtag wifi_mgr)
