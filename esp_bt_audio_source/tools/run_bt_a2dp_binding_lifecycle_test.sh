#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="${project_dir}/test/host_test"
source_dir="${test_dir}/a2dp_binding_lifecycle"
build_root="${test_dir}/build_host_tests"
build_dir="${build_root}/a2dp_binding_lifecycle_sanitized"
compile_log="${build_root}/a2dp_binding_lifecycle_compile.log"
test_log="${build_root}/a2dp_binding_lifecycle_test.log"

mkdir -p "${build_root}"

# Configure only the two production-path lifecycle targets. The general host
# CMake graph contains unrelated historical adapter targets, so using a focused
# graph prevents dead legacy sources from hiding failures in these tests.
{
    cmake -S "${source_dir}" -B "${build_dir}"
    cmake --build "${build_dir}" --parallel "$(nproc)"
} 2>&1 | tee "${compile_log}"

{
    for binary in \
        test_bt_ctx_lock \
        test_bt_manager_connection_pairing_events
    do
        echo "Running ${binary} with ASan/UBSan"
        ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:strict_string_checks=1" \
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
            "${build_dir}/${binary}"
    done
} 2>&1 | tee "${test_log}"
