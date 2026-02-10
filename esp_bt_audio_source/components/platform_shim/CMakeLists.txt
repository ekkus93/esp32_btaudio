# Platform abstraction shim component
#
# Provides platform-independent interfaces for:
# - Synchronization primitives (semaphores, mutexes, etc.)
# - Timing/delay functions
# - Memory allocation with caps
# 
# CODE_REVIEW8 P2.2 - Platform Shim Layer
#
# Build logic:
# - IDF_TARGET defined: Compile ESP32 implementation (FreeRTOS-based)
# - IDF_TARGET not defined: Compile host implementation (mock/test)

if(IDF_TARGET)
    # ESP32 build: Use FreeRTOS and ESP-IDF based implementation
    idf_component_register(
        SRCS 
            "platform_sync_esp32.c"
            "platform_timing_esp32.c"
            "platform_memory_esp32.c"
            "platform_storage_esp32.c"
        INCLUDE_DIRS 
            "."
        PRIV_REQUIRES 
            freertos
            esp_timer
            nvs_flash
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PLATFORM_SHIM_ESP32=1)
else()
    # Host build: Use mock implementation for testing
    idf_component_register(
        SRCS
            "platform_sync_host.c"
            "platform_timing_host.c"
            "platform_memory_host.c"
            "platform_storage_host.c"
        INCLUDE_DIRS 
            "."
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PLATFORM_SHIM_HOST=1)
endif()
