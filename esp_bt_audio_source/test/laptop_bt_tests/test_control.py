"""
test_control.py — CTRL-1: Control command tests during live A2DP connection.

Verifies ESP32 control commands work correctly with a real A2DP link active.
Connection state is asserted via the laptop BlueZ side; STATUS fields (INIT,
PAIRED_COUNT, VOL) are the firmware-side indicators.

Commands under test:
  STATUS            → OK|STATUS|CURRENT|MUTE=N,...,PAIRED_COUNT=N,INIT=N,RUN=N,VOL=N,...
  VOLUME <0-100>    → OK|VOLUME|SET|<N>
  VOLUME <oob>      → ERR|VOLUME|OUT_OF_RANGE
  MUTE              → OK|MUTE|SET
  UNMUTE            → OK|UNMUTE|CLEARED
  AUDIO_STATUS      → OK|AUDIO_STATUS|CURRENT|...,ENGINE_BYTES=N,...
  MEM               → OK|MEM|STATS|DRAM=N,...
  AUDIO_AUTOSTART get → OK|AUDIO_AUTOSTART|STATUS|enabled|disabled
  VERSION           → OK|VERSION|<non-empty version string>
"""

import logging
import re
import time

import pytest

from conftest import ESP32_MAC

log = logging.getLogger(__name__)

pytestmark = pytest.mark.laptop_bt


def _parse_status(line):
    """Return dict of key=value pairs from an OK|STATUS|CURRENT|... line."""
    m = re.search(r"OK\|STATUS\|CURRENT\|(.+)", line)
    if not m:
        return {}
    fields = {}
    for part in m.group(1).split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            fields[k.strip()] = v.strip()
    return fields


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


class TestControl:

    def test_status_shows_connected_bt_state(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """STATUS while connected: PAIRED_COUNT > 0 and INIT=1."""
        assert laptop_bt_adapter.is_connected(ESP32_MAC), (
            "Precondition: ESP32 not showing as connected on laptop"
        )
        esp32.drain(0.1)
        line = esp32.send_and_expect("STATUS", "OK|STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_status(line)
        log.info("STATUS fields: %s", fields)

        paired_count = int(fields.get("PAIRED_COUNT", "0"))
        assert paired_count > 0, (
            "STATUS PAIRED_COUNT=0 while connected — expected >= 1: {}".format(line)
        )
        init_val = int(fields.get("INIT", "0"))
        assert init_val == 1, (
            "STATUS INIT={} — audio processor not initialized: {}".format(init_val, line)
        )

    def test_volume_set_persists_in_status(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """VOLUME 42 is accepted and appears as VOL=42 in the next STATUS response."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("VOLUME 42", "OK|VOLUME|SET|", timeout_s=5.0)
        assert "OK|VOLUME|SET|42" in line, "VOLUME SET failed: {}".format(line)

        esp32.drain(0.1)
        line = esp32.send_and_expect("STATUS", "OK|STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_status(line)
        log.info("STATUS after VOLUME 42: %s", fields)
        vol = int(fields.get("VOL", "-1"))
        try:
            assert vol == 42, (
                "STATUS VOL={} after VOLUME 42: {}".format(vol, line)
            )
        finally:
            # Restore default volume so NVS-persisted value doesn't affect other tests.
            try:
                esp32.drain(0.1)
                esp32.send_and_expect("VOLUME 100", "OK|VOLUME|SET|", timeout_s=5.0)
            except Exception:
                pass

    def test_volume_out_of_range_rejected(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """VOLUME above 100 or below 0 is rejected with ERR|VOLUME|OUT_OF_RANGE."""
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "VOLUME 101", "ERR|VOLUME|OUT_OF_RANGE", timeout_s=5.0
        )
        assert "ERR|VOLUME|OUT_OF_RANGE" in line, (
            "VOLUME 101 not rejected: {}".format(line)
        )

        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "VOLUME -1", "ERR|VOLUME|OUT_OF_RANGE", timeout_s=5.0
        )
        assert "ERR|VOLUME|OUT_OF_RANGE" in line, (
            "VOLUME -1 not rejected: {}".format(line)
        )

    def test_mute_unmute_cycle_while_streaming(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """MUTE/UNMUTE cycle during streaming: engine keeps producing data."""
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(1.5)

        esp32.drain(0.1)
        line = esp32.send_and_expect("MUTE", "OK|MUTE|SET", timeout_s=5.0)
        assert "OK|MUTE|SET" in line, "MUTE failed: {}".format(line)

        esp32.drain(0.1)
        line = esp32.send_and_expect("UNMUTE", "OK|UNMUTE|CLEARED", timeout_s=5.0)
        assert "OK|UNMUTE|CLEARED" in line, "UNMUTE failed: {}".format(line)

        time.sleep(1.0)
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0
        )
        fields = _parse_audio_status(line)
        engine_bytes = int(fields.get("ENGINE_BYTES", "0"))
        ring_peak = int(fields.get("RING_PEAK", "0"))
        assert engine_bytes > 0 or ring_peak > 0, (
            "Audio not flowing after MUTE/UNMUTE — ENGINE_BYTES={} RING_PEAK={}".format(
                engine_bytes, ring_peak
            )
        )

    def test_mem_command_returns_stats(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """MEM returns OK|MEM|STATS|DRAM=N,... with DRAM > 0."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("MEM", "OK|MEM|STATS|", timeout_s=5.0)
        assert "OK|MEM|STATS|" in line, "MEM failed: {}".format(line)
        m = re.search(r"DRAM=(\d+)", line)
        assert m is not None, "MEM response missing DRAM field: {}".format(line)
        dram = int(m.group(1))
        assert dram > 0, "MEM DRAM={} — expected > 0: {}".format(dram, line)

    def test_audio_autostart_get_reflects_current_setting(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """AUDIO_AUTOSTART get returns OK|AUDIO_AUTOSTART|STATUS|enabled or disabled."""
        esp32.drain(0.1)
        line = esp32.send_and_expect(
            "AUDIO_AUTOSTART get", "OK|AUDIO_AUTOSTART|STATUS|", timeout_s=5.0
        )
        assert "OK|AUDIO_AUTOSTART|STATUS|" in line, (
            "AUDIO_AUTOSTART get failed: {}".format(line)
        )
        assert "enabled" in line or "disabled" in line, (
            "AUDIO_AUTOSTART STATUS value neither 'enabled' nor 'disabled': {}".format(
                line
            )
        )

    def test_version_command_returns_non_empty(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """VERSION returns OK|VERSION|<non-empty version string>."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("VERSION", "OK|VERSION|", timeout_s=5.0)
        resp = esp32.parse_response(line)
        assert resp.get("status") == "OK", (
            "VERSION returned non-OK status: {}".format(line)
        )
        # parse_response splits STATUS|COMMAND|RESULT[|DATA]:
        # OK|VERSION|<version>[|metadata] → result = version string
        version = resp.get("result", "")
        assert version, "VERSION result is empty: {}".format(line)
        log.info("Firmware version: %s", version)
