"""
test_autoconnect.py — CONN-2: Auto-reconnect on boot tests.

Verifies that the ESP32 persists the last-connected MAC to NVS and
automatically reconnects to it on reboot when autostart is enabled.

Commands under test:
  LAST_MAC get            → OK|LAST_MAC|<MAC>  or  OK|LAST_MAC|NONE
  LAST_MAC clear          → OK|LAST_MAC|CLEARED
  RESET                   → OK|RESET|REBOOTING  (triggers ESP32 reboot)
  AUDIO_AUTOSTART on/off  → OK|AUDIO_AUTOSTART|ENABLED/DISABLED|Restart required to apply
"""

import logging
import time

import pytest

from conftest import ESP32_MAC, LAPTOP_MAC, _glib_drain

log = logging.getLogger(__name__)

pytestmark = pytest.mark.laptop_bt

BOOT_TIMEOUT_S = 30.0
AUTOCONNECT_TIMEOUT_S = 30.0
NO_CONNECT_WAIT_S = 20.0


class TestAutoConnect:

    def test_last_mac_saved_after_connection(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """After establishing an A2DP link, LAST_MAC get returns the laptop MAC."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("LAST_MAC get", "OK|LAST_MAC|", timeout_s=5.0)
        assert LAPTOP_MAC.upper() in line.upper(), (
            "LAST_MAC did not contain laptop MAC {}: {}".format(LAPTOP_MAC, line)
        )

    def test_last_mac_none_when_never_connected(
        self, esp32, laptop_bt_adapter, clean_pair_state
    ):
        """After LAST_MAC clear, LAST_MAC get returns NONE."""
        esp32.drain(0.1)
        esp32.send_and_expect("LAST_MAC clear", "OK|LAST_MAC|", timeout_s=5.0)
        esp32.drain(0.1)
        line = esp32.send_and_expect("LAST_MAC get", "OK|LAST_MAC|NONE", timeout_s=5.0)
        assert "NONE" in line, "LAST_MAC get did not return NONE: {}".format(line)

    @pytest.mark.slow
    def test_autostart_reconnects_on_reboot(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """After an A2DP connection, reboot causes ESP32 to auto-reconnect to last device."""
        # connected_state establishes A2DP; the CONNECTED event writes LAST_MAC to NVS.
        # Verify the precondition before RESET.
        esp32.drain(0.1)
        line = esp32.send_and_expect("LAST_MAC get", "OK|LAST_MAC|", timeout_s=5.0)
        assert LAPTOP_MAC.upper() in line.upper(), (
            "LAST_MAC not set before RESET — precondition failed: {}".format(line)
        )

        # Reboot the ESP32.
        esp32.drain(0.1)
        esp32.send_and_expect("RESET", "OK|RESET|REBOOTING", timeout_s=5.0)
        esp32.wait_for_boot(timeout_s=BOOT_TIMEOUT_S)
        esp32.drain(0.2)  # clear remaining boot messages

        # Auto-reconnect must fire without any explicit CONNECT command.
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=AUTOCONNECT_TIMEOUT_S)
        assert ok, (
            "ESP32 did not auto-reconnect within {:.0f}s after reboot".format(
                AUTOCONNECT_TIMEOUT_S
            )
        )
        # Drain GLib so AuthorizeService completes and A2DP reaches CONNECTED
        # state before teardown sends DISCONNECT.
        _glib_drain(3.0)
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=10.0)
        assert ok, "Auto-reconnect connection dropped after A2DP setup"
        time.sleep(1.5)

    @pytest.mark.slow
    def test_no_autostart_when_last_mac_cleared(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """When LAST_MAC is cleared before reboot, ESP32 does not auto-reconnect."""
        # Clear the persisted MAC so auto-connect has no target.
        esp32.drain(0.1)
        esp32.send_and_expect("LAST_MAC clear", "OK|LAST_MAC|", timeout_s=5.0)
        # Verify the clear took effect.
        esp32.drain(0.1)
        line = esp32.send_and_expect("LAST_MAC get", "OK|LAST_MAC|NONE", timeout_s=5.0)
        assert "NONE" in line, "LAST_MAC not cleared before RESET: {}".format(line)

        # Reboot.
        esp32.drain(0.1)
        esp32.send_and_expect("RESET", "OK|RESET|REBOOTING", timeout_s=5.0)
        esp32.wait_for_boot(timeout_s=BOOT_TIMEOUT_S)
        esp32.drain(0.2)

        # Wait the full no-connect window, then confirm still disconnected.
        time.sleep(NO_CONNECT_WAIT_S)
        _glib_drain(1.0)  # flush any pending D-Bus state changes

        assert not laptop_bt_adapter.is_connected(ESP32_MAC), (
            "ESP32 {} unexpectedly connected after reboot with no LAST_MAC".format(ESP32_MAC)
        )

    @pytest.mark.slow
    def test_autostart_disabled_prevents_reconnect_on_reboot(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """With AUDIO_AUTOSTART off, reboot skips auto-reconnect even when LAST_MAC is set."""
        # connected_state set LAST_MAC in NVS via A2DP CONNECTED.
        # Disable autostart — NVS write; setting only takes effect after restart.
        esp32.drain(0.1)
        esp32.send_and_expect("AUDIO_AUTOSTART off", "OK|AUDIO_AUTOSTART|", timeout_s=5.0)

        try:
            # Reboot — boots with autostart=off AND LAST_MAC=laptop MAC.
            esp32.drain(0.1)
            esp32.send_and_expect("RESET", "OK|RESET|REBOOTING", timeout_s=5.0)
            esp32.wait_for_boot(timeout_s=BOOT_TIMEOUT_S)
            esp32.drain(0.2)

            # Auto-reconnect must be suppressed.
            time.sleep(NO_CONNECT_WAIT_S)
            _glib_drain(1.0)

            assert not laptop_bt_adapter.is_connected(ESP32_MAC), (
                "ESP32 {} auto-reconnected despite AUDIO_AUTOSTART being disabled".format(
                    ESP32_MAC
                )
            )
        finally:
            # Restore AUDIO_AUTOSTART so future test runs are not affected.
            esp32.drain(0.1)
            try:
                esp32.send_and_expect(
                    "AUDIO_AUTOSTART on", "OK|AUDIO_AUTOSTART|", timeout_s=5.0
                )
            except Exception as exc:
                log.warning(
                    "test_autostart_disabled: failed to restore AUDIO_AUTOSTART — %s", exc
                )
