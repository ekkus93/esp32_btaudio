"""
conftest.py — pytest fixtures for the laptop_bt integration test suite.

Session-scoped fixtures open the laptop Bluetooth adapter and the ESP32
serial port once for the whole run.  Function-scoped fixtures guarantee a
clean pairing slate before and after every test.
"""

import logging
import subprocess
import time
from pathlib import Path

import pytest
from gi.repository import GLib

from laptop_bt import LaptopBT
from esp32_serial import ESP32Serial

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Hardware constants
# ---------------------------------------------------------------------------

#: Laptop adapter MAC (confirmed from `bluetoothctl show`)
LAPTOP_MAC = "E8:FB:1C:25:E4:C2"

#: ESP32 Classic BT MAC (from boot log: "Bluetooth MAC: a0:b7:65:2b:e6:5e")
#: Note: base/WiFi MAC is 5C; Classic BT MAC = base + 2 = 5E
ESP32_MAC = "A0:B7:65:2B:E6:5E"

#: Serial port the ESP32 is connected to
ESP32_PORT = "/dev/ttyUSB0"


# ---------------------------------------------------------------------------
# Hardware presence guard (INFRA-4e)
# Skip entire session when hardware is absent so the suite is CI-safe.
# ---------------------------------------------------------------------------

def _hardware_present():
    if not Path(ESP32_PORT).exists():
        return False, "No ESP32 on {}".format(ESP32_PORT)
    try:
        result = subprocess.run(
            ["bluetoothctl", "show"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode != 0 or LAPTOP_MAC not in result.stdout.upper():
            return False, "bluetoothctl show failed or adapter {} not found".format(
                LAPTOP_MAC
            )
    except Exception as exc:
        return False, "bluetoothctl unavailable: {}".format(exc)
    return True, None


_HW_OK, _HW_REASON = _hardware_present()


def pytest_configure(config):  # noqa: ARG001
    if not _HW_OK:
        log.warning("laptop_bt suite skipped: %s", _HW_REASON)


def pytest_collection_modifyitems(config, items):  # noqa: ARG001
    if not _HW_OK:
        skip = pytest.mark.skip(reason="Hardware not available: {}".format(_HW_REASON))
        for item in items:
            item.add_marker(skip)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _glib_drain(seconds):
    """Process pending GLib events for `seconds` (50ms tick, may_block=False)."""
    ctx = GLib.MainContext.default()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ctx.iteration(may_block=False)
        time.sleep(0.05)


# ---------------------------------------------------------------------------
# Session-scoped fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def laptop_bt_adapter():
    """Make the laptop adapter discoverable/pairable for the whole test session."""
    with LaptopBT(LAPTOP_MAC) as bt:
        log.info("conftest: LaptopBT session started — adapter %s", LAPTOP_MAC)
        yield bt
    log.info("conftest: LaptopBT session ended")


@pytest.fixture(scope="session")
def esp32():
    """Open the ESP32 serial connection once for the whole test session."""
    with ESP32Serial(ESP32_PORT) as dev:
        log.info("conftest: ESP32Serial session started on %s", ESP32_PORT)
        yield dev
    log.info("conftest: ESP32Serial session ended")


# ---------------------------------------------------------------------------
# Function-scoped fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def clean_pair_state(esp32, laptop_bt_adapter):
    """
    Guarantee a clean pairing slate before and after each test.

    Before: unpairs all devices from ESP32 NVS; removes ESP32 from laptop.
    After:  same cleanup so failures don't contaminate the next test.
    """

    def _cleanup():
        # Disconnect first so UNPAIR_ALL works cleanly and autostart stops.
        try:
            esp32.drain(0.1)
            esp32.send_and_expect("DISCONNECT", "DISCONNECT|", timeout_s=5.0)
        except Exception:
            pass
        # Clear LAST_MAC so the autostart reconnect loop has no target.
        try:
            esp32.drain(0.1)
            esp32.send_and_expect("LAST_MAC clear", "OK|LAST_MAC|", timeout_s=5.0)
        except Exception as exc:
            log.warning("clean_pair_state: LAST_MAC clear failed — %s", exc)
        try:
            esp32.drain(0.1)
            esp32.send_and_expect("UNPAIR_ALL", "OK|UNPAIR_ALL|", timeout_s=5.0)
        except Exception as exc:
            log.warning("clean_pair_state: UNPAIR_ALL failed — %s", exc)
        try:
            laptop_bt_adapter.remove_device(ESP32_MAC)
        except Exception as exc:
            log.warning("clean_pair_state: remove_device failed — %s", exc)
        # Let the BT stack fully process the HCI disconnect and device removal before
        # the next operation. After a real pairing cycle BlueZ needs extra time.
        time.sleep(5.0)

    _cleanup()
    yield
    _cleanup()


@pytest.fixture()
def paired_state(esp32, laptop_bt_adapter, clean_pair_state):
    """
    Fixture that ensures the ESP32 and laptop are paired before the test
    and cleaned up after.  Uses the standard pairing handshake.

    Yields the esp32 driver so tests can reuse it without re-importing.
    """
    laptop_bt_adapter.set_discoverable(True)
    esp32.drain(0.1)

    # Initiate pairing from the ESP32
    esp32.send_and_expect("PAIR {}".format(LAPTOP_MAC), "OK|PAIR|", timeout_s=5.0)

    # Handle the confirmation event (laptop agent auto-accepts)
    deadline_ts = time.monotonic() + 30.0
    while time.monotonic() < deadline_ts:
        try:
            line = esp32.wait_for_line("EVENT|PAIR|", timeout_s=5.0)
        except TimeoutError:
            continue
        if "CONFIRM" in line:
            esp32.send_and_expect("CONFIRM_PIN 1", "OK|CONFIRM_PIN|", timeout_s=5.0)
        elif "SUCCESS" in line:
            break
        elif "FAILED" in line:
            pytest.fail("Pairing failed during paired_state fixture: {}".format(line))

    # Disconnect the A2DP link so the device is bonded but not connected.
    # connected_state will CONNECT afterwards when it needs a live link.
    try:
        esp32.drain(0.1)
        esp32.send_and_expect("DISCONNECT", "DISCONNECT|", timeout_s=5.0)
    except Exception:
        pass
    time.sleep(2.0)

    yield esp32

    # teardown handled by clean_pair_state


@pytest.fixture()
def connected_state(paired_state, laptop_bt_adapter):
    """
    Fixture that pairs and connects before the test, disconnects after.

    Yields the esp32 driver.
    """
    esp32 = paired_state
    esp32.send_and_expect("CONNECT {}".format(LAPTOP_MAC), "OK|CONNECT|", timeout_s=5.0)
    ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=20.0)
    if not ok:
        pytest.fail("connected_state: ESP32 did not connect to laptop within 20s")
    # BlueZ fires Connected=True on ACL establishment, but AuthorizeService for
    # A2DP may still be pending in the GLib event queue.  A plain time.sleep()
    # does not drain GLib, so AuthorizeService would only fire later (when
    # _assert_disconnected calls ctx.iteration), by which time DISCONNECT has
    # already been sent while A2DP is in OPENING state (unhandled by ESP32).
    # Drain GLib so A2DP setup completes, then re-confirm the link is stable
    # (the initial connection can briefly drop and be re-established), then
    # sleep for the ESP32's A2DP CONNECTED callback to fire.
    _glib_drain(seconds=3.0)
    ok = laptop_bt_adapter.wait_for_connect(ESP32_MAC, timeout_s=10.0)
    if not ok:
        pytest.fail("connected_state: connection lost after A2DP setup")
    time.sleep(1.5)
    yield esp32
    # Best-effort disconnect
    try:
        esp32.send_and_expect("DISCONNECT", "OK|DISCONNECT|", timeout_s=5.0)
    except Exception:
        pass
