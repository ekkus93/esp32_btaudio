"""
esp32_serial.py — Synchronous serial driver for the ESP32 command protocol.

Wraps the command wire format used throughout the firmware:
  TX (host → ESP32): <COMMAND> [PARAM …]\r\n
  RX (ESP32 → host): <STATUS>|<COMMAND>|<RESULT>[|<DATA>]\n
    STATUS ∈ {OK, ERR, INFO, EVENT}

Usage::

    with ESP32Serial("/dev/ttyUSB0") as dev:
        line = dev.send_and_expect("STATUS", "OK|STATUS|")
        resp = dev.parse_response(line)
"""

import logging
import re
import time

import serial

log = logging.getLogger(__name__)

# Strip ANSI colour/escape sequences from firmware output
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[mGKHF]")

# Lines that indicate the device has booted and is ready for commands
_BOOT_PATTERNS = (
    "esp-idf",
    "I2S",
    "cmd_init",
    "Command interface",
    "bt_manager",
)


def _strip_ansi(text):
    return _ANSI_RE.sub("", text)


class ESP32Serial:
    """
    Synchronous serial driver for the ESP32 firmware command interface.

    All blocking read calls use a deadline-based loop over the underlying
    serial timeout so partial reads and short bursts are handled gracefully.
    """

    def __init__(self, port="/dev/ttyUSB0", baud=115200, timeout=2.0):
        self._port = port
        self._baud = baud
        self._timeout = timeout
        self._ser = serial.Serial(port, baud, timeout=0.1)
        log.info("ESP32Serial: opened %s @ %d", port, baud)
        self.drain(0.2)

    # ------------------------------------------------------------------
    # Context manager
    # ------------------------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def close(self):
        if self._ser and self._ser.is_open:
            self._ser.close()
            log.info("ESP32Serial: closed %s", self._port)

    # ------------------------------------------------------------------
    # Low-level I/O
    # ------------------------------------------------------------------

    def send(self, command):
        """Send a command string terminated with CR+LF."""
        data = (command + "\r\n").encode("utf-8")
        self._ser.write(data)
        log.debug("TX: %s", command)

    def readline(self, timeout_s=None):
        """
        Read one line from the device, stripping ANSI escapes and whitespace.
        Returns an empty string on timeout; never raises on partial reads.
        """
        if timeout_s is None:
            timeout_s = self._timeout
        deadline = time.monotonic() + timeout_s
        buf = b""
        while time.monotonic() < deadline:
            try:
                ch = self._ser.read(1)
            except serial.SerialException:
                break
            if not ch:
                continue
            buf += ch
            if ch == b"\n":
                break
        line = _strip_ansi(buf.decode("utf-8", errors="replace")).rstrip()
        if line:
            log.debug("RX: %s", line)
        return line

    def drain(self, seconds=0.2):
        """Read and discard all available input for `seconds`."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                chunk = self._ser.read(256)
                if not chunk:
                    time.sleep(0.01)
            except serial.SerialException:
                break

    # ------------------------------------------------------------------
    # Higher-level helpers
    # ------------------------------------------------------------------

    def send_and_expect(self, command, prefix, timeout_s=5.0):
        """
        Send `command` and read lines until one starts with `prefix`.
        Returns the matching line.
        Raises AssertionError with a log of all seen lines if timeout expires.
        """
        self.send(command)
        seen = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.readline(timeout_s=min(0.5, deadline - time.monotonic()))
            if not line:
                continue
            seen.append(line)
            if line.startswith(prefix):
                return line
        raise AssertionError(
            "send_and_expect: command={!r} prefix={!r} timeout={:.1f}s\n"
            "Lines seen:\n{}".format(
                command, prefix, timeout_s, "\n".join("  " + ln for ln in seen)
            )
        )

    def wait_for_line(self, prefix, timeout_s=10.0):
        """
        Read lines (no TX) until one starts with `prefix`.
        Returns the matching line; raises TimeoutError on expiry.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.readline(timeout_s=min(0.5, deadline - time.monotonic()))
            if line and line.startswith(prefix):
                return line
        raise TimeoutError(
            "wait_for_line: prefix={!r} not seen within {:.1f}s".format(
                prefix, timeout_s
            )
        )

    def wait_for_boot(self, timeout_s=20.0):
        """
        Read until a boot-complete indicator appears or timeout expires.
        Raises TimeoutError if the device does not boot in time.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.readline(timeout_s=min(0.5, deadline - time.monotonic()))
            if not line:
                continue
            for pat in _BOOT_PATTERNS:
                if pat in line:
                    log.info("ESP32Serial.wait_for_boot: boot marker=%r", line)
                    return line
        raise TimeoutError(
            "wait_for_boot: device did not boot within {:.1f}s".format(timeout_s)
        )

    def collect_until(self, terminal_prefix, timeout_s=10.0):
        """
        Read lines until one starts with `terminal_prefix` or timeout.
        Returns all collected lines including the terminal line.
        """
        lines = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            line = self.readline(timeout_s=min(0.5, deadline - time.monotonic()))
            if not line:
                continue
            lines.append(line)
            if line.startswith(terminal_prefix):
                break
        return lines

    # ------------------------------------------------------------------
    # Response parsing
    # ------------------------------------------------------------------

    @staticmethod
    def parse_response(line):
        """
        Split a firmware response line into a structured dict.

        Format: STATUS|COMMAND|RESULT[|DATA]

        Returns::

            {"status": "OK", "command": "STATUS", "result": "CURRENT",
             "data": "BT_STATE=CONNECTED,..."}

        Lines that do not match the protocol return ``{"raw": line}``.
        """
        parts = line.split("|", 3)
        if len(parts) < 3:
            return {"raw": line}
        return {
            "status": parts[0],
            "command": parts[1],
            "result": parts[2],
            "data": parts[3] if len(parts) > 3 else "",
        }
