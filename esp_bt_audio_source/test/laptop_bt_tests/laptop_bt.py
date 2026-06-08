"""
laptop_bt.py — BlueZ D-Bus controller for laptop-side Bluetooth operations.

Wraps the BlueZ 5 D-Bus API (via pydbus) to make the laptop adapter
discoverable/pairable, register a programmable pairing agent, and manage
paired devices.  Used by the laptop_bt_tests fixture layer (conftest.py).
"""

import logging
import time
from gi.repository import GLib
import pydbus


class _BlueZRejected(Exception):
    """Raised by SimpleAgent methods to signal org.bluez.Error.Rejected."""

    name = "org.bluez.Error.Rejected"


log = logging.getLogger(__name__)

BLUEZ_SERVICE = "org.bluez"
ADAPTER1_IFACE = "org.bluez.Adapter1"
DEVICE1_IFACE = "org.bluez.Device1"
AGENT_MANAGER1_IFACE = "org.bluez.AgentManager1"
OBJECT_MANAGER_IFACE = "org.freedesktop.DBus.ObjectManager"

AGENT_PATH = "/test/laptop_bt_agent"

# A2DP Sink service UUID
A2DP_SINK_UUID = "0000110b-0000-1000-8000-00805f9b34fb"


def _mac_to_dbus_suffix(mac):
    """Convert 'AA:BB:CC:DD:EE:FF' → 'AA_BB_CC_DD_EE_FF'."""
    return mac.upper().replace(":", "_")


class SimpleAgent:
    """
    Minimal BlueZ Agent1 implementation registered on the system D-Bus.
    auto_accept=True: silently confirm all pairing requests.
    auto_accept=False: reject all RequestConfirmation calls (for negative tests).
    """

    AGENT1_XML = """
    <node>
      <interface name="org.bluez.Agent1">
        <method name="RequestPinCode">
          <arg direction="in"  type="o" name="device"/>
          <arg direction="out" type="s" name="pincode"/>
        </method>
        <method name="DisplayPinCode">
          <arg direction="in"  type="o" name="device"/>
          <arg direction="in"  type="s" name="pincode"/>
        </method>
        <method name="RequestPasskey">
          <arg direction="in"  type="o" name="device"/>
          <arg direction="out" type="u" name="passkey"/>
        </method>
        <method name="DisplayPasskey">
          <arg direction="in"  type="o" name="device"/>
          <arg direction="in"  type="u" name="passkey"/>
          <arg direction="in"  type="q" name="entered"/>
        </method>
        <method name="RequestConfirmation">
          <arg direction="in"  type="o" name="device"/>
          <arg direction="in"  type="u" name="passkey"/>
        </method>
        <method name="RequestAuthorization">
          <arg direction="in"  type="o" name="device"/>
        </method>
        <method name="AuthorizeService">
          <arg direction="in"  type="o" name="device"/>
          <arg direction="in"  type="s" name="uuid"/>
        </method>
        <method name="Cancel"/>
        <method name="Release"/>
      </interface>
    </node>
    """

    dbus = AGENT1_XML

    def __init__(self, auto_accept=True):
        self._auto_accept = auto_accept

    def RequestPinCode(self, device):  # noqa: N802
        log.info("Agent.RequestPinCode device=%s", device)
        if not self._auto_accept:
            raise _BlueZRejected("Rejected by test agent")
        return "0000"

    def DisplayPinCode(self, device, pincode):  # noqa: N802
        log.info("Agent.DisplayPinCode device=%s pin=%s", device, pincode)

    def RequestPasskey(self, device):  # noqa: N802
        log.info("Agent.RequestPasskey device=%s", device)
        if not self._auto_accept:
            raise _BlueZRejected("Rejected by test agent")
        return 0

    def DisplayPasskey(self, device, passkey, entered):  # noqa: N802
        log.info("Agent.DisplayPasskey device=%s passkey=%d", device, passkey)

    def RequestConfirmation(self, device, passkey):  # noqa: N802
        log.info(
            "Agent.RequestConfirmation device=%s passkey=%d auto_accept=%s",
            device,
            passkey,
            self._auto_accept,
        )
        if not self._auto_accept:
            raise _BlueZRejected("Rejected by test agent")

    def RequestAuthorization(self, device):  # noqa: N802
        log.info("Agent.RequestAuthorization device=%s", device)
        if not self._auto_accept:
            raise _BlueZRejected("Rejected by test agent")

    def AuthorizeService(self, device, uuid):  # noqa: N802
        log.info("Agent.AuthorizeService device=%s uuid=%s", device, uuid)

    def Cancel(self):  # noqa: N802
        log.info("Agent.Cancel")

    def Release(self):  # noqa: N802
        log.info("Agent.Release")


