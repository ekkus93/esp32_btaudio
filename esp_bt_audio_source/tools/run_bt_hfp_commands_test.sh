#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
test_dir="${project_dir}/test/host_test"
build_dir="${test_dir}/build_host_tests/bt_hfp_commands"
binary="${build_dir}/test_bt_hfp_commands"
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
    -DUNIT_TEST
    -I"${unity_dir}"
    -I"${test_dir}/mocks/include"
    -I"${test_dir}/mocks"
    -I"${project_dir}/components/command_interface/include"
    -I"${project_dir}/components/bt_manager/include"
    -I"${project_dir}/components/audio_processor/include"
    -I"${project_dir}/components/nvs_storage"
    -I"${project_dir}/components/platform_shim"
    -I"${project_dir}/components/util_safe/include"
)

{
    "${CC:-cc}" "${common_flags[@]}" \
        "${unity_dir}/unity.c" \
        "${project_dir}/components/command_interface/commands.c" \
        "${project_dir}/components/command_interface/cmd_handlers_hfp_fd11_v2.c" \
        "${test_dir}/mocks/fake_esp_err.c" \
        "${test_dir}/mocks/bt_hfp_command_uart_stub.c" \
        "${test_dir}/mocks/bt_hfp_manager_command_stub.c" \
        "${test_dir}/mocks/bt_hfp_command_dependencies.c" \
        "${test_dir}/test_bt_hfp_commands_cases.c" \
        "${test_dir}/test_bt_hfp_commands_integrity_cases.c" \
        "${test_dir}/test_bt_hfp_commands.c" \
        -o "${binary}"
} 2>&1 | tee "${compile_log}"

{
    ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        "${binary}"
} 2>&1 | tee "${test_log}"
