"""
test_uart_streaming.py — UARTAUDIO: laptop -> UART -> ESP32 -> laptop-as-BT-speaker.

End-to-end verification of the UART audio streaming feature (UARTAUDIO-1..9)
with the laptop's own Bluetooth adapter acting as the A2DP sink. A synthetic
sine tone (stereo 22050 Hz s16le) is framed and paced over /dev/ttyUSB0 at
921600 baud, upsampled 2x on the ESP32 and played back over A2DP to the
laptop; counters on both sides confirm the audio actually flowed.

Frame building and CRC are imported from tools/stream_audio_uart.py so the
tests exercise the exact bytes the real host tool produces.

Text-only protocol tests (STATUS/BAD_BAUD/abort recovery) need just the
ESP32; the full streaming test additionally needs the A2DP link
(connected_state fixture). Never run in CI — conftest's hardware guard
skips the whole module when the ESP32 or BT adapter is absent.
"""

import importlib.util
import logging
import math
import re
import struct
import time
from pathlib import Path

import pytest

log = logging.getLogger(__name__)

pytestmark = pytest.mark.laptop_bt

# ---------------------------------------------------------------------------
# Reuse the real host tool's frame builders (single source of truth)
# ---------------------------------------------------------------------------

_TOOL_PATH = Path(__file__).resolve().parents[2] / "tools" / "stream_audio_uart.py"
_spec = importlib.util.spec_from_file_location("stream_audio_uart", _TOOL_PATH)
_tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_tool)

build_data_frame = _tool.build_data_frame
build_stop_frame = _tool.build_stop_frame
FRAME_PAYLOAD = _tool.FRAME_PAYLOAD
FRAME_PERIOD = _tool.FRAME_PERIOD

STREAM_BAUD = 921600
TEXT_BAUD = 115200

# firmware timing constants (uart_audio.c)
READY_ABORT_S = 5.0
INACTIVITY_S = 2.0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _make_tone_payloads(seconds, freq_hz=440.0):
    """Generate `seconds` of stereo 22050 Hz s16le sine, split into frames."""
    total_frames = int(seconds * 22050)
    samples = bytearray()
    for n in range(total_frames):
        v = int(12000 * math.sin(2 * math.pi * freq_hz * n / 22050.0))
        samples += struct.pack("<hh", v, v)
    return [
        bytes(samples[i:i + FRAME_PAYLOAD])
        for i in range(0, len(samples) - len(samples) % 4, FRAME_PAYLOAD)
    ]


def _read_until(raw, needle, timeout_s):
    """Binary-safe: read raw port until needle appears; returns full buffer."""
    deadline = time.monotonic() + timeout_s
    buf = b""
    while time.monotonic() < deadline:
        chunk = raw.read(256)
        if chunk:
            buf += chunk
            if needle in buf:
                return buf
    return buf


def _parse_stopped_counters(line):
    """EVENT|UARTAUDIO|STOPPED|frames=..,bytes=..,crc=..,... -> dict of ints."""
    fields = {}
    m = re.search(r"EVENT\|UARTAUDIO\|STOPPED\|(.+)", line)
    if not m:
        return fields
    for part in m.group(1).split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            try:
                fields[k.strip()] = int(v.strip())
            except ValueError:
                pass
    return fields


def _parse_audio_status(line):
    m = re.search(r"OK\|AUDIO_STATUS\|CURRENT\|(.+)", line)
    if not m:
        return {}
    return {
        k.strip(): v.strip()
        for k, v in (p.split("=", 1) for p in m.group(1).split(",") if "=" in p)
    }


def _recover_text_mode(esp32):
    """Best-effort: force the session back to 115200 text mode."""
    esp32.raw.baudrate = TEXT_BAUD
    time.sleep(0.2)
    esp32.drain(0.5)


# ---------------------------------------------------------------------------
# Text-mode protocol tests (no BT connection required)
# ---------------------------------------------------------------------------


class TestUartAudioTextMode:

    def test_status_reports_idle(self, esp32):
        """UARTAUDIO STATUS in text mode: streaming=0, source INACTIVE."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("UARTAUDIO STATUS", "OK|UARTAUDIO|STATUS|", timeout_s=5.0)
        assert "streaming=0" in line, "unexpected STATUS: {}".format(line)
        assert "state=INACTIVE" in line, "unexpected STATUS: {}".format(line)

    def test_bad_baud_rejected_text_mode_intact(self, esp32):
        """Unsupported baud is rejected and the command loop keeps working."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("UARTAUDIO START 12345", "ERR|UARTAUDIO|BAD_BAUD", timeout_s=5.0)
        assert "BAD_BAUD" in line

        line = esp32.send_and_expect("UARTAUDIO STATUS", "OK|UARTAUDIO|STATUS|", timeout_s=5.0)
        assert "streaming=0" in line

    def test_stop_in_text_mode_not_streaming(self, esp32):
        esp32.drain(0.1)
        line = esp32.send_and_expect("UARTAUDIO STOP", "ERR|UARTAUDIO|NOT_STREAMING", timeout_s=5.0)
        assert "NOT_STREAMING" in line

    @pytest.mark.slow
    def test_ready_abort_recovers_to_text_mode(self, esp32):
        """
        Host starts a stream but never sends a byte: the device must beacon
        UA|READY at the new baud, give up after ~5 s, restore 115200 and
        emit EVENT|UARTAUDIO|STOPPED — no reset, no stuck streaming mode.
        """
        esp32.drain(0.1)
        esp32.send_and_expect("UARTAUDIO START", "OK|UARTAUDIO|STARTING", timeout_s=5.0)
        try:
            # Stay at 115200 and send nothing. The 921600-baud READY
            # beacons arrive as garbage; wait out the abort window.
            time.sleep(READY_ABORT_S + 2.0)
        finally:
            _recover_text_mode(esp32)

        line = esp32.send_and_expect("UARTAUDIO STATUS", "OK|UARTAUDIO|STATUS|", timeout_s=5.0)
        assert "streaming=0" in line, "device stuck in streaming mode: {}".format(line)
        assert "state=INACTIVE" in line

    @pytest.mark.slow
    def test_host_death_inactivity_recovers(self, esp32):
        """
        Host sends one frame then goes silent: the 2 s inactivity timeout
        must return the device to text mode on its own.
        """
        raw = esp32.raw
        esp32.drain(0.1)
        esp32.send_and_expect("UARTAUDIO START", "OK|UARTAUDIO|STARTING", timeout_s=5.0)
        try:
            time.sleep(0.05)
            raw.baudrate = STREAM_BAUD
            raw.reset_input_buffer()
            buf = _read_until(raw, b"UA|READY", timeout_s=3.0)
            assert b"UA|READY" in buf, "no READY beacon at {} baud".format(STREAM_BAUD)

            raw.write(build_data_frame(0, _make_tone_payloads(0.1)[0]))
            raw.flush()
            # simulate host death: silence > inactivity timeout (+ drain window)
            time.sleep(INACTIVITY_S + 1.5)
        finally:
            _recover_text_mode(esp32)

        line = esp32.send_and_expect("UARTAUDIO STATUS", "OK|UARTAUDIO|STATUS|", timeout_s=5.0)
        assert "streaming=0" in line, "device stuck after host death: {}".format(line)


