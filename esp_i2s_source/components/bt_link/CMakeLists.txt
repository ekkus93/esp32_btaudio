# bt_link — UART1 command client to the WROOM32 UART2 port (LINK-1).
# bt_link_parser + bt_link_session are pure/host-tested; the UART task
# (bt_link.c) lands at LINK-1c with the hardware.
idf_component_register(SRCS "bt_link_parser.c" "bt_link_session.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver)
