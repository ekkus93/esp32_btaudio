"""
test_pairing.py — PAIR-1 and PAIR-2: Bluetooth pairing tests.

PAIR-1: Basic pairing handshake between ESP32 and laptop.
PAIR-2: Pairing edge cases (rejection, unpair, re-pair).

Pairing protocol:
  TX: PAIR <MAC>
  RX: OK|PAIR|INITIATED
  RX: EVENT|PAIR|CONFIRM|<mac>,<passkey>   (SSP numeric comparison)
  TX: CONFIRM_PIN 1
  RX: OK|CONFIRM_PIN|ACCEPTED|<mac>
  RX: EVENT|PAIR|SUCCESS|<mac>
"""

import time

import pytest

from conftest import ESP32_MAC, LAPTOP_MAC

pytestmark = pytest.mark.laptop_bt

PAIR_TIMEOUT_S = 30.0
CONNECT_NAME_TOTAL_TIMEOUT_S = 80.0  # scan (~20s) + pair + connect


def _do_pair(esp32):
    """
    Drive the full SSP pairing handshake from the ESP32 side.

    Sends PAIR <LAPTOP_MAC>, handles the CONFIRM event, and waits for SUCCESS.
    Raises pytest.fail on any failure.
    """
    esp32.drain(0.1)
    line = esp32.send_and_expect(
        "PAIR {}".format(LAPTOP_MAC), "OK|PAIR|INITIATED", timeout_s=5.0
    )
    assert "OK|PAIR|INITIATED" in line, "PAIR did not initiate: {}".format(line)

    deadline = time.monotonic() + PAIR_TIMEOUT_S
    while time.monotonic() < deadline:
        try:
            rx = esp32.wait_for_line("EVENT|PAIR|", timeout_s=5.0)
        except TimeoutError:
            continue
        if "CONFIRM" in rx:
            esp32.send_and_expect("CONFIRM_PIN 1", "OK|CONFIRM_PIN|ACCEPTED", timeout_s=5.0)
        elif "SUCCESS" in rx:
            # Disconnect the A2DP link; leave device bonded but not connected.
            try:
                esp32.drain(0.1)
                esp32.send_and_expect("DISCONNECT", "DISCONNECT|", timeout_s=5.0)
            except Exception:
                pass
            time.sleep(2.0)
            return
        elif "FAILED" in rx:
            pytest.fail("Pairing failed: {}".format(rx))
    pytest.fail("Pairing did not complete within {:.0f}s".format(PAIR_TIMEOUT_S))


def _collect_paired_list(esp32):
    """Send PAIRED and return all collected lines (INFO items + COUNT terminal)."""
    esp32.drain(0.1)
    esp32.send("PAIRED")
    return esp32.collect_until("OK|PAIRED|COUNT|", timeout_s=5.0)


def _run_scan(esp32, collect_s=25.0):
    """Run a SCAN and return (result_lines, completed)."""
    esp32.drain(0.1)
    esp32.send_and_expect("SCAN", "OK|SCAN|STARTED", timeout_s=5.0)
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


class TestPairing:

    def test_pair_initiated_from_esp32_succeeds(self, esp32, laptop_bt_adapter,
                                                clean_pair_state):
        """Full over-the-air pairing handshake succeeds; both sides show device as paired."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            _do_pair(esp32)
        finally:
            laptop_bt_adapter.set_discoverable(False)
        assert laptop_bt_adapter.is_paired(ESP32_MAC), (
            "Laptop does not show ESP32 {} as paired after handshake".format(ESP32_MAC)
        )

    def test_paired_command_lists_laptop_after_pairing(
        self, esp32, laptop_bt_adapter, clean_pair_state
    ):
        """After pairing, PAIRED command includes the laptop MAC."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            _do_pair(esp32)
        finally:
            laptop_bt_adapter.set_discoverable(False)

        lines = _collect_paired_list(esp32)
        all_text = "\n".join(lines)
        assert LAPTOP_MAC.lower() in all_text.lower(), (
            "Laptop MAC {} not found in PAIRED output:\n{}".format(LAPTOP_MAC, all_text)
        )
        count_line = next((ln for ln in lines if "OK|PAIRED|COUNT|" in ln), "")
        assert "COUNT|0" not in count_line, (
            "PAIRED returned COUNT=0 after pairing: {}".format(count_line)
        )

    def test_pair_result_persists_in_status(self, esp32, laptop_bt_adapter, clean_pair_state):
        """After pairing, STATUS shows PAIRED_COUNT >= 1."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            _do_pair(esp32)
        finally:
            laptop_bt_adapter.set_discoverable(False)

        esp32.drain(0.1)
        line = esp32.send_and_expect("STATUS", "OK|STATUS|CURRENT|", timeout_s=5.0)
        assert "PAIRED_COUNT=0" not in line, (
            "STATUS shows PAIRED_COUNT=0 after pairing: {}".format(line)
        )

    @pytest.mark.slow
    def test_pair_by_name_succeeds(self, esp32, laptop_bt_adapter, clean_pair_state):
        """CONNECT_NAME arisu discovers the laptop by name, pairs, and connects."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            # Populate discovered_devices so bt_connect_by_name can resolve the name
            result_lines, completed = _run_scan(esp32)
            assert completed, "Scan did not complete before CONNECT_NAME test"
            found = any(LAPTOP_MAC.lower() in ln.lower() for ln in result_lines)
            assert found, (
                "Laptop not found in scan — cannot test CONNECT_NAME: {}".format(
                    result_lines
                )
            )

            esp32.drain(0.1)
            line = esp32.send_and_expect(
                "CONNECT_NAME arisu", "OK|CONNECT_NAME|INITIATED", timeout_s=5.0
            )
            assert "OK|CONNECT_NAME|INITIATED" in line

            # Handle pairing events that follow the connection attempt
            deadline = time.monotonic() + CONNECT_NAME_TOTAL_TIMEOUT_S
            paired = False
            while time.monotonic() < deadline:
                try:
                    rx = esp32.wait_for_line("EVENT|PAIR|", timeout_s=5.0)
                except TimeoutError:
                    if laptop_bt_adapter.is_connected(ESP32_MAC):
                        break
                    continue
                if "CONFIRM" in rx:
                    try:
                        esp32.send_and_expect(
                            "CONFIRM_PIN 1", "OK|CONFIRM_PIN|ACCEPTED", timeout_s=5.0
                        )
                    except AssertionError:
                        pass
                elif "SUCCESS" in rx:
                    paired = True
                    break
                elif "FAILED" in rx:
                    pytest.fail("CONNECT_NAME pairing failed: {}".format(rx))

            connected = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=20.0)
            assert paired or connected, (
                "CONNECT_NAME: pairing/connection did not complete"
            )
        finally:
            laptop_bt_adapter.set_discoverable(False)


