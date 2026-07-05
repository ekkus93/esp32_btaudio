# bt_link — UART1 command client to the WROOM32 UART2 port (LINK-1).
# bt_link_parser is pure/host-tested; the UART task lands in LINK-1b.
idf_component_register(SRCS "bt_link_parser.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver)
