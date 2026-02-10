idf_component_register(SRCS "nvs_storage.c"
                    INCLUDE_DIRS "."
                    REQUIRES platform_shim util_safe
                    PRIV_REQUIRES nvs_flash)
