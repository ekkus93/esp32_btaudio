#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
test_dir="${project_dir}/test/host_test"
build_dir="${test_dir}/build_host_tests/bt_hfp_audio"
binary="${build_dir}/test_bt_hfp_audio"
module_object="${build_dir}/bt_hfp_audio.o"
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
    -I"${unity_dir}"
    -I"${test_dir}/mocks/include"
    -I"${project_dir}/components/bt_manager/include"
    -I"${project_dir}/components/audio_processor/include"
    -I"${project_dir}/components/platform_shim"
)

{
    "${CC:-cc}" "${common_flags[@]}" \
        -c "${project_dir}/components/bt_manager/bt_hfp_audio.c" \
        -o "${module_object}"

    if nm -u "${module_object}" | grep -Eq \
        '(^|[[:space:]])(malloc|calloc|realloc|heap_caps_malloc|heap_caps_calloc)$'; then
        echo "ERROR: allocation symbol is reachable from bt_hfp_audio.o" >&2
        exit 1
    fi

    "${CC:-cc}" "${common_flags[@]}" \
        "${unity_dir}/unity.c" \
        "${module_object}" \
        "${project_dir}/components/platform_shim/platform_sync_host.c" \
        "${test_dir}/mocks/bt_hfp_audio_i2s_stub.c" \
        "${test_dir}/test_bt_hfp_audio_cases.c" \
        "${test_dir}/test_bt_hfp_audio_concurrency.c" \
        "${test_dir}/test_bt_hfp_audio.c" \
        -o "${binary}"
} 2>&1 | tee "${compile_log}"

{
    ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        "${binary}"
} 2>&1 | tee "${test_log}"
