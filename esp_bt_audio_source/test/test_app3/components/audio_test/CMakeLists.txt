# Register a small test component that reuses the audio component's test source
# so the Unity test is compiled into the test_app3 image.

idf_component_register(
    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/../test_pcm_processing_bridge.c"
    INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/.."
    REQUIRES freertos unity esp_ringbuf audio_processor
)
