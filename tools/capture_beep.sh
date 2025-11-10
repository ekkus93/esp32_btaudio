#!/bin/bash
# Usage: ./capture_beep.sh <serial_device> <baud> <capture_seconds> <output_log>
set -euo pipefail
DEV=${1:-/dev/ttyUSB0}
BAUD=${2:-115200}
CAP_SEC=${3:-3}
OUT=${4:-$(pwd)/diag_capture.log}
# Ensure device exists
if [ ! -e "$DEV" ]; then
  echo "Serial device $DEV not found" >&2
  exit 2
fi
# Configure serial parameters
stty -F "$DEV" $BAUD cs8 -cstopb -parenb -echo
# Start background capture
mkdir -p "$(dirname "$OUT")"
# Use cat to dump raw serial to file; ensure unbuffered output
stdbuf -oL cat "$DEV" > "$OUT" &
CAT_PID=$!
echo "Started capture PID=$CAT_PID -> $OUT"
# Small sleep to let capture warm up
sleep 0.1
# Arm the beep diag
printf "DEBUG BEEP_DIAG\r\n" > "$DEV"
echo "Sent: DEBUG BEEP_DIAG"
# Short pause then force beep
sleep 0.1
printf "DEBUG FORCE_BEEP\r\n" > "$DEV"
echo "Sent: DEBUG FORCE_BEEP"
# Wait capture window
sleep "$CAP_SEC"
# Stop capture
kill $CAT_PID 2>/dev/null || true
wait $CAT_PID 2>/dev/null || true
echo "Capture complete, saved to $OUT"
exit 0