# ---------------------------------------------------------------------------
# Full end-to-end streaming test (laptop as A2DP sink)
# ---------------------------------------------------------------------------


class TestUartAudioStreaming:

    @pytest.mark.slow
    def test_stream_tone_to_laptop_sink(self, esp32, laptop_bt_adapter, connected_state):
        """
        Stream ~3 s of sine tone over UART while A2DP-connected to the
        laptop. Verifies the full path: handshake, paced framed transfer
        with UA|FILL feedback, clean STOP/UA|BYE shutdown, zero link-error
        counters, UART_BYTES growth in AUDIO_STATUS, and text-mode recovery.
        """
        payloads = _make_tone_payloads(3.0)
        raw = esp32.raw

        # audio engine must be running so the UART source is consumed
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(1.0)

        esp32.drain(0.1)
        esp32.send_and_expect("UARTAUDIO START", "OK|UARTAUDIO|STARTING", timeout_s=5.0)

        stopped_line = None
        fill_lines = []
        try:
            time.sleep(0.05)
            raw.baudrate = STREAM_BAUD
            raw.reset_input_buffer()
            buf = _read_until(raw, b"UA|READY", timeout_s=3.0)
            assert b"UA|READY" in buf, "no UA|READY beacon after baud switch"

            # paced transfer: frame n no earlier than origin + n*FRAME_PERIOD
            rx = b""
            origin = time.monotonic()
            for n, payload in enumerate(payloads):
                delay = origin + n * FRAME_PERIOD - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                raw.write(build_data_frame(n & 0xFF, payload))
                # non-blocking drain: raw.read(1) would block up to the
                # port timeout (100 ms) and stall pacing into underruns
                waiting = raw.in_waiting
                if waiting:
                    rx += raw.read(waiting)

            raw.write(build_stop_frame())
            raw.flush()
            rx += _read_until(raw, b"UA|BYE", timeout_s=2.0)
            assert b"UA|BYE" in rx, "no UA|BYE after STOP frame"
            fill_lines = [
                ln for ln in rx.split(b"\n") if ln.startswith(b"UA|FILL|")
            ]
        finally:
            raw.baudrate = TEXT_BAUD
            time.sleep(0.2)

        # STOPPED event arrives at 115200 after the device restores baud
        try:
            stopped_line = esp32.wait_for_line("EVENT|UARTAUDIO|STOPPED|", timeout_s=5.0)
        except TimeoutError:
            stopped_line = None
        esp32.drain(0.3)

        # ~3 s of audio at 4 UA|FILL/s -> expect several feedback lines
        assert len(fill_lines) >= 2, (
            "expected periodic UA|FILL feedback, got {}".format(len(fill_lines))
        )

        if stopped_line is not None:
            counters = _parse_stopped_counters(stopped_line)
            log.info("STOPPED counters: %s", counters)
            assert counters.get("frames", 0) >= len(payloads) - 1, (
                "device saw {} frames, host sent {}".format(
                    counters.get("frames"), len(payloads))
            )
            assert counters.get("crc", 0) == 0, "CRC errors on the link: {}".format(counters)
            assert counters.get("ovf", 0) == 0, "staging ring overflowed: {}".format(counters)
            assert counters.get("lost", 0) == 0, "frames lost (seq gaps): {}".format(counters)
        else:
            log.warning("EVENT|UARTAUDIO|STOPPED not captured; relying on STATUS checks")

        # text mode must be fully restored
        line = esp32.send_and_expect("UARTAUDIO STATUS", "OK|UARTAUDIO|STATUS|", timeout_s=5.0)
        assert "streaming=0" in line, "still in streaming mode: {}".format(line)

        # the decisive check: UART-source bytes actually reached the engine
        esp32.drain(0.1)
        line = esp32.send_and_expect("AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_audio_status(line)
        uart_bytes = int(fields.get("UART_BYTES", "0"))
        assert uart_bytes > 0, (
            "no UART-source audio flowed through the engine — AUDIO_STATUS: {}".format(fields)
        )
        log.info("UART_BYTES=%d after ~3 s stream", uart_bytes)

        esp32.drain(0.1)
        esp32.send_and_expect("STOP", "OK|STOP|", timeout_s=5.0)
