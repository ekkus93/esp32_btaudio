"""
test_connection.py — CONN-1: Bluetooth connection management tests.

Verifies connect/disconnect lifecycle using a real A2DP link between the
ESP32 and the laptop.  Connection state is confirmed from the laptop side via
BlueZ (is_connected / wait_for_connect) since the STATUS command does not
expose a BT_STATE field.

Commands under test:
  CONNECT <MAC>       → OK|CONNECT|INITIATED
  CONNECT_NAME <name> → OK|CONNECT_NAME|INITIATED
  DISCONNECT          → OK|DISCONNECT|DONE
  STATUS              → OK|STATUS|CURRENT|...,PAIRED_COUNT=N,...
"""

import logging
import subprocess
import time

import pytest
from gi.repository import GLib

from conftest import ESP32_MAC, LAPTOP_MAC, _glib_drain

log = logging.getLogger(__name__)

pytestmark = pytest.mark.laptop_bt

CONNECT_TIMEOUT_S = 20.0
DISCONNECT_TIMEOUT_S = 20.0


def _assert_disconnected(laptop_bt_adapter, timeout_s=DISCONNECT_TIMEOUT_S):
    """Poll until laptop no longer shows ESP32 as connected."""
    deadline = time.monotonic() + timeout_s
    t0 = time.monotonic()
    while time.monotonic() < deadline:
        # Process pending D-Bus signals so BlueZ property changes propagate.
        GLib.MainContext.default().iteration(may_block=False)
        connected = laptop_bt_adapter.is_connected(ESP32_MAC)
        elapsed = time.monotonic() - t0
        log.info("_assert_disconnected: t=%.1fs connected=%s", elapsed, connected)
        if not connected:
            return
        time.sleep(0.5)
    # Collect extra diagnostics on failure.
    try:
        result = subprocess.run(
            ["bluetoothctl", "info", ESP32_MAC],
            capture_output=True, text=True, timeout=5,
        )
        log.warning("bluetoothctl info:\n%s", result.stdout)
    except Exception as exc:
        log.warning("bluetoothctl info failed: %s", exc)
    assert False, (
        "ESP32 {} still connected after {:.0f}s".format(ESP32_MAC, timeout_s)
    )


class TestConnection:

    def test_connect_to_paired_laptop_succeeds(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """CONNECT to a paired device establishes an A2DP link."""
        assert laptop_bt_adapter.is_connected(ESP32_MAC), (
            "Laptop does not show ESP32 {} as connected".format(ESP32_MAC)
        )

    def test_disconnect_returns_to_disconnected_state(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """DISCONNECT tears down the A2DP link on both sides."""
        # A2DP media errors flood the serial buffer while connected (no audio source
        # active); drain before sending DISCONNECT so the response isn't missed.
        esp32.drain(0.1)
        line = esp32.send_and_expect("DISCONNECT", "OK|DISCONNECT|", timeout_s=5.0)
        assert "OK|DISCONNECT|" in line
        _assert_disconnected(laptop_bt_adapter)

    def test_reconnect_after_explicit_disconnect(
        self, esp32, laptop_bt_adapter, paired_state
    ):
        """Connect, disconnect, then connect again — guards against state-machine wedge."""
        # First connection
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "CONNECT {}".format(LAPTOP_MAC), "OK|CONNECT|", timeout_s=5.0
        )
        assert "INITIATED" in line or "CONNECTED" in line, (
            "First CONNECT did not initiate: {}".format(line)
        )
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=CONNECT_TIMEOUT_S)
        assert ok, "First connection did not establish within {:.0f}s".format(CONNECT_TIMEOUT_S)
        _glib_drain(3.0)  # drain AuthorizeService so A2DP setup completes
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=10.0)
        assert ok, "First connection dropped after A2DP setup"
        time.sleep(1.5)   # let ESP32 CONNECTED callback fire

        # Disconnect
        esp32.drain(0.1)
        esp32.send_and_expect("DISCONNECT", "OK|DISCONNECT|", timeout_s=5.0)
        _assert_disconnected(laptop_bt_adapter)
        time.sleep(2.0)

        # Second connection — must succeed without re-pairing
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "CONNECT {}".format(LAPTOP_MAC), "OK|CONNECT|", timeout_s=5.0
        )
        assert "INITIATED" in line or "CONNECTED" in line, (
            "Second CONNECT did not initiate: {}".format(line)
        )
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=CONNECT_TIMEOUT_S)
        assert ok, "Second connection did not establish within {:.0f}s".format(CONNECT_TIMEOUT_S)

    @pytest.mark.slow
    def test_connect_by_name_resolves_to_correct_device(
        self, esp32, laptop_bt_adapter, paired_state
    ):
        """CONNECT_NAME arisu connects to the laptop, not any other device."""
        # PAIR connects directly without scanning, so paired_devices has no name.
        # BlueZ may reset Discoverable=False after the pairing connection — re-enable
        # so the ESP32 inquiry scan can find the laptop by name.
        laptop_bt_adapter.set_discoverable(True)
        # Scan first to populate discovered_devices so CONNECT_NAME can resolve
        # the name "arisu" to the laptop's MAC.
        esp32.drain(0.1)
        esp32.send_and_expect("SCAN", "OK|SCAN|STARTED", timeout_s=5.0)
        esp32.wait_for_line("OK|SCAN|DONE|", timeout_s=30.0)

        line = esp32.send_and_expect(
            "CONNECT_NAME arisu", "OK|CONNECT_NAME|INITIATED", timeout_s=5.0
        )
        assert "OK|CONNECT_NAME|INITIATED" in line

        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=CONNECT_TIMEOUT_S)
        assert ok, (
            "CONNECT_NAME arisu did not result in laptop connection within {:.0f}s".format(
                CONNECT_TIMEOUT_S
            )
        )
        assert laptop_bt_adapter.is_connected(ESP32_MAC), (
            "Laptop does not confirm ESP32 {} connected after CONNECT_NAME".format(ESP32_MAC)
        )

    def test_status_shows_paired_count_when_connected(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """While connected, STATUS PAIRED_COUNT reflects the bonded device count."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("STATUS", "OK|STATUS|CURRENT|", timeout_s=5.0)
        assert "PAIRED_COUNT=0" not in line, (
            "STATUS shows PAIRED_COUNT=0 while connected — expected >= 1: {}".format(line)
        )
