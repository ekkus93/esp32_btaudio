idf_component_register(
    SRCS "test_app_main.c" "bt_source_stub.c"
    INCLUDE_DIRS "." "../../main" "../../components/command_interface/include"
    REQUIRES command_interface unity
)