class TestPairingEdgeCases:

    def test_pair_rejection_returns_failed(self, esp32, laptop_bt_adapter, clean_pair_state):
        """Pairing to an unreachable device yields EVENT|PAIR|FAILED.

        Uses a non-existent MAC so the BT page request times out.  The A2DP
        DISCONNECTED callback fires, which triggers the firmware's pending-
        pairing cleanup path and emits EVENT|PAIR|FAILED.
        """
        DEAD_MAC = "DE:AD:BE:EF:CA:FE"
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "PAIR {}".format(DEAD_MAC), "OK|PAIR|INITIATED", timeout_s=5.0
        )
        assert "OK|PAIR|INITIATED" in line

        deadline = time.monotonic() + PAIR_TIMEOUT_S
        got_failed = False
        while time.monotonic() < deadline:
            try:
                rx = esp32.wait_for_line("EVENT|PAIR|", timeout_s=5.0)
            except TimeoutError:
                continue
            if "FAILED" in rx:
                got_failed = True
                break
            elif "SUCCESS" in rx:
                pytest.fail("Pairing to non-existent device unexpectedly succeeded")
        assert got_failed, "Expected EVENT|PAIR|FAILED for unreachable device"

    def test_unpair_removes_device_from_esp32_list(
        self, esp32, laptop_bt_adapter, clean_pair_state
    ):
        """After UNPAIR, PAIRED no longer lists the removed MAC."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            _do_pair(esp32)
        finally:
            laptop_bt_adapter.set_discoverable(False)

        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "UNPAIR {}".format(LAPTOP_MAC), "OK|UNPAIR|REMOVED", timeout_s=5.0
        )
        assert "OK|UNPAIR|REMOVED" in line

        lines = _collect_paired_list(esp32)
        all_text = "\n".join(lines)
        assert LAPTOP_MAC.lower() not in all_text.lower(), (
            "Laptop MAC still in PAIRED list after UNPAIR:\n{}".format(all_text)
        )

    def test_unpair_all_clears_list(self, esp32, laptop_bt_adapter, clean_pair_state):
        """UNPAIR_ALL returns SUCCESS and PAIRED shows COUNT=0."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            _do_pair(esp32)
        finally:
            laptop_bt_adapter.set_discoverable(False)

        esp32.drain(0.1)
        line = esp32.send_and_expect("UNPAIR_ALL", "OK|UNPAIR_ALL|SUCCESS", timeout_s=5.0)
        assert "OK|UNPAIR_ALL|SUCCESS" in line

        lines = _collect_paired_list(esp32)
        count_line = next((ln for ln in lines if "OK|PAIRED|COUNT|" in ln), "")
        assert "COUNT|0" in count_line, (
            "Expected COUNT=0 after UNPAIR_ALL, got: {}".format(count_line)
        )

    def test_second_pair_to_same_device_succeeds(
        self, esp32, laptop_bt_adapter, clean_pair_state
    ):
        """Pair → unpair → pair again: both pairing attempts succeed."""
        laptop_bt_adapter.set_discoverable(True)
        try:
            _do_pair(esp32)
            esp32.drain(0.1)
            esp32.send_and_expect(
                "UNPAIR {}".format(LAPTOP_MAC), "OK|UNPAIR|REMOVED", timeout_s=5.0
            )
            laptop_bt_adapter.remove_device(ESP32_MAC)
            time.sleep(1.0)
            _do_pair(esp32)
        finally:
            laptop_bt_adapter.set_discoverable(False)
        assert laptop_bt_adapter.is_paired(ESP32_MAC), (
            "Laptop does not show ESP32 as paired after second pair cycle"
        )
