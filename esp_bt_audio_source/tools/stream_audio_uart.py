#!/usr/bin/env python3
"""
stream_audio_uart.py — stream PCM audio from this machine to the ESP32
over the USB serial cable, for playback on the connected Bluetooth speaker.

Wire format: stereo 22050 Hz s16le PCM in CRC-framed chunks (see
components/command_interface/uart_audio_frame.c). The device upsamples
2x to 44.1 kHz and plays it through the normal A2DP path.

Input: a WAV file (must already be 22050 Hz / stereo / 16-bit) or '-'
for raw s16le on stdin. To play arbitrary audio, transcode with ffmpeg:

    ffmpeg -i song.mp3 -ar 22050 -ac 2 -f s16le - | \
        python tools/stream_audio_uart.py --port /dev/ttyUSB0 -

Typical session (speaker already connected via the CONNECT command):

    python tools/stream_audio_uart.py --port /dev/ttyUSB0 song.wav

Ctrl-C stops cleanly: a STOP frame is sent, the device drains its
buffer, answers UA|BYE and both sides drop back to 115200 text mode.
"""

import argparse
import struct
import sys
import threading
import time
import wave

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial (or use the python310 conda env)")

TEXT_BAUD = 115200
MAGIC = b"\xa5\x5a"
TYPE_DATA = 0x01
TYPE_STOP = 0x02
FRAME_PAYLOAD = 1024              # bytes per DATA frame (256 stereo frames)
WIRE_RATE = 22050 * 2 * 2         # payload bytes/second (88200)
FRAME_PERIOD = FRAME_PAYLOAD / WIRE_RATE

# feedback-driven pacing trim (asymmetric: overflow loses audio,
# underrun is only a brief silence)
FILL_HIGH_TRIM_S = 0.025          # ring > 75%: slow down by 25 ms
FILL_LOW_TRIM_S = 0.012           # ring < 25%: speed up by 12 ms


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE — must match uart_af_crc16() in firmware."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_data_frame(seq: int, payload: bytes) -> bytes:
    hdr = struct.pack("<BBH", TYPE_DATA, seq & 0xFF, len(payload))
    crc = crc16_ccitt_false(hdr + payload)
    return MAGIC + hdr + struct.pack("<H", crc) + payload


def build_stop_frame() -> bytes:
    hdr = struct.pack("<BBH", TYPE_STOP, 0, 0)
    crc = crc16_ccitt_false(hdr)
    return MAGIC + hdr + struct.pack("<H", crc)


