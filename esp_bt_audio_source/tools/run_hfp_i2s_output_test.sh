#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "${project_dir}/.." && pwd)"
unity_dir="${repo_dir}/esp_i2s_source/test/third_party/unity/src"
test_dir="${project_dir}/test/host_test"
log_dir="${test_dir}/build_host_tests"
build_dir="${log_dir}/hfp_i2s_output"
binary="${build_dir}/test_hfp_i2s_output"
compile_log="${log_dir}/hfp_i2s_output-compile.log"
test_log="${log_dir}/hfp_i2s_output-test.log"

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
    -I"${project_dir}/components/audio_processor/include" \
    -I"${project_dir}/components/util_safe/include" \
    -I"${project_dir}/components/platform_shim" \
    "${unity_dir}/unity.c" \
    "${project_dir}/components/audio_processor/hfp_i2s_output.c" \
    "${project_dir}/components/audio_processor/hfp_i2s_output_platform.c" \
    "${project_dir}/components/audio_processor/hfp_i2s_output_lifecycle.c" \
    "${project_dir}/components/audio_processor/hfp_i2s_output_data.c" \
    "${project_dir}/components/audio_processor/hfp_pcm_ring.c" \
    "${project_dir}/components/audio_processor/hfp_voice_convert.c" \
    "${test_dir}/mocks/hfp_i2s_output_util_stubs.c" \
    "${project_dir}/components/platform_shim/platform_sync_host.c" \
    "${test_dir}/mocks/fake_log.c" \
    "${test_dir}/test_hfp_i2s_output_cases.c" \
    "${test_dir}/test_hfp_i2s_output.c" \
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
