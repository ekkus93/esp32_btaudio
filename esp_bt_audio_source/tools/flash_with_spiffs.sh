#!/usr/bin/env bash
# Helper: flash app + partition table + canonical spiffs image
# Usage: tools/flash_with_spiffs.sh [PORT] [SPIFFS_PATH]
# Defaults: PORT=/dev/ttyUSB0, SPIFFS_PATH=main/assets/spiffs/spiffs.bin
set -euo pipefail
PORT=${1:-/dev/ttyUSB0}
SPIFFS_IMAGE=${2:-main/assets/spiffs/spiffs.bin}
IDF_ENV=". $HOME/esp/esp-idf/export.sh"
PARTITION_BIN="build/partition_table/partition-table.bin"
APP_FLASH_ARGS=(--chip esp32 --port "$PORT" --baud 460800)
PART_OFFSET=0x1C0000

if [ ! -f "$SPIFFS_IMAGE" ]; then
  echo "ERROR: SPIFFS image not found: $SPIFFS_IMAGE" >&2
  exit 2
fi
if [ ! -f "$PARTITION_BIN" ]; then
  echo "ERROR: partition table binary not found: $PARTITION_BIN" >&2
  echo "Please run: idf.py build" >&2
  exit 3
fi

echo "Sourcing IDF environment..."
# shellcheck disable=SC1090
. $HOME/esp/esp-idf/export.sh

echo "Flashing app and partition table to $PORT (this will reset the device)..."
idf.py -p "$PORT" flash

echo "Writing SPIFFS image $SPIFFS_IMAGE to offset $PART_OFFSET..."
python -m esptool --chip esp32 --port "$PORT" --baud 460800 write_flash $PART_OFFSET "$SPIFFS_IMAGE"

echo "Done. Please open monitor: idf.py -p $PORT monitor"
exit 0