def open_audio(path: str):
    """Return (read_fn, description). read_fn(n) -> up to n bytes of s16le stereo 22050."""
    if path == "-":
        src = sys.stdin.buffer
        return src.read, "raw s16le from stdin"

    wav = wave.open(path, "rb")
    problems = []
    if wav.getframerate() != 22050:
        problems.append(f"sample rate {wav.getframerate()} (need 22050)")
    if wav.getnchannels() != 2:
        problems.append(f"{wav.getnchannels()} channel(s) (need 2)")
    if wav.getsampwidth() != 2:
        problems.append(f"{8 * wav.getsampwidth()}-bit (need 16-bit)")
    if problems:
        sys.exit(
            f"{path}: " + "; ".join(problems) + "\n"
            "Convert first:  ffmpeg -i INPUT -ar 22050 -ac 2 -sample_fmt s16 out.wav"
        )

    duration = wav.getnframes() / wav.getframerate()

    def read_fn(n: int) -> bytes:
        return wav.readframes(n // 4)  # 4 bytes per stereo frame

    return read_fn, f"WAV, {duration:.1f} s"


class DeviceLines:
    """Background reader: drains device TX, tracks UA|FILL feedback and UA|BYE."""

    def __init__(self, ser: serial.Serial, verbose: bool):
        self.ser = ser
        self.verbose = verbose
        self.fill_used = None
        self.fill_cap = None
        self.last_fill = None       # full parsed tuple for the final report
        self.last_a2dp_bps = None   # device A2DP pull rate (bytes/s), if reported
        self.bye = threading.Event()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=1.0)

    def _run(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(256)
            except (serial.SerialException, OSError):
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self._handle(line.strip().decode("utf-8", errors="replace"))

    def _handle(self, line: str):
        if not line:
            return
        if line.startswith("UA|FILL|"):
            parts = line.split("|")[2:]
            if len(parts) in (7, 8):  # 8th field (a2dp pull B/s) added later
                try:
                    nums = [int(p) for p in parts]
                except ValueError:
                    return
                used, cap, und, crc, lost, ovf, seq = nums[:7]
                a2dp_bps = nums[7] if len(nums) == 8 else None
                self.fill_used, self.fill_cap = used, cap
                self.last_fill = (used, cap, und, crc, lost, ovf, seq)
                if a2dp_bps is not None:
                    self.last_a2dp_bps = a2dp_bps
                if self.verbose:
                    pct = 100 * used // cap if cap else 0
                    rate = f" a2dp={a2dp_bps/1000:.1f}kB/s" if a2dp_bps else ""
                    print(f"  [fill {pct:3d}%  und={und} crc={crc} lost={lost} ovf={ovf}{rate}]")
        elif line.startswith("UA|BYE"):
            self.bye.set()
        elif self.verbose:
            print(f"  [dev] {line}")


def wait_for(ser: serial.Serial, needle: str, timeout_s: float) -> bool:
    """Read lines until one contains needle (device may interleave logs)."""
    deadline = time.monotonic() + timeout_s
    buf = b""
    while time.monotonic() < deadline:
        chunk = ser.read(64)
        if chunk:
            buf += chunk
            if needle.encode() in buf:
                return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("audio", help="WAV file (22050 Hz stereo s16) or '-' for raw s16le stdin")
    ap.add_argument("--port", default="/dev/ttyUSB0", help="serial port (default /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=921600,
                    help="streaming baud rate (default 921600)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print device feedback lines")
    args = ap.parse_args()

    read_fn, desc = open_audio(args.audio)

    ser = serial.Serial(args.port, TEXT_BAUD, timeout=0.05)
    time.sleep(0.2)
    ser.reset_input_buffer()

    # ── handshake at 115200 ──────────────────────────────────────────
    ser.write(f"UARTAUDIO START {args.baud}\r\n".encode())
    ser.flush()
    if not wait_for(ser, "OK|UARTAUDIO|STARTING", 3.0):
        ser.close()
        sys.exit("no OK|UARTAUDIO|STARTING from device — is the firmware current and idle?")

    time.sleep(0.05)                    # let the device switch baud first
    ser.baudrate = args.baud
    ser.reset_input_buffer()

    if not wait_for(ser, "UA|READY", 3.0):
        ser.baudrate = TEXT_BAUD
        ser.close()
        sys.exit(f"no UA|READY beacon at {args.baud} baud — try a lower --baud")

    print(f"streaming {desc} at {args.baud} baud ({WIRE_RATE} B/s payload) — Ctrl-C to stop")

    lines = DeviceLines(ser, args.verbose)
    lines.start()

    # ── paced streaming ──────────────────────────────────────────────
    seq = 0
    sent_frames = 0
    origin = time.monotonic()
    interrupted = False
    try:
        while True:
            payload = read_fn(FRAME_PAYLOAD)
            if not payload:
                break                   # end of input
            if len(payload) % 4:        # keep whole stereo frames
                payload = payload[: len(payload) - (len(payload) % 4)]
                if not payload:
                    break

            # absolute-deadline pacing: frame n leaves no earlier than
            # origin + n*FRAME_PERIOD; feedback trims the origin
            deadline = origin + sent_frames * FRAME_PERIOD
            delay = deadline - time.monotonic()
            if delay > 0:
                time.sleep(delay)

            ser.write(build_data_frame(seq, payload))
            seq = (seq + 1) & 0xFF
            sent_frames += 1

            used, cap = lines.fill_used, lines.fill_cap
            if used is not None and cap:
                if used > 3 * cap // 4:
                    origin += FILL_HIGH_TRIM_S
                    lines.fill_used = None      # one trim per feedback line
                elif used < cap // 4 and sent_frames > 32:
                    origin -= FILL_LOW_TRIM_S
                    lines.fill_used = None
    except KeyboardInterrupt:
        interrupted = True
        print("\ninterrupted — stopping stream")

    # ── shutdown handshake ───────────────────────────────────────────
    ser.write(build_stop_frame())
    ser.flush()
    if not lines.bye.wait(timeout=1.5):
        print("warning: no UA|BYE from device (it will time out on its own)")
    lines.stop()

    ser.baudrate = TEXT_BAUD
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.close()

    mins, secs = divmod(sent_frames * FRAME_PERIOD, 60)
    print(f"sent {sent_frames} frames ({int(mins)}m{secs:04.1f}s of audio)"
          + (" [interrupted]" if interrupted else ""))
    if lines.last_fill:
        used, cap, und, crc, lost, ovf, _ = lines.last_fill
        print(f"device last report: underruns={und} crc_errors={crc} "
              f"frames_lost={lost} overflows={ovf}")
        if lines.last_a2dp_bps is not None:
            need = 44100 * 4
            pct = 100 * lines.last_a2dp_bps / need
            print(f"device A2DP pull rate: {lines.last_a2dp_bps/1000:.1f} kB/s "
                  f"({pct:.0f}% of the {need/1000:.1f} kB/s real-time rate)")
        if crc or lost or ovf:
            print("note: nonzero error counters — try a lower --baud or a shorter cable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
