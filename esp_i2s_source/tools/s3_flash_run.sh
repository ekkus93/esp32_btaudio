#!/usr/bin/env bash
#
# Flash esp_i2s_source to the ESP32-S3 over native USB-Serial-JTAG, boot the
# APP (not download mode), and capture the console.
#
# Why this exists — two USB-Serial-JTAG gotchas learned at INFRA-1d:
#   1. esptool's default reset ("Hard resetting via RTS pin") leaves THIS S3 in
#      DOWNLOAD mode (DTR holds GPIO0 low) — the app never runs. Booting the app
#      reliably requires `esptool --after watchdog_reset`.
#   2. The CDC port hops /dev/ttyACM0 <-> /dev/ttyACM1 on every re-enumeration,
#      so we rescan after the reset instead of hardcoding a port.
#
# Usage:
#   tools/s3_flash_run.sh            # build + flash + boot app + monitor 20s
#   tools/s3_flash_run.sh --no-flash # just boot the app + monitor
#   tools/s3_flash_run.sh --seconds 30
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

DO_FLASH=1
SECONDS_CAP=20
for a in "$@"; do
    case "$a" in
        --no-flash) DO_FLASH=0 ;;
        --seconds) shift; SECONDS_CAP="${1:-20}" ;;
        --seconds=*) SECONDS_CAP="${a#*=}" ;;
    esac
done

first_port() { ls /dev/ttyACM* 2>/dev/null | head -1; }

# shellcheck disable=SC1091
. "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1

if [ "$DO_FLASH" = 1 ]; then
    idf.py build
    PORT="$(first_port)"; [ -n "$PORT" ] || { echo "no /dev/ttyACM* found"; exit 1; }
    echo ">> flashing on $PORT"
    ( cd build && esptool.py --chip esp32s3 -p "$PORT" --before default_reset \
        --after no_reset write_flash "@flash_args" )
fi

# Boot the app via watchdog reset (the download-mode workaround).
PORT="$(first_port)"; [ -n "$PORT" ] || { echo "no /dev/ttyACM* found"; exit 1; }
echo ">> booting app via watchdog_reset on $PORT"
esptool.py --chip esp32s3 -p "$PORT" --before default_reset --after watchdog_reset \
    flash_id >/dev/null 2>&1 || true

# Re-enumeration may move the port; rescan and read.
python3 - "$SECONDS_CAP" <<'PY'
import sys, time, glob, serial
secs = float(sys.argv[1])
time.sleep(2.0)
port = None
end = time.time() + 8
while time.time() < end:
    ports = glob.glob('/dev/ttyACM*')
    if ports:
        port = ports[0]; break
    time.sleep(0.3)
if not port:
    print("no /dev/ttyACM* after reset"); sys.exit(1)
s = serial.Serial(port, 115200, timeout=0.3)
print(f">> reading {port} for {secs:.0f}s")
end = time.time() + secs
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        sys.stdout.write(chunk.decode('utf-8', 'replace')); sys.stdout.flush()
s.close()
PY
