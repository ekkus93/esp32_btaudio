#!/usr/bin/env python3
"""Apply the exact host-test CMake stabilization edit, then exit.

This script is deliberately one-shot. The workflow that invokes it removes both
this script and itself in the same commit as the real CMake change.
"""

from pathlib import Path

CMAKE_PATH = Path("esp_bt_audio_source/test/host_test/CMakeLists.txt")

RETIRED_ADAPTER_BLOCK = """## Adapter runner: compile test_bluetooth pairing test and the adapter code so we can
# exercise the host-mode adapter instrumentation (`test_pairing_adapters.c`) as
# a native host binary. This helps capture the `TEST_ADAPT` logs and in-memory
# event queue behavior without flashing a device.
set(TEST_PAIRING_ADAPTER_SRC ../../test_bluetooth/main/test_pairing_adapters.c)

add_executable(test_pairing_adapter_runner
    ../../test_bluetooth/main/test_pairing_commands.c
    ${TEST_PAIRING_ADAPTER_SRC}
    ../../components/command_interface/commands.c
    mocks/mock_gap.c
    mocks/mock_uart.c
)
target_sources(test_pairing_adapter_runner PRIVATE test_pairing_adapter_main.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/nvs_storage_mock.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_manager.c ../../components/bt_manager/bt_manager_ops.c ../../components/bt_manager/bt_manager_mocks.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_pairing_store.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_scan.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_connection.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_events_gap.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_events_a2dp.c)
target_sources(test_pairing_adapter_runner PRIVATE ../../components/bt_manager/bt_events_avrc.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/bt_manager_test_hooks.c)
target_link_libraries(test_pairing_adapter_runner unity util_safe_host command_interface_host platform_shim_host)
add_test(NAME test_pairing_adapter_runner COMMAND $<TARGET_FILE:test_pairing_adapter_runner>)
# Enable SEQ/TS emission for this runner so we can validate ordering/timestamps
target_compile_definitions(test_pairing_adapter_runner PRIVATE TEST_INCLUDE_SEQ)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_audio_and_btstate.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/audio_processor_host_stub.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/fake_esp_err.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_gap.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_a2dp.c)
target_sources(test_pairing_adapter_runner PRIVATE mocks/mock_avrc.c)

"""

INSERTION_MARKER = """target_compile_definitions(test_bt_ctx_lock PRIVATE UNIT_TEST)
add_test(NAME test_bt_ctx_lock COMMAND $<TARGET_FILE:test_bt_ctx_lock>)
"""

