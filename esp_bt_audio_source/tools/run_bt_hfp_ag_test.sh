#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
build_dir="${project_dir}/test/host_test/build_host_tests/bt_hfp_ag"
binary="${build_dir}/test_bt_hfp_ag"

if [[ ! -f "${unity_dir}/unity.c" || ! -f "${unity_dir}/unity.h" ]]; then
    echo "ERROR: vendored Unity source not found at ${unity_dir}" >&2
    exit 2
fi

mkdir -p "${build_dir}"

"${CC:-cc}" \
    -std=c11 \
    -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -pthread \
    -DUNIT_TEST \
    -I"${unity_dir}" \
    -I"${project_dir}/test/host_test/mocks/include" \
    -I"${project_dir}/test/host_test/mocks" \
    -I"${project_dir}/components/command_interface/include" \
    -I"${project_dir}/components/bt_manager/include" \
    -I"${project_dir}/components/platform_shim" \
    "${unity_dir}/unity.c" \
    "${project_dir}/components/platform_shim/platform_sync_host.c" \
    "${project_dir}/components/bt_manager/bt_hfp_event_contract.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_core.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_profile.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_transitions.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_strings.c" \
    "${project_dir}/components/bt_manager/bt_hfp_ag_lifecycle.c" \
    "${project_dir}/components/bt_manager/bt_hfp_ag_events.c" \
    "${project_dir}/test/host_test/mocks/bt_hfp_event_command_stub.c" \
    "${project_dir}/test/host_test/mocks/bt_hfp_connection_untracked_stub.c" \
    "${project_dir}/test/host_test/mocks/bt_hfp_audio_lifecycle_stub.c" \
    "${project_dir}/test/host_test/test_bt_hfp_ag_cases.c" \
    "${project_dir}/test/host_test/test_bt_hfp_ag_fd10_cases.c" \
    "${project_dir}/test/host_test/test_bt_hfp_ag.c" \
    -o "${binary}"

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${binary}"
