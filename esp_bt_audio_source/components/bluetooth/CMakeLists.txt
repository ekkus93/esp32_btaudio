set(COMPONENT_SRCS 
    "bt_interface.c"
)

set(COMPONENT_ADD_INCLUDEDIRS 
    "include"
)

set(COMPONENT_REQUIRES 
    nvs_flash
    bt
)

register_component()
