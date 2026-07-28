#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
test_dir="${project_dir}/test/host_test"
build_dir="${test_dir}/build_host_tests/bt_manager_hfp_profiles"
binary="${build_dir}/test_bt_manager_hfp_profiles"
compile_log="${build_dir}/compile.log"
test_log="${build_dir}/test.log"

if [[ ! -f "${unity_dir}/unity.c" || ! -f "${unity_dir}/unity.h" ]]; then
    echo "ERROR: vendored Unity source not found at ${unity_dir}" >&2
    exit 2
fi

mkdir -p "${build_dir}"

{
"${CC:-cc}" \
    -std=c11 \
    -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -pthread \
    -DUNIT_TEST \
    -DBT_MANAGER_TEST_HFP_PROFILES \
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
    "${test_dir}/test_bt_manager_hfp_profiles.c" \
    "${project_dir}/components/bt_manager/bt_manager.c" \
    "${project_dir}/components/bt_manager/bt_manager_ops.c" \
    "${project_dir}/components/bt_manager/bt_manager_mocks.c" \
    "${project_dir}/components/bt_manager/bt_pairing_store.c" \
    "${project_dir}/components/bt_manager/bt_scan.c" \
    "${project_dir}/components/bt_manager/bt_connection.c" \
    "${project_dir}/components/bt_manager/bt_events_gap.c" \
    "${project_dir}/components/bt_manager/bt_events_a2dp.c" \
    "${project_dir}/components/bt_manager/bt_events_avrc.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_core.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_profile.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_transitions.c" \
    "${project_dir}/components/bt_manager/bt_duplex_state_strings.c" \
    "${project_dir}/components/bt_manager/bt_hfp_ag_lifecycle.c" \
    "${project_dir}/components/bt_manager/bt_hfp_ag_events.c" \
    "${test_dir}/mocks/mock_a2dp.c" \
    "${test_dir}/mocks/mock_avrc.c" \
    "${test_dir}/mocks/mock_gap.c" \
    "${test_dir}/mocks/nvs_storage_mock.c" \
    "${test_dir}/mocks/mock_audio_and_btstate.c" \
    "${test_dir}/mocks/bt_manager_test_hooks.c" \
    "${test_dir}/mocks/fake_esp_err.c" \
    "${test_dir}/mocks/fake_log.c" \
    "${project_dir}/components/util_safe/util_safe.c" \
    "${project_dir}/components/platform_shim/platform_sync_host.c" \
    "${project_dir}/components/platform_shim/platform_timing_host.c" \
    "${project_dir}/components/platform_shim/platform_memory_host.c" \
    "${project_dir}/components/platform_shim/platform_storage_host.c" \
    -lm \
    -o "${binary}"
} 2>&1 | tee "${compile_log}"

{
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${binary}"
} 2>&1 | tee "${test_log}"
