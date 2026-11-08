#!/usr/bin/env bash
# Helper: flash app + partition table + canonical spiffs image
# Usage: tools/flash_with_spiffs.sh [PORT] [SPIFFS_PATH]
# Defaults: PORT=/dev/ttyUSB0, SPIFFS_PATH=main/assets/spiffs/spiffs.bin
set -euo pipefail
PORT=${1:-/dev/ttyUSB0}
SPIFFS_IMAGE=${2:-main/assets/spiffs/spiffs.bin}
PARTITION_BIN="build/partition_table/partition-table.bin"
PART_OFFSET=0x1C0000
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -f "$SPIFFS_IMAGE" ]; then
  echo "ERROR: SPIFFS image not found: $SPIFFS_IMAGE" >&2
  exit 2
fi
if [ ! -f "$PARTITION_BIN" ]; then
  echo "ERROR: partition table binary not found: $PARTITION_BIN" >&2
  echo "Please run: idf.py build" >&2
  exit 3
fi

# Find the ESP-IDF environment script (EIM activate_idf_<ver>.sh or legacy export.sh).
IDF_ENV_SCRIPT="$(python3 "$SCRIPT_DIR/idf_env.py")" || exit 4

# Run a command inside the IDF environment. The script is sourced by a fresh
# `bash -c`, never by this file: EIM's activate script calls `exit 1` when it
# is sourced from a script file, and it breaks under `set -u`.
in_idf() {
  bash -c '. "$1" >/dev/null 2>&1 || exit 1; shift; "$@"' bash "$IDF_ENV_SCRIPT" "$@"
}

echo "Using IDF environment: $IDF_ENV_SCRIPT"

echo "Flashing app and partition table to $PORT (this will reset the device)..."
in_idf idf.py -p "$PORT" flash

echo "Writing SPIFFS image $SPIFFS_IMAGE to offset $PART_OFFSET..."
in_idf python -m esptool --chip esp32 --port "$PORT" --baud 460800 write_flash "$PART_OFFSET" "$SPIFFS_IMAGE"

echo "Done. Please open monitor: idf.py -p $PORT monitor"
exit 0