STABILIZATION_TARGETS = """

# A2DP binding lifecycle stabilization tests. These targets compile the real
# manager and A2DP event sources; no inline no-op replacement is permitted.
set(A2DP_STABILIZATION_MANAGER_SOURCES
    ../../components/bt_manager/bt_manager.c
    ../../components/bt_manager/bt_manager_ops.c
    ../../components/bt_manager/bt_manager_mocks.c
    ../../components/bt_manager/bt_pairing_store.c
    ../../components/bt_manager/bt_scan.c
    ../../components/bt_manager/bt_connection.c
    ../../components/bt_manager/bt_events_gap.c
    ../../components/bt_manager/bt_events_a2dp.c
    ../../components/bt_manager/bt_events_avrc.c
)
set(A2DP_STABILIZATION_MOCK_SOURCES
    mocks/mock_a2dp.c
    mocks/mock_avrc.c
    mocks/mock_gap.c
    mocks/nvs_storage_mock.c
    mocks/mock_audio_and_btstate.c
    mocks/bt_manager_test_hooks.c
    mocks/fake_esp_err.c
    mocks/fake_log.c
)

function(add_a2dp_stabilization_test target source)
    add_executable(${target}
        ${source}
        ${A2DP_STABILIZATION_MANAGER_SOURCES}
        ${A2DP_STABILIZATION_MOCK_SOURCES}
    )
    target_compile_definitions(${target} PRIVATE
        UNIT_TEST
        CONFIG_BT_MOCK_TESTING=1
    )
    target_link_libraries(${target} unity util_safe_host platform_shim_host pthread)
endfunction()

add_a2dp_stabilization_test(
    test_a2dp_binding_diagnostics_exact
    test_a2dp_binding_diagnostics_exact.c
)
add_test(NAME test_a2dp_binding_diagnostics_exact
         COMMAND $<TARGET_FILE:test_a2dp_binding_diagnostics_exact>)

add_a2dp_stabilization_test(
    test_a2dp_cross_session_exact
    test_a2dp_cross_session_exact.c
)
add_test(NAME test_a2dp_cross_session_exact
         COMMAND $<TARGET_FILE:test_a2dp_cross_session_exact>)

add_a2dp_stabilization_test(
    test_a2dp_secondary_failures_exact
    test_a2dp_secondary_failures_exact.c
)
add_test(NAME test_a2dp_secondary_failures_exact
         COMMAND $<TARGET_FILE:test_a2dp_secondary_failures_exact>)

add_a2dp_stabilization_test(
    test_bt_manager_init_rollback
    test_bt_manager_init_rollback.c
)
# Quarantine is intentionally irreversible. Each rollback case therefore runs
# in a fresh process rather than using a test-only quarantine reset escape hatch.
add_test(NAME test_bt_manager_init_rollback_complete
         COMMAND $<TARGET_FILE:test_bt_manager_init_rollback> complete)
add_test(NAME test_bt_manager_init_rollback_callbacks_live
         COMMAND $<TARGET_FILE:test_bt_manager_init_rollback> callbacks-live)
add_test(NAME test_bt_manager_init_rollback_reset_fails
         COMMAND $<TARGET_FILE:test_bt_manager_init_rollback> reset-fails)
add_test(NAME test_bt_manager_init_rollback_cleanup_incomplete
         COMMAND $<TARGET_FILE:test_bt_manager_init_rollback> cleanup-incomplete)
"""


def require_exact_count(text: str, needle: str, expected: int, label: str) -> None:
    actual = text.count(needle)
    if actual != expected:
        raise RuntimeError(
            f"{label}: expected {expected} occurrence(s), found {actual}; "
            "refusing a partial or ambiguous patch"
        )


def main() -> None:
    text = CMAKE_PATH.read_text(encoding="utf-8")

    require_exact_count(text, RETIRED_ADAPTER_BLOCK, 1, "retired adapter block")
    require_exact_count(text, INSERTION_MARKER, 1, "stabilization insertion marker")
    require_exact_count(
        text,
        "add_a2dp_stabilization_test(",
        0,
        "pre-existing stabilization helper",
    )

    text = text.replace(RETIRED_ADAPTER_BLOCK, "", 1)
    text = text.replace(
        INSERTION_MARKER,
        INSERTION_MARKER + STABILIZATION_TARGETS,
        1,
    )

    forbidden = (
        "test_pairing_adapter_runner",
        "test_pairing_commands.c",
        "test_pairing_adapters.c",
    )
    for token in forbidden:
        if token in text:
            raise RuntimeError(f"retired adapter token still present: {token}")

    for required in (
        "test_a2dp_binding_diagnostics_exact",
        "test_a2dp_cross_session_exact",
        "test_a2dp_secondary_failures_exact",
        "test_bt_manager_init_rollback_complete",
        "test_bt_manager_init_rollback_callbacks_live",
        "test_bt_manager_init_rollback_reset_fails",
        "test_bt_manager_init_rollback_cleanup_incomplete",
    ):
        if required not in text:
            raise RuntimeError(f"required target/test missing after patch: {required}")

    CMAKE_PATH.write_text(text, encoding="utf-8")
    print(f"Patched {CMAKE_PATH} successfully")


if __name__ == "__main__":
    main()
