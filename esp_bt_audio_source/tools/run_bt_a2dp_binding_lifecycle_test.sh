#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="${project_dir}/test/host_test"
build_root="${test_dir}/build_host_tests"
build_dir="${build_root}/a2dp_binding_lifecycle_sanitized"
compile_log="${build_root}/a2dp_binding_lifecycle_compile.log"
test_log="${build_root}/a2dp_binding_lifecycle_test.log"

mkdir -p "${build_root}"

# Build the existing CMake targets so these sanitizer runs exercise the same
# real bt_manager.c and bt_events_a2dp.c link graph used by the complete host
# suite. ASan is enabled by the repository option; UBSan is supplied through
# CMake's compile and executable-link flags.
{
    cmake -S "${test_dir}" -B "${build_dir}" \
        -DENABLE_ASAN=ON \
        -DENABLE_COVERAGE=OFF \
        -DCMAKE_C_FLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"

    cmake --build "${build_dir}" \
        --target test_bt_ctx_lock test_bt_manager_connection_pairing_events \
        --parallel "$(nproc)"
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
