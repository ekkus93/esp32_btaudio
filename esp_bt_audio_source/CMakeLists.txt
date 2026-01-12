# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_bt_audio_source)

# Build and flash the SPIFFS partition image from the local spiffs/ directory
# (spiffs partition label and offset/size are defined in partitions.csv).
spiffs_create_partition_image(spiffs "${CMAKE_SOURCE_DIR}/spiffs" FLASH_IN_PROJECT)
