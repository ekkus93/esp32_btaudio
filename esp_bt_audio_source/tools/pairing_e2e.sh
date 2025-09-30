#!/usr/bin/env bash
# Simple helper: build, flash, and capture serial monitor output for a pairing E2E run
set -euo pipefail

WORKDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$WORKDIR"

PORT=${1:-/dev/ttyUSB0}
BUILD_DIR=build
OUT_DIR="$BUILD_DIR/pairing_e2e_logs"
mkdir -p "$OUT_DIR"

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
LOGFILE="$OUT_DIR/pairing_e2e_$TIMESTAMP.log"

echo "Building project in $WORKDIR..."
idf.py build

echo "Flashing and starting monitor on port $PORT..."
echo "Logs will be saved to $LOGFILE"

# Flash then start monitor and tee output to file
idf.py -p "$PORT" flash monitor |& tee "$LOGFILE"
