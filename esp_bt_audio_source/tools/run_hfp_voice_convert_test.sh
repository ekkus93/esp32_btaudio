#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
build_dir="${project_dir}/test/host_test/build_host_tests/hfp_voice_convert"
binary="${build_dir}/test_hfp_voice_convert"

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
    -I"${unity_dir}" \
    -I"${project_dir}/test/host_test/mocks/include" \
    -I"${project_dir}/components/audio_processor/include" \
    "${unity_dir}/unity.c" \
    "${project_dir}/components/audio_processor/hfp_voice_convert.c" \
    "${project_dir}/test/host_test/test_hfp_voice_convert_cases.c" \
    "${project_dir}/test/host_test/test_hfp_voice_convert.c" \
    -o "${binary}"

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${binary}"
