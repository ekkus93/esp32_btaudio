"""
test_e2e.py — E2E-1: End-to-end lifecycle tests.

Full scenario tests that exercise the complete discover → pair → connect →
stream → disconnect lifecycle, reconnect after simulated range loss, and
boot-time auto-reconnect followed by streaming.
"""

import logging
import re
import time

import pulsectl
import pytest
from gi.repository import GLib

from conftest import ESP32_MAC, LAPTOP_MAC, _glib_drain

log = logging.getLogger(__name__)

pytestmark = pytest.mark.laptop_bt

CONNECT_TIMEOUT_S = 20.0


def _find_bt_source(esp32_mac, timeout_s=15.0):
    """Poll PulseAudio for a Bluetooth source/sink matching the ESP32 MAC."""
    mac_lower = esp32_mac.upper().replace(":", "_").lower()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with pulsectl.Pulse("e2e-bt-test") as pulse:
                for obj in list(pulse.sink_list()) + list(pulse.source_list()):
                    if "bluez" in obj.name.lower() or mac_lower in obj.name.lower():
                        return obj
        except Exception as exc:
            log.warning("_find_bt_source: %s", exc)
        time.sleep(0.5)
    return None


def _parse_audio_status(line):
    """Return dict of key=value pairs from an OK|AUDIO_STATUS|CURRENT|... line."""
    m = re.search(r"OK\|AUDIO_STATUS\|CURRENT\|(.+)", line)
    if not m:
        return {}
    fields = {}
    for part in m.group(1).split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            fields[k.strip()] = v.strip()
    return fields


def _pair_from_esp32(esp32, timeout_s=30.0):
    """
    Drive full SSP pairing handshake: PAIR LAPTOP_MAC → CONFIRM_PIN 1 → SUCCESS.
    Disconnects the auto-connected A2DP link at the end.
    Raises pytest.fail on failure or timeout.
    """
    esp32.drain(0.1)
    line = esp32.send_and_expect(
        "PAIR {}".format(LAPTOP_MAC), "OK|PAIR|INITIATED", timeout_s=5.0
    )
    assert "OK|PAIR|INITIATED" in line, "PAIR did not initiate: {}".format(line)

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            rx = esp32.wait_for_line("EVENT|PAIR|", timeout_s=5.0)
        except TimeoutError:
            continue
        if "CONFIRM" in rx:
            esp32.send_and_expect("CONFIRM_PIN 1", "OK|CONFIRM_PIN|ACCEPTED", timeout_s=5.0)
        elif "SUCCESS" in rx:
            # Disconnect any auto-connection; leave device bonded but not connected.
            try:
                esp32.drain(0.1)
                esp32.send_and_expect("DISCONNECT", "DISCONNECT|", timeout_s=5.0)
            except Exception:
                pass
            time.sleep(2.0)
            return
        elif "FAILED" in rx:
            pytest.fail("Pairing failed: {}".format(rx))
    pytest.fail("Pairing did not complete within {:.0f}s".format(timeout_s))


