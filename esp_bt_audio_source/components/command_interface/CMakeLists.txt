idf_component_register(
    SRCS "commands.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES main
    PRIV_INCLUDE_DIRS 
        "${CMAKE_SOURCE_DIR}/components/bt_manager/include"
        "${CMAKE_SOURCE_DIR}/../components/bt_core/include"
)
