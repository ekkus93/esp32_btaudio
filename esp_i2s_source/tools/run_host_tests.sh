#!/usr/bin/env bash
# Build + run the esp_i2s_source host unit tests (CTest). One-command verify
# entry point for the Ralph loop. No hardware, no ESP-IDF needed, no network.
#
# Usage: tools/run_host_tests.sh [--strict] [--asan] [--ubsan] [--coverage]
# Flags compose (e.g. --strict --asan). Each distinct flag combination gets
# its own build directory so sanitizer/warning instrumentation from one mode
# never leaves stale object files behind for another mode.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE_ARGS=()
SUFFIX=""
for arg in "$@"; do
    case "$arg" in
        --strict)   CMAKE_ARGS+=("-DENABLE_STRICT=ON");   SUFFIX="${SUFFIX}_strict" ;;
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
