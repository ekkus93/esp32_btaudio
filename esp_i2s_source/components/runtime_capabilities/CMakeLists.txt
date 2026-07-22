# runtime_capabilities — FIX3 10.1: boot-time capability publish/read,
# depended on by both main (publisher) and web_ui (reader) without a
# circular requirement on main.
idf_component_register(
    SRCS "runtime_capabilities.c"
    INCLUDE_DIRS "include")
