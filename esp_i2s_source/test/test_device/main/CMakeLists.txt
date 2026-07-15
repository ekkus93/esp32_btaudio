# Device test component - main entry point for esp_i2s_source device tests
idf_component_register(
    SRCS
        "test_main.c"
        "test_device.c"
    INCLUDE_DIRS
        "."
        "include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/signal_gen/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/tone/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../components/i2s_out/include"
    REQUIRES
        unity
        test_utils
        nvs_flash
        driver
)
