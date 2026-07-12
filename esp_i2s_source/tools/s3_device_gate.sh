#!/usr/bin/env bash
#
# esp_i2s_source ESP32-S3 device gate: flash (optional) → boot the app → capture
# the boot console → assert the DIAG boot markers → exit 0/1.
#
# Turns the manual s3_flash_run.sh smoke into an automated pass/fail gate. The
# capture reuses the two USB-Serial-JTAG workarounds from s3_flash_run.sh
# (watchdog_reset to boot the app, ACM port rescan). The verdict is delegated to
# tools/s3_gate_assert.py (pure + unit-tested in tools/test_s3_gate_assert.py).
#
# What it gates on (see s3_gate_assert.py): a clean boot — no panic, and both
# CONSOLE|READY + WEB|READY reached (app_main ran through every init). BOOT|READY/
# PSRAM, WiFi, I2S throughput and the BTLINK self-test are reported but only warn
# by default, since they depend on capture timing / a known AP / audio actually
# playing / the WROOM32 being wired. Promote the last two to hard failures with
# --require-i2s / --require-link when that context is guaranteed.
#
# Usage:
#   tools/s3_device_gate.sh                    # build + flash + boot + assert
#   tools/s3_device_gate.sh --no-flash         # just boot + assert (already flashed)
#   tools/s3_device_gate.sh --require-link      # also fail if BTLINK self-test != 3/3
#   tools/s3_device_gate.sh --require-i2s       # also fail if I2S output is idle/stalled
#   tools/s3_device_gate.sh --seconds 25
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$HERE"

DO_FLASH=1
# Wide enough to span the early one-shot BOOT|READY and the later CONSOLE/WEB
# markers, which only print after WiFi association (a few seconds).
SECONDS_CAP=20
GATE_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --no-flash)      DO_FLASH=0 ;;
        --require-i2s)   GATE_ARGS+=("--require-i2s") ;;
        --require-link)  GATE_ARGS+=("--require-link") ;;
        --seconds)       SECONDS_CAP="${2:-20}"; shift ;;
        --seconds=*)     SECONDS_CAP="${1#*=}" ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
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

# Boot the app via watchdog reset (download-mode workaround, see s3_flash_run.sh).
PORT="$(first_port)"; [ -n "$PORT" ] || { echo "no /dev/ttyACM* found"; exit 1; }
echo ">> booting app via watchdog_reset on $PORT"
esptool.py --chip esp32s3 -p "$PORT" --before default_reset --after watchdog_reset \
    flash_id >/dev/null 2>&1 || true

CAP="$(mktemp)"
trap 'rm -f "$CAP"' EXIT

# Capture the boot console (re-enumeration may move the port; rescan then read).
python3 - "$SECONDS_CAP" "$CAP" <<'PY'
import sys, time, glob, serial
secs = float(sys.argv[1])
cap_path = sys.argv[2]
# Open the re-enumerated CDC port as fast as it appears: BOOT|READY is a one-shot
# line printed within ~1s of the watchdog reset, so a long pre-sleep races past it.
time.sleep(0.3)
port = None
end = time.time() + 8
while time.time() < end:
    ports = glob.glob('/dev/ttyACM*')
    if ports:
        port = ports[0]; break
    time.sleep(0.2)
if not port:
    print("no /dev/ttyACM* after reset"); sys.exit(1)
s = serial.Serial(port, 115200, timeout=0.3)
print(f">> reading {port} for {secs:.0f}s")
buf = []
end = time.time() + secs
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        txt = chunk.decode('utf-8', 'replace')
        sys.stdout.write(txt); sys.stdout.flush()
        buf.append(txt)
s.close()
with open(cap_path, 'w', encoding='utf-8') as f:
    f.write(''.join(buf))
PY

echo
echo ">> gate verdict:"
python3 "$HERE/tools/s3_gate_assert.py" --file "$CAP" "${GATE_ARGS[@]}"