class TestE2E:

    @pytest.mark.slow
    def test_full_discovery_pair_connect_stream_disconnect_lifecycle(
        self, esp32, laptop_bt_adapter, clean_pair_state
    ):
        """
        Full lifecycle: SCAN → PAIR → CONNECT → START → control → STOP → DISCONNECT.
        Exercises every major layer from a clean slate without using higher-level fixtures.
        """
        # Re-assert adapter state (BlueZ may reset Discoverable between tests).
        laptop_bt_adapter.set_pairable(True)
        laptop_bt_adapter.set_discoverable(True)
        time.sleep(0.5)

        # Step 1 — Discovery: scan for laptop MAC
        esp32.drain(0.1)
        esp32.send_and_expect("SCAN", "OK|SCAN|STARTED", timeout_s=5.0)
        scan_lines = esp32.collect_until("OK|SCAN|DONE|", timeout_s=25.0)
        combined = " ".join(scan_lines).upper()
        assert LAPTOP_MAC.upper() in combined, (
            "Laptop MAC {} not found in SCAN results".format(LAPTOP_MAC)
        )
        log.info("E2E Step 1: scan found laptop MAC")

        # Step 2 — Pairing: PAIR → CONFIRM_PIN 1 → SUCCESS
        _pair_from_esp32(esp32, timeout_s=30.0)
        log.info("E2E Step 2: pairing succeeded")

        # Step 3 — Connection
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "CONNECT {}".format(LAPTOP_MAC), "OK|CONNECT|", timeout_s=5.0
        )
        assert "INITIATED" in line or "CONNECTED" in line, (
            "CONNECT did not initiate: {}".format(line)
        )
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=CONNECT_TIMEOUT_S)
        assert ok, "ESP32 did not connect to laptop within {:.0f}s".format(
            CONNECT_TIMEOUT_S
        )
        _glib_drain(3.0)
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=10.0)
        assert ok, "A2DP connection dropped after setup"
        time.sleep(1.5)
        log.info("E2E Step 3: A2DP connection established")

        # Step 4 — Streaming: START, verify audio flowing, PulseAudio sink present
        esp32.drain(0.1)
        line = esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        assert "OK|START|STARTED" in line, "START failed: {}".format(line)
        time.sleep(2.0)

        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0
        )
        fields = _parse_audio_status(line)
        engine_bytes = int(fields.get("ENGINE_BYTES", "0"))
        ring_peak = int(fields.get("RING_PEAK", "0"))
        assert engine_bytes > 0 or ring_peak > 0, (
            "Audio not flowing after START — ENGINE_BYTES={} RING_PEAK={}".format(
                engine_bytes, ring_peak
            )
        )

        bt_source = _find_bt_source(ESP32_MAC, timeout_s=15.0)
        assert bt_source is not None, (
            "No PulseAudio BT source for ESP32 {} found".format(ESP32_MAC)
        )
        log.info("E2E Step 4: streaming confirmed, PulseAudio source=%s", bt_source.name)

        # Step 5 — Control: VOLUME / MUTE / UNMUTE
        esp32.drain(0.1)
        line = esp32.send_and_expect("VOLUME 60", "OK|VOLUME|SET|", timeout_s=5.0)
        assert "OK|VOLUME|SET|60" in line, "VOLUME 60 failed: {}".format(line)

        esp32.drain(0.1)
        line = esp32.send_and_expect("MUTE", "OK|MUTE|SET", timeout_s=5.0)
        assert "OK|MUTE|SET" in line, "MUTE failed: {}".format(line)

        esp32.drain(0.1)
        line = esp32.send_and_expect("UNMUTE", "OK|UNMUTE|CLEARED", timeout_s=5.0)
        assert "OK|UNMUTE|CLEARED" in line, "UNMUTE failed: {}".format(line)
        log.info("E2E Step 5: VOLUME/MUTE/UNMUTE all OK")

        # Restore volume to default
        esp32.drain(0.1)
        esp32.send_and_expect("VOLUME 100", "OK|VOLUME|SET|", timeout_s=5.0)

        # Step 6 — Stop streaming
        esp32.drain(0.1)
        line = esp32.send_and_expect("STOP", "OK|STOP|STOPPED", timeout_s=5.0)
        assert "OK|STOP|STOPPED" in line, "STOP failed: {}".format(line)
        log.info("E2E Step 6: streaming stopped")

        # Step 7 — Disconnect
        esp32.drain(0.1)
        line = esp32.send_and_expect("DISCONNECT", "OK|DISCONNECT|", timeout_s=5.0)
        assert "OK|DISCONNECT|" in line, "DISCONNECT failed: {}".format(line)

        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            GLib.MainContext.default().iteration(may_block=False)
            if not laptop_bt_adapter.is_connected(ESP32_MAC):
                break
            time.sleep(0.5)
        assert not laptop_bt_adapter.is_connected(ESP32_MAC), (
            "ESP32 still connected after DISCONNECT in E2E lifecycle"
        )
        log.info("E2E Step 7: disconnected — full lifecycle complete")

    @pytest.mark.slow
    def test_reconnect_after_simulated_range_loss(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """
        Simulate range loss by removing the device from the laptop's BlueZ database
        (forcing disconnect), then reconnect from the ESP32 side.
        Re-pairing is handled automatically by the auto-accept agent.
        """
        assert laptop_bt_adapter.is_connected(ESP32_MAC), (
            "Precondition: ESP32 not connected to laptop"
        )

        # Simulate range loss: removing the device forces an HCI disconnect
        # and deletes the link key from the laptop side.
        laptop_bt_adapter.remove_device(ESP32_MAC)
        time.sleep(2.0)

        # Re-enable pairable so laptop accepts the reconnection and any re-pairing.
        laptop_bt_adapter.set_pairable(True)
        laptop_bt_adapter.set_discoverable(True)
        time.sleep(0.5)

        # Reconnect from ESP32 — will trigger re-pairing because laptop's link
        # key is gone; SimpleAgent auto-accepts RequestConfirmation.
        esp32.drain(0.1)
        esp32.send_and_expect(
            "CONNECT {}".format(LAPTOP_MAC), "OK|CONNECT|", timeout_s=5.0
        )

        # Event loop: handle CONFIRM_PIN if re-pairing occurs; watch for connection.
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            line = esp32.readline(timeout_s=0.3)
            if line:
                log.debug("range-loss rx: %s", line)
                if "EVENT|PAIR|CONFIRM" in line:
                    esp32.drain(0.1)
                    esp32.send_and_expect(
                        "CONFIRM_PIN 1", "OK|CONFIRM_PIN|", timeout_s=5.0
                    )
                elif "EVENT|PAIR|FAILED" in line:
                    pytest.fail("Re-pairing failed after range loss: {}".format(line))
            GLib.MainContext.default().iteration(may_block=False)
            if laptop_bt_adapter.is_connected(ESP32_MAC):
                break

        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=5.0)
        assert ok, "ESP32 did not reconnect to laptop after simulated range loss"
        _glib_drain(3.0)
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=10.0)
        assert ok, "Connection lost after A2DP setup in range-loss reconnect"
        log.info("Reconnected successfully after simulated range loss")

    @pytest.mark.slow
    def test_boot_reconnect_full_sequence(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """
        Pair, connect, stream briefly, disconnect, then verify the device auto-reconnects
        on reboot (using saved LAST_MAC) and resumes streaming.
        """
        assert laptop_bt_adapter.is_connected(ESP32_MAC), (
            "Precondition: ESP32 not connected to laptop"
        )

        # Stream briefly to confirm the link is healthy
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(2.0)
        esp32.drain(0.1)
        esp32.send_and_expect("STOP", "OK|STOP|STOPPED", timeout_s=5.0)

        # Disconnect (LAST_MAC set to LAPTOP_MAC when A2DP CONNECTED fired)
        esp32.drain(0.1)
        esp32.send_and_expect("DISCONNECT", "OK|DISCONNECT|", timeout_s=5.0)
        # Wait for the laptop to fully process the disconnect before rebooting.
        # Without this, the firmware's auto-reconnect attempt (fired on
        # DISCONNECTED) races with RESET and leaves the laptop's AVDTP/L2CAP
        # stack in a half-open state — the boot auto-reconnect then establishes
        # ACL but A2DP never completes.
        ok = laptop_bt_adapter.wait_for_disconnect(ESP32_MAC, timeout_s=15.0)
        if not ok:
            log.warning("Laptop did not see disconnect within 15s; continuing")
        _glib_drain(1.0)

        # Confirm LAST_MAC is set to laptop before reboot
        esp32.drain(0.1)
        line = esp32.send_and_expect("LAST_MAC get", "OK|LAST_MAC|", timeout_s=5.0)
        assert LAPTOP_MAC.upper() in line.upper(), (
            "LAST_MAC not set to laptop MAC before reboot: {}".format(line)
        )
        log.info("LAST_MAC confirmed: %s", line)

        # Reboot and wait for boot complete
        esp32.drain(0.1)
        esp32.send_and_expect("RESET", "OK|RESET|", timeout_s=5.0)
        esp32.wait_for_boot(timeout_s=30.0)

        # Auto-connect should restore A2DP connection within 30s
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=30.0)
        assert ok, "ESP32 did not auto-reconnect to laptop within 30s after reboot"
        _glib_drain(3.0)
        ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=10.0)
        assert ok, "Connection lost after A2DP setup in boot reconnect"
        # Drain serial to let boot-reconnect re-authentication events settle.
        # After auto-reconnect the A2DP stack completes pairing auth async;
        # START returns ERR|START|FAILED if sent before auth finishes.
        esp32.drain(3.0)
        log.info("Auto-reconnect confirmed after reboot")

        # Retry START with backoff — boot reconnect triggers re-authentication
        # that can delay A2DP readiness by several seconds past the ACL connect.
        start_line = None
        start_deadline = time.monotonic() + 20.0
        while time.monotonic() < start_deadline:
            esp32.drain(0.2)
            try:
                start_line = esp32.send_and_expect(
                    "START", "OK|START|STARTED", timeout_s=3.0
                )
                break
            except AssertionError:
                time.sleep(2.0)
        assert start_line is not None and "OK|START|STARTED" in start_line, (
            "START timed out after boot reconnect (A2DP not ready)"
        )

        time.sleep(2.0)
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0
        )
        fields = _parse_audio_status(line)
        engine_bytes = int(fields.get("ENGINE_BYTES", "0"))
        ring_peak = int(fields.get("RING_PEAK", "0"))
        assert engine_bytes > 0 or ring_peak > 0, (
            "Audio not flowing after boot reconnect — "
            "ENGINE_BYTES={} RING_PEAK={}".format(engine_bytes, ring_peak)
        )
        log.info("Boot reconnect full sequence complete — streaming confirmed")
