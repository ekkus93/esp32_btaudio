#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
test_dir="${project_dir}/test/host_test"
root_build_dir="${test_dir}/build_host_tests"
build_dir="${root_build_dir}/bt_hfp_connection"
binary="${build_dir}/test_bt_hfp_connection"
compile_log="${root_build_dir}/fd07-hfp-connection-compile.log"
test_log="${root_build_dir}/fd07-hfp-connection-test.log"

if [[ ! -f "${unity_dir}/unity.c" || ! -f "${unity_dir}/unity.h" ]]; then
    echo "ERROR: vendored Unity source not found at ${unity_dir}" >&2
    exit 2
fi

mkdir -p "${build_dir}"

set +e
"${CC:-cc}" \
    -std=c11 \
    -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -pthread \
    -DUNIT_TEST \
    -I"${unity_dir}" \
    -I"${test_dir}/mocks/include" \
    -I"${test_dir}/mocks" \
    -I"${test_dir}/include" \
    -I"${test_dir}/../component/bt_mock/include" \
    -I"${test_dir}/../component/test_common/include" \
    -I"${project_dir}/components/command_interface/include" \
    -I"${project_dir}/components/bt_manager/include" \
    -I"${project_dir}/components/util_safe/include" \
    -I"${project_dir}/components/nvs_storage" \
    -I"${test_dir}/esp_idf_stubs/bt/common/osi/include" \
    -I"${project_dir}/components/audio_processor/include" \
    -I"${project_dir}/components/platform_shim" \
    "${unity_dir}/unity.c" \
    "${test_dir}/test_bt_hfp_connection.c" \
    "${project_dir}/components/bt_manager/bt_hfp_connection.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_core.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_profile.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_transitions.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_strings.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_events.c" \
    "${test_dir}/mocks/bt_app_core_host_stub.c" \
    "${test_dir}/mocks/bt_hfp_connection_platform_stubs.c" \
    "${test_dir}/mocks/fake_esp_err.c" \
    "${test_dir}/mocks/fake_log.c" \
    "${project_dir}/components/platform_shim/platform_sync_host.c" \
    -lm \
    -o "${binary}" 2>&1 | tee "${compile_log}"
compile_status=${PIPESTATUS[0]}
set -e
if (( compile_status != 0 )); then
    exit "${compile_status}"
fi

set +e
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${binary}" 2>&1 | tee "${test_log}"
test_status=${PIPESTATUS[0]}
set -e
exit "${test_status}"
