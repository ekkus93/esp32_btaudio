# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_bt_audio_source)

# Flash the prebuilt SPIFFS image at the partition offset defined in partitions.csv.
set(SPIFFS_IMAGE_FILE "${CMAKE_SOURCE_DIR}/main/assets/spiffs/spiffs.bin")
if(NOT EXISTS "${SPIFFS_IMAGE_FILE}")
	message(FATAL_ERROR "Expected prebuilt SPIFFS image not found at ${SPIFFS_IMAGE_FILE}")
endif()

# Partition offset from partitions.csv: spiffs at 0x1C0000 size 0x40000.
set(ESPTOOLPY_FLASH_EXTRA_IMAGES "0x1C0000 ${SPIFFS_IMAGE_FILE}" CACHE STRING "Extra images to flash" FORCE)
