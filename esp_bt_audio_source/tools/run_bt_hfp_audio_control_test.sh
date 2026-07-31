#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
test_dir="${project_dir}/test/host_test"
build_dir="${test_dir}/build_host_tests/bt_hfp_audio_control"
binary="${build_dir}/test_bt_hfp_audio_control"
compile_log="${build_dir}/compile.log"
test_log="${build_dir}/test.log"

if [[ ! -f "${unity_dir}/unity.c" || ! -f "${unity_dir}/unity.h" ]]; then
    echo "ERROR: vendored Unity source not found at ${unity_dir}" >&2
    exit 2
fi

mkdir -p "${build_dir}"

common_flags=(
    -std=c11
    -Wall -Wextra -Werror
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -pthread
    -DUNIT_TEST
    -DBT_HFP_AUDIO_REQUEST_TIMEOUT_MS=10U
    -DBT_HFP_AUDIO_EVENT_TIMEOUT_MS=10U
    -I"${unity_dir}"
    -I"${test_dir}/mocks/include"
    -I"${test_dir}/mocks"
    -I"${project_dir}/components/command_interface/include"
    -I"${project_dir}/components/bt_manager/include"
    -I"${project_dir}/components/audio_processor/include"
    -I"${project_dir}/components/platform_shim"
)

{
    "${CC:-cc}" "${common_flags[@]}" \
        "${unity_dir}/unity.c" \
        "${project_dir}/components/platform_shim/platform_sync_host.c" \
        "${project_dir}/components/bt_manager/bt_hfp_event_contract.c" \
        "${project_dir}/components/bt_manager/bt_duplex_state_core.c" \
        "${project_dir}/components/bt_manager/bt_duplex_state_audio.c" \
        "${project_dir}/components/bt_manager/bt_duplex_state_transitions.c" \
        "${project_dir}/components/bt_manager/bt_hfp_audio_control.c" \
        "${project_dir}/components/bt_manager/bt_hfp_audio_control_i2s.c" \
        "${project_dir}/components/bt_manager/bt_hfp_audio_control_work.c" \
        "${project_dir}/components/bt_manager/bt_hfp_audio_control_events.c" \
        "${test_dir}/mocks/bt_hfp_event_command_stub.c" \
        "${test_dir}/mocks/bt_hfp_audio_control_dependencies.c" \
        "${test_dir}/test_bt_hfp_audio_control_cases.c" \
        "${test_dir}/test_bt_hfp_audio_control_lifecycle_cases.c" \
        "${test_dir}/test_bt_hfp_audio_control.c" \
        -o "${binary}"
} 2>&1 | tee "${compile_log}"

{
    ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        "${binary}"
} 2>&1 | tee "${test_log}"

# FD-12 builds the stable event contract and real dual-UART broadcast path as
# focused sanitizer binaries before the FD-11 manager/command facade suites.
bash "${project_dir}/tools/run_bt_hfp_event_contract_test.sh"
bash "${project_dir}/tools/run_bt_hfp_manager_test.sh"
bash "${project_dir}/tools/run_bt_hfp_commands_test.sh"
