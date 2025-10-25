set(priv_requires
    main
    bt_manager
    bt
    driver)

if(CONFIG_APP_NO_BLOBS)
    list(APPEND priv_requires bt_stack_stub)
endif()

idf_component_register(
    SRCS "command_interface.c" "commands.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
    PRIV_INCLUDE_DIRS
)

# Prefer project-local path when building as a sub-project (e.g. test_app).
if(EXISTS "${PROJECT_DIR}/../components/bt_manager/include")
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${PROJECT_DIR}/../components/bt_manager/include")
else()
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/components/bt_manager/include")
endif()

# bt_core include lives next to this component when used from the project root;
# also support the relative path when building from a subproject.
if(EXISTS "${PROJECT_DIR}/../components/bt_core/include")
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${PROJECT_DIR}/../components/bt_core/include")
else()
    list(APPEND COMPONENT_PRIV_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../components/bt_core/include")
endif()