class LaptopBT:
    """
    Context-manager wrapper around the laptop's BlueZ adapter.

    Usage::

        with LaptopBT() as bt:
            bt.set_discoverable(True)
            ...

    The adapter is made pairable and an auto-accept pairing agent is
    registered on entry; both are restored on exit.
    """

    def __init__(self, adapter_mac="E8:FB:1C:25:E4:C2"):
        self._adapter_mac = adapter_mac.upper()
        self._bus = pydbus.SystemBus()
        self._adapter_path = self._resolve_adapter_path(adapter_mac)
        self._adapter = self._bus.get(BLUEZ_SERVICE, self._adapter_path)
        self._agent_mgr = self._bus.get(BLUEZ_SERVICE, "/org/bluez")[
            AGENT_MANAGER1_IFACE
        ]
        self._agent_pub = None  # pydbus publication handle
        self._agent_obj = None
        self._loop = None

    def _resolve_adapter_path(self, mac):
        """Find the D-Bus object path for the adapter with the given MAC."""
        target = mac.upper()
        obj_mgr = self._bus.get(BLUEZ_SERVICE, "/")[OBJECT_MANAGER_IFACE]
        for path, ifaces in obj_mgr.GetManagedObjects().items():
            if ADAPTER1_IFACE in ifaces:
                if ifaces[ADAPTER1_IFACE].get("Address", "").upper() == target:
                    return path
        raise RuntimeError(
            "No BlueZ adapter found with MAC {} — "
            "is Bluetooth powered on?".format(mac)
        )

    # ------------------------------------------------------------------
    # Context manager
    # ------------------------------------------------------------------

    def __enter__(self):
        # Power-cycle the adapter so the HCI controller resets page-scan state.
        # After many connect/disconnect/pair/unpair cycles the HCI-level page-scan
        # bit can drift despite D-Bus properties showing Pairable=True, causing
        # PAGE_TIMEOUT errors (error 0x04) on the ESP32 side.
        self._adapter.Powered = False
        time.sleep(0.8)
        self._adapter.Powered = True
        time.sleep(1.0)
        self.register_agent(auto_accept=True)
        self._adapter.Pairable = True
        self._adapter.Discoverable = True
        self._adapter.DiscoverableTimeout = 0
        log.info(
            "LaptopBT: adapter %s powered, pairable, discoverable",
            self._adapter_mac,
        )
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        try:
            self._adapter.Discoverable = False
        except Exception:
            pass
        try:
            self._adapter.Pairable = False
        except Exception:
            pass
        try:
            self._deregister_agent()
        except Exception:
            pass
        log.info("LaptopBT: adapter restored (not discoverable, not pairable)")
        return False  # do not suppress exceptions

    # ------------------------------------------------------------------
    # Agent management
    # ------------------------------------------------------------------

    def register_agent(self, auto_accept=True):
        """Register a BlueZ pairing agent on the system bus."""
        self._deregister_agent()
        self._agent_obj = SimpleAgent(auto_accept=auto_accept)
        self._agent_pub = self._bus.register_object(
            AGENT_PATH, self._agent_obj, None
        )
        self._agent_mgr.RegisterAgent(AGENT_PATH, "NoInputNoOutput")
        self._agent_mgr.RequestDefaultAgent(AGENT_PATH)
        log.info(
            "LaptopBT: agent registered at %s auto_accept=%s",
            AGENT_PATH,
            auto_accept,
        )

    def _deregister_agent(self):
        if self._agent_pub is not None:
            try:
                self._agent_mgr.UnregisterAgent(AGENT_PATH)
            except Exception:
                pass
            try:
                self._agent_pub.unregister()
            except Exception:
                pass
            self._agent_pub = None
            self._agent_obj = None
            log.info("LaptopBT: agent deregistered")

    # ------------------------------------------------------------------
    # Discoverability
    # ------------------------------------------------------------------

    def set_pairable(self, on):
        """Set adapter pairable state.  Returns previous value."""
        import time as _time
        old = bool(self._adapter.Pairable)
        for attempt in range(5):
            try:
                self._adapter.Pairable = bool(on)
                break
            except Exception as exc:
                if attempt >= 4:
                    raise
                log.warning(
                    "LaptopBT: set_pairable attempt %d failed (%s), retrying…", attempt + 1, exc
                )
                _time.sleep(1.0)
        log.info("LaptopBT: pairable=%s", on)
        return old

    def set_discoverable(self, on, timeout_s=0):
        """Set adapter discoverable state; returns previous value."""
        import time as _time
        old = bool(self._adapter.Discoverable)
        max_attempts = 20
        _power_cycled = False
        for attempt in range(max_attempts):
            try:
                self._adapter.Discoverable = bool(on)
                self._adapter.DiscoverableTimeout = int(timeout_s)
                break
            except Exception as exc:
                if attempt >= max_attempts - 1:
                    if not on:
                        # set_discoverable(False) failures are non-critical and
                        # must NOT trigger a power-cycle — that would destroy active
                        # pairing/connection state in BlueZ.  Log and return.
                        log.warning(
                            "LaptopBT: set_discoverable(False) stuck after %d attempts "
                            "(adapter will auto-timeout): %s",
                            max_attempts, exc,
                        )
                        return old
                    raise
                if on and not _power_cycled and attempt == 2:
                    # For set_discoverable(True): after 3 consecutive failures the
                    # adapter is stuck (typically degraded HCI state from prior ESP32
                    # inquiry scans).  Power-cycle once via D-Bus and re-initialise.
                    # Safe here because we haven't started any BT operation yet.
                    _power_cycled = True
                    log.warning(
                        "LaptopBT: set_discoverable stuck after %d attempts, "
                        "power-cycling adapter…",
                        attempt + 1,
                    )
                    try:
                        self._adapter.Powered = False
                        _time.sleep(3.0)
                        self._adapter.Powered = True
                        _time.sleep(5.0)
                        self._adapter.Pairable = True
                        self.register_agent(auto_accept=True)
                    except Exception as reset_exc:
                        log.warning(
                            "LaptopBT: adapter reset failed: %s", reset_exc
                        )
                log.warning(
                    "LaptopBT: set_discoverable attempt %d failed (%s), retrying…",
                    attempt + 1, exc,
                )
                _time.sleep(1.5)
        log.info("LaptopBT: discoverable=%s timeout=%ds", on, timeout_s)
        return old

    # ------------------------------------------------------------------
    # Device enumeration
    # ------------------------------------------------------------------

    def _get_objects(self):
        """Return all BlueZ managed objects."""
        obj_mgr = self._bus.get(BLUEZ_SERVICE, "/")[OBJECT_MANAGER_IFACE]
        return obj_mgr.GetManagedObjects()

    def _find_device_path(self, mac):
        """Return the D-Bus object path for the device with the given MAC, or None."""
        target = mac.upper()
        suffix = _mac_to_dbus_suffix(target)
        # Expected path: /org/bluez/<adapter>/dev_XX_XX_XX_XX_XX_XX
        for path, ifaces in self._get_objects().items():
            if DEVICE1_IFACE in ifaces:
                addr = ifaces[DEVICE1_IFACE].get("Address", "")
                if addr.upper() == target or path.endswith("dev_" + suffix):
                    return path
        return None

    def get_paired_devices(self):
        """Return list of dicts for all paired devices visible to this adapter."""
        result = []
        prefix = self._adapter_path + "/dev_"
        for path, ifaces in self._get_objects().items():
            if not path.startswith(prefix):
                continue
            if DEVICE1_IFACE not in ifaces:
                continue
            props = ifaces[DEVICE1_IFACE]
            if not props.get("Paired", False):
                continue
            result.append(
                {
                    "mac": props.get("Address", ""),
                    "name": props.get("Name", props.get("Alias", "")),
                    "trusted": props.get("Trusted", False),
                    "connected": props.get("Connected", False),
                }
            )
        return result

    def is_paired(self, mac):
        """Return True if the device with the given MAC is paired."""
        path = self._find_device_path(mac)
        if path is None:
            return False
        try:
            dev = self._bus.get(BLUEZ_SERVICE, path)
            return bool(dev.Paired)
        except Exception:
            return False

    def remove_device(self, mac):
        """Remove (unpair) the device with the given MAC from the adapter."""
        path = self._find_device_path(mac)
        if path is None:
            log.info("LaptopBT.remove_device: %s not found — already removed", mac)
            return
        try:
            self._adapter.RemoveDevice(path)
            log.info("LaptopBT.remove_device: removed %s (%s)", mac, path)
        except Exception as exc:
            log.warning("LaptopBT.remove_device: %s — %s", mac, exc)

    # ------------------------------------------------------------------
    # Connection state
    # ------------------------------------------------------------------

    def is_connected(self, mac):
        """Return True if the device with the given MAC is currently connected."""
        path = self._find_device_path(mac)
        if path is None:
            return False
        try:
            dev = self._bus.get(BLUEZ_SERVICE, path)
            return bool(dev.Connected)
        except Exception:
            return False

    def wait_for_connect(self, mac, timeout_s=30.0):
        """
        Poll until the device is connected or timeout expires.
        Returns True on success, False on timeout.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self.is_connected(mac):
                log.info("LaptopBT.wait_for_connect: %s connected", mac)
                return True
            # Process any pending D-Bus callbacks briefly
            ctx = GLib.MainContext.default()
            ctx.iteration(may_block=False)
            time.sleep(0.5)
        log.warning(
            "LaptopBT.wait_for_connect: %s not connected after %.1fs",
            mac,
            timeout_s,
        )
        return False

    def wait_for_disconnect(self, mac, timeout_s=15.0):
        """
        Poll until the device is no longer connected or timeout expires.
        Returns True when disconnected, False on timeout.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if not self.is_connected(mac):
                log.info("LaptopBT.wait_for_disconnect: %s disconnected", mac)
                return True
            ctx = GLib.MainContext.default()
            ctx.iteration(may_block=False)
            time.sleep(0.5)
        log.warning(
            "LaptopBT.wait_for_disconnect: %s still connected after %.1fs",
            mac,
            timeout_s,
        )
        return False

    def connect_profiles(self, mac, retries=8, retry_delay_s=2.0):
        """Ask BlueZ to connect all profiles (including A2DP) to the device.

        Idempotent — no-op if already fully connected.  Retries on
        br-connection-unknown (PulseAudio briefly deregisters the A2DP Sink
        profile after DeviceRemoved and needs a moment to re-register).
        """
        path = self._find_device_path(mac)
        if path is None:
            log.warning("LaptopBT.connect_profiles: %s not found", mac)
            return
        dev = self._bus.get(BLUEZ_SERVICE, path)
        for attempt in range(retries):
            try:
                dev.Connect()
                log.info(
                    "LaptopBT.connect_profiles: %s Connect() called (attempt %d)",
                    mac, attempt + 1,
                )
                return
            except Exception as exc:
                exc_str = str(exc)
                if "br-connection-unknown" in exc_str and attempt < retries - 1:
                    log.warning(
                        "LaptopBT.connect_profiles: attempt %d br-connection-unknown"
                        " — PulseAudio not ready, retrying in %.1fs",
                        attempt + 1, retry_delay_s,
                    )
                    time.sleep(retry_delay_s)
                    continue
                log.warning("LaptopBT.connect_profiles: %s — %s", mac, exc)
                return

    def set_trusted(self, mac, trusted):
        """Set the Trusted property on the BlueZ device object.

        PulseAudio's module-bluetooth-policy only auto-reconnects TRUSTED
        devices.  Set trusted=False before a firmware RESET to prevent
        PulseAudio from racing with the ESP32's own attempt_reconnection()
        for the AVDTP slot; restore to True once the boot reconnect completes.
        """
        path = self._find_device_path(mac)
        if path is None:
            log.warning("LaptopBT.set_trusted: %s not found", mac)
            return
        dev = self._bus.get(BLUEZ_SERVICE, path)
        dev.Trusted = bool(trusted)
        log.info("LaptopBT.set_trusted: %s trusted=%s", mac, trusted)

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def adapter_mac(self):
        """Return the adapter MAC address string."""
        return self._adapter_mac
