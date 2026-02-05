#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-esp_bt_audio_source/build_clang_tidy}
DB_DIR="$ROOT_DIR/$BUILD_DIR"
DB_PATH="$DB_DIR/compile_commands.json"

STRIP_FLAGS=(-fno-shrink-wrap -fno-tree-switch-conversion -fstrict-volatile-bitfields)
SANITIZED_DB_DIR="$DB_DIR/clangtidy_db"
SANITIZED_DB_PATH="$SANITIZED_DB_DIR/compile_commands.json"

if [ ! -f "$DB_PATH" ]; then
    echo "compile_commands.json not found at $DB_PATH" >&2
    exit 1
fi

CLANG_PREFIX=${CLANG_PREFIX:-$HOME/.espressif/tools/esp-clang/esp-19.1.2_20250312/esp-clang/bin}
CLANG_TIDY=${CLANG_TIDY:-$CLANG_PREFIX/clang-tidy}
RUN_CLANG_TIDY=${RUN_CLANG_TIDY:-$CLANG_PREFIX/run-clang-tidy}

if [ ! -x "$RUN_CLANG_TIDY" ]; then
    RUN_CLANG_TIDY=$(command -v run-clang-tidy || true)
fi
if [ -z "$RUN_CLANG_TIDY" ] || [ ! -x "$RUN_CLANG_TIDY" ]; then
    echo "run-clang-tidy not found; set RUN_CLANG_TIDY to the esp-clang binary" >&2
    exit 1
fi

SYSROOT_BASE=${SYSROOT_BASE:-$HOME/.espressif/tools/esp-clang/esp-19.1.2_20250312/esp-clang/lib/clang-runtimes/xtensa-esp-unknown-elf/esp32}
RUNTIME_INCLUDE="$SYSROOT_BASE/include"
CLANG_INCLUDE=""
CLANG_LIB_DIR=$(cd "$CLANG_PREFIX/../lib/clang" && pwd)
if [ -d "$CLANG_LIB_DIR" ]; then
    for ver in 19 18; do
        if [ -d "$CLANG_LIB_DIR/$ver/include" ]; then
            CLANG_INCLUDE="$CLANG_LIB_DIR/$ver/include"
            break
        fi
    done
fi
if [ -z "$CLANG_INCLUDE" ]; then
    echo "Missing clang include dir under $CLANG_PREFIX/../lib/clang (expected 19/ or 18/)" >&2
    exit 1
fi

for dir in "$SYSROOT_BASE" "$RUNTIME_INCLUDE" "$CLANG_INCLUDE"; do
    if [ ! -d "$dir" ]; then
        echo "Missing required directory: $dir" >&2
        exit 1
    fi
done

mkdir -p "$SANITIZED_DB_DIR"
STRIP_FLAGS_CSV=$(IFS=,; echo "${STRIP_FLAGS[*]}")
DB_IN="$DB_PATH" DB_OUT="$SANITIZED_DB_PATH" STRIP_FLAGS="$STRIP_FLAGS_CSV" python3 - <<'PY'
import json
import os
import sys

db_in = os.environ["DB_IN"]
db_out = os.environ["DB_OUT"]
strip_flags = [f for f in os.environ.get("STRIP_FLAGS", "").split(",") if f]

try:
    with open(db_in, "r", encoding="utf-8") as f:
        entries = json.load(f)
except Exception as exc:  # pragma: no cover - guard for malformed DB
    sys.stderr.write(f"Failed to load {db_in}: {exc}\n")
    sys.exit(1)

filtered_entries = []
for entry in entries:
    entry_file = entry.get("file")
    if entry_file and not os.path.exists(entry_file):
        continue
    if "arguments" in entry:
        entry["arguments"] = [arg for arg in entry["arguments"] if arg not in strip_flags]
    if "command" in entry:
        parts = entry["command"].split()
        entry["command"] = " ".join(part for part in parts if part not in strip_flags)
    filtered_entries.append(entry)

with open(db_out, "w", encoding="utf-8") as f:
    json.dump(filtered_entries, f, indent=2)
PY

DB_DIR_EFFECTIVE="$SANITIZED_DB_DIR"

EXTRA_ARGS=(
    "--target=xtensa-esp32-elf"
    "--sysroot=$SYSROOT_BASE"
    "-isystem$RUNTIME_INCLUDE"
    "-isystem$CLANG_INCLUDE"
    "-Qunused-arguments"
    "-Wno-unused-command-line-argument"
    "-Wno-unknown-warning-option"
    "-U_POSIX_READER_WRITER_LOCKS"
)

CXX_INCLUDE="$SYSROOT_BASE/include/c++/v1"
if [ -d "$CXX_INCLUDE" ]; then
    EXTRA_ARGS+=("-isystem$CXX_INCLUDE")
fi

PREFIXED_ARGS=()
for arg in "${EXTRA_ARGS[@]}"; do
    PREFIXED_ARGS+=("-extra-arg=$arg")
done

# Only lint project files, not ESP-IDF framework
# If user provides paths via $@, use those; otherwise default to project components
if [ $# -eq 0 ]; then
    PROJECT_FILTER="$ROOT_DIR/esp_bt_audio_source/components|$ROOT_DIR/esp_bt_audio_source/main"
else
    PROJECT_FILTER=""  # User specified paths, don't filter
fi

"$RUN_CLANG_TIDY" \
    -clang-tidy-binary "$CLANG_TIDY" \
    -quiet \
    -p "$DB_DIR_EFFECTIVE" \
    ${PROJECT_FILTER:+-header-filter="$PROJECT_FILTER"} \
    "${PREFIXED_ARGS[@]}" \
    ${PROJECT_FILTER:+"$PROJECT_FILTER"} \
    "$@"
