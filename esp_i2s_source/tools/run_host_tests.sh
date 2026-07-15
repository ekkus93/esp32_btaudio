#!/usr/bin/env bash
# Build + run the esp_i2s_source host unit tests (CTest). One-command verify
# entry point for the Ralph loop. No hardware, no ESP-IDF needed, no network.
#
# Usage: tools/run_host_tests.sh [--strict] [--no-strict] [--asan] [--ubsan] [--coverage]
# Strict warnings are ON by default (CMakeLists ENABLE_STRICT default); pass
# --no-strict to relax them. Flags compose (e.g. --asan --ubsan). Each
# distinct flag combination gets its own build directory so sanitizer/warning
# instrumentation from one mode never leaves stale object files behind for
# another mode.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE_ARGS=()
SUFFIX="_strict"
for arg in "$@"; do
    case "$arg" in
        --strict)   CMAKE_ARGS+=("-DENABLE_STRICT=ON") ;;
        --no-strict) CMAKE_ARGS+=("-DENABLE_STRICT=OFF"); SUFFIX="" ;;
        --asan)     CMAKE_ARGS+=("-DENABLE_ASAN=ON");     SUFFIX="${SUFFIX}_asan" ;;
        --ubsan)    CMAKE_ARGS+=("-DENABLE_UBSAN=ON");    SUFFIX="${SUFFIX}_ubsan" ;;
        --coverage) CMAKE_ARGS+=("-DENABLE_COVERAGE=ON"); SUFFIX="${SUFFIX}_coverage" ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done
BUILD_DIR="$HERE/test/host_test/build_host_tests${SUFFIX}"

mkdir -p "$BUILD_DIR"
cmake -S "$HERE/test/host_test" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -- -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure
