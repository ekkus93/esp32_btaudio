"""
test_discovery.py — DISC-1: ESP32 Bluetooth discovery tests.

Verify that the ESP32 SCAN command discovers the laptop adapter when it is
discoverable, and that the result contains the expected MAC and device name.

Protocol (after firmware fix in bt_scan.c):
  TX: SCAN
  RX: OK|SCAN|STARTED            — scan initiated
  RX: INFO|SCAN|RESULT|<mac>,<name>  — one per discovered device (async)
  RX: OK|SCAN|DONE|count=<n>    — terminal marker when inquiry window closes
"""

import time

import pytest

from conftest import LAPTOP_MAC

pytestmark = pytest.mark.laptop_bt

# Laptop BlueZ device name as reported by bluetoothctl / BlueZ
LAPTOP_NAME = "arisu"

# Slack over the 10-second ESP-IDF inquiry window
SCAN_TIMEOUT_S = 25.0


def _run_scan(esp32, collect_s=SCAN_TIMEOUT_S):
    """
    Send SCAN and collect all INFO|SCAN|RESULT| lines until
    OK|SCAN|DONE arrives or the timeout expires.

    Returns (result_lines, completed) where result_lines is the list of
    INFO|SCAN|RESULT| lines and completed is True if DONE was seen.
    """
    esp32.drain(0.1)
    line = esp32.send_and_expect("SCAN", "OK|SCAN|STARTED", timeout_s=5.0)
    assert "OK|SCAN|STARTED" in line, "Scan did not start: {}".format(line)

    result_lines = []
    completed = False
    deadline = time.monotonic() + collect_s
    while time.monotonic() < deadline:
        rx = esp32.readline(timeout_s=1.0)
        if not rx:
            continue
        if "INFO|SCAN|RESULT|" in rx:
            result_lines.append(rx)
        elif rx.startswith("OK|SCAN|DONE"):
            completed = True
            break
    return result_lines, completed


class TestDiscovery:

    def test_laptop_discoverable_appears_in_scan(self, esp32, laptop_bt_adapter):
        """ESP32 SCAN finds the laptop MAC when the laptop is discoverable."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            result_lines, completed = _run_scan(esp32)
            assert completed, "SCAN did not emit OK|SCAN|DONE within {}s".format(
                SCAN_TIMEOUT_S
            )
            found = any(LAPTOP_MAC.lower() in ln.lower() for ln in result_lines)
            assert found, (
                "Laptop MAC {} not found in SCAN results.\nSeen:\n{}".format(
                    LAPTOP_MAC,
                    "\n".join("  " + ln for ln in result_lines) or "  (none)",
                )
            )
        finally:
            laptop_bt_adapter.set_discoverable(False)

    def test_scan_result_contains_device_name(self, esp32, laptop_bt_adapter):
        """SCAN result for the laptop MAC includes the device name 'arisu'."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            result_lines, completed = _run_scan(esp32)
            assert completed, "SCAN did not emit OK|SCAN|DONE within {}s".format(
                SCAN_TIMEOUT_S
            )
            mac_line = next(
                (ln for ln in result_lines if LAPTOP_MAC.lower() in ln.lower()), None
            )
            assert mac_line is not None, (
                "Laptop MAC {} not found in SCAN results".format(LAPTOP_MAC)
            )
            assert LAPTOP_NAME.lower() in mac_line.lower(), (
                "Device name '{}' not found in scan result: {!r}".format(
                    LAPTOP_NAME, mac_line
                )
            )
        finally:
            laptop_bt_adapter.set_discoverable(False)

    def test_scan_not_discoverable_does_not_find_laptop(self, esp32, laptop_bt_adapter):
        """When the laptop is not discoverable, SCAN must not return its MAC."""
        laptop_bt_adapter.set_discoverable(False)
        # Give BlueZ time to propagate the non-discoverable mode to the HCI layer
        time.sleep(5.0)

        result_lines, _ = _run_scan(esp32)
        found = any(LAPTOP_MAC.lower() in ln.lower() for ln in result_lines)
        assert not found, (
            "Laptop MAC {} unexpectedly found in SCAN while not discoverable.\n"
            "Lines: {}".format(LAPTOP_MAC, result_lines)
        )

    def test_scan_completes_within_timeout(self, esp32, laptop_bt_adapter):
        """SCAN command emits OK|SCAN|DONE within the expected time window."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            t0 = time.monotonic()
            _, completed = _run_scan(esp32)
            elapsed = time.monotonic() - t0
            assert completed, (
                "SCAN did not emit OK|SCAN|DONE within {:.1f}s".format(elapsed)
            )
            assert elapsed < SCAN_TIMEOUT_S, (
                "SCAN took {:.1f}s — exceeded {}s budget".format(
                    elapsed, SCAN_TIMEOUT_S
                )
            )
        finally:
            laptop_bt_adapter.set_discoverable(False)
