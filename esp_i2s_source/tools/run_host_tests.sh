#!/usr/bin/env bash
# Build + run the esp_i2s_source host unit tests (CTest). One-command verify
# entry point for the Ralph loop. No hardware, no ESP-IDF needed.
#
# Usage: tools/run_host_tests.sh [--coverage] [--asan]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$HERE/test/host_test/build_host_tests"
CMAKE_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --coverage) CMAKE_ARGS+=("-DENABLE_COVERAGE=ON") ;;
        --asan)     CMAKE_ARGS+=("-DENABLE_ASAN=ON") ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

mkdir -p "$BUILD_DIR"
cmake -S "$HERE/test/host_test" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -- -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure
