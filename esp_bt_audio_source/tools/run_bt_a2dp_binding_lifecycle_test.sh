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

# This is a dedicated, disposable sanitizer build. Remove only this directory
# so a restored CMake cache cannot retain the previous full-project source path
# or stale sanitizer flags.
rm -rf "${build_dir}"

# Configure only the production-path lifecycle targets. The general host CMake
# graph is validated separately; this focused graph gives lifecycle failures a
# small, deterministic sanitizer surface.
{
    cmake -S "${source_dir}" -B "${build_dir}"
    cmake --build "${build_dir}" --parallel "$(nproc)"
} 2>&1 | tee "${compile_log}"

run_sanitized() {
    local binary="$1"
    shift
    echo "Running ${binary} $* with ASan/UBSan"
    ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:strict_string_checks=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        "${build_dir}/${binary}" "$@"
}

{
    for binary in \
        test_bt_ctx_lock \
        test_bt_manager_connection_pairing_events \
        test_a2dp_binding_diagnostics_exact \
        test_a2dp_cross_session_exact \
        test_a2dp_secondary_failures_exact
    do
        run_sanitized "${binary}"
    done

    for rollback_case in \
        complete \
        callbacks-live \
        reset-fails \
        cleanup-incomplete
    do
        run_sanitized test_bt_manager_init_rollback "${rollback_case}"
    done
} 2>&1 | tee "${test_log}"
