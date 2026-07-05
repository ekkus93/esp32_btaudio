# bt_link — UART1 command client to the WROOM32 UART2 port (LINK-1).
# bt_link_parser + bt_link_session are pure/host-tested; bt_link.c is the
# device UART task, verified on hardware at LINK-1c.
idf_component_register(SRCS "bt_link_parser.c" "bt_link_session.c" "bt_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver)
