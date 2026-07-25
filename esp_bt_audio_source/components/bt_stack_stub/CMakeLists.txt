# CONFIG_APP_NO_BLOBS is unresolved during CMake's early requirements-expansion
# pass (it runs before sdkconfig.cmake is generated/included — see
# idf_build_process() in tools/cmake/build.cmake), so a component that skips
# idf_component_register() entirely based on it can never be discovered by
# components that conditionally REQUIRES it, regardless of the real Kconfig
# value. Match the pattern components/bt/CMakeLists.txt uses: always
# register, and condition only the contents on the config.
# Dependents (bt_manager, command_interface, main) now REQUIRES this
# component unconditionally too — see comments there — so keep it a true
# no-op (no sources, no include dir, no deps) when the real Bluetooth stack
# is in use, rather than shadowing the real esp_bt.h from components/bt.
if(CONFIG_APP_NO_BLOBS)
    set(srcs "bt_stub.c")
    # bt_stub.c AND bt_manager.c (a dependent) both include osi/allocator.h.
    # components/bt/CMakeLists.txt only adds common/osi/include to ITS OWN
    # INCLUDE_DIRS inside its `if(CONFIG_BT_ENABLED)` block, which
    # APP_NO_BLOBS forces off (unlike the Bluedroid API headers, this one
    # isn't also covered by the IDF_DOC_BUILD escape hatch
    # test_bluetooth/CMakeLists.txt sets — see its comment) — so reach it
    # directly (public, since dependents need it too) rather than patch
    # vendored IDF.
    set(include_dirs "include" "$ENV{IDF_PATH}/components/bt/common/osi/include")
else()
    set(srcs "")
    set(include_dirs "")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES esp_common
    # Unconditional for the same reason as srcs/include_dirs above being the
    # only Kconfig-conditional parts here: `bt` always registers regardless
    # of config (see components/bt/CMakeLists.txt), so depending on it
    # unconditionally is harmless, and a config-gated PRIV_REQUIRES would
    # hit the same early-expansion problem this whole file is working around.
    PRIV_REQUIRES bt
)
