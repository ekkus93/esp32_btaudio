idf_component_register(
    SRCS "test_command_interface.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES audio_processor main)

# Ensure test hooks in audio_processor.h are visible for this component.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_BT_MOCK_TESTING)
