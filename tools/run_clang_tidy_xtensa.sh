#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-esp_bt_audio_source/build_clang_tidy}
DB_DIR="$ROOT_DIR/$BUILD_DIR"
DB_PATH="$DB_DIR/compile_commands.json"

if [ ! -f "$DB_PATH" ]; then
    echo "compile_commands.json not found at $DB_PATH" >&2
    exit 1
fi

CLANG_PREFIX=${CLANG_PREFIX:-$HOME/.espressif/tools/esp-clang/esp-18.1.2_20240912/esp-clang/bin}
CLANG_TIDY=${CLANG_TIDY:-$CLANG_PREFIX/clang-tidy}
RUN_CLANG_TIDY=${RUN_CLANG_TIDY:-$CLANG_PREFIX/run-clang-tidy}

if [ ! -x "$RUN_CLANG_TIDY" ]; then
    RUN_CLANG_TIDY=$(command -v run-clang-tidy || true)
fi
if [ -z "$RUN_CLANG_TIDY" ] || [ ! -x "$RUN_CLANG_TIDY" ]; then
    echo "run-clang-tidy not found; set RUN_CLANG_TIDY to the esp-clang binary" >&2
    exit 1
fi

SYSROOT_BASE=${SYSROOT_BASE:-$HOME/.espressif/tools/esp-clang/esp-18.1.2_20240912/esp-clang/lib/clang-runtimes/xtensa-esp-unknown-elf/esp32}
RUNTIME_INCLUDE="$SYSROOT_BASE/include"
CLANG_INCLUDE=$(cd "$CLANG_PREFIX/../lib/clang/18/include" && pwd)

for dir in "$SYSROOT_BASE" "$RUNTIME_INCLUDE" "$CLANG_INCLUDE"; do
    if [ ! -d "$dir" ]; then
        echo "Missing required directory: $dir" >&2
        exit 1
    fi
done

EXTRA_ARGS=(
    "--target=xtensa-esp32-elf"
    "--sysroot=$SYSROOT_BASE"
    "-isystem$RUNTIME_INCLUDE"
    "-isystem$CLANG_INCLUDE"
    "-Qunused-arguments"
)

CXX_INCLUDE="$SYSROOT_BASE/include/c++/v1"
if [ -d "$CXX_INCLUDE" ]; then
    EXTRA_ARGS+=("-isystem$CXX_INCLUDE")
fi

PREFIXED_ARGS=()
for arg in "${EXTRA_ARGS[@]}"; do
    PREFIXED_ARGS+=("-extra-arg=$arg")
done

"$RUN_CLANG_TIDY" \
    -clang-tidy-binary "$CLANG_TIDY" \
    -quiet \
    -p "$DB_DIR" \
    "${PREFIXED_ARGS[@]}" \
    "$@"
