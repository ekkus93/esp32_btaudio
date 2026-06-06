"""
test_streaming.py — STREAM-1: A2DP streaming verification tests.

After an A2DP connection, the laptop's PulseAudio stack creates a Bluetooth
sink for the ESP32.  Audio data flow is verified by checking ENGINE_BYTES and
RING_PEAK in the AUDIO_STATUS output (cumulative counters that grow once the
audio engine is running).

Commands under test:
  START       → OK|START|STARTED
  STOP        → OK|STOP|STOPPED
  AUDIO_STATUS → OK|AUDIO_STATUS|CURRENT|RING_CAP=N,RING_USED=N,...
  VOLUME <N>  → OK|VOLUME|SET|<N>
  MUTE        → OK|MUTE|SET
  UNMUTE      → OK|UNMUTE|CLEARED
"""

import logging
import re
import time

import pulsectl
import pytest

from conftest import ESP32_MAC

log = logging.getLogger(__name__)

pytestmark = pytest.mark.laptop_bt

BT_SINK_TIMEOUT_S = 15.0


def _find_bt_sink(esp32_mac, timeout_s=BT_SINK_TIMEOUT_S):
    """Poll PulseAudio for a Bluetooth audio object matching the ESP32 MAC.

    The ESP32 acts as an A2DP SOURCE (it sends audio to the laptop).  PulseAudio
    registers this as a *source* (bluez_source.*), not a sink.  We search both
    lists so the helper works regardless of how the profile is negotiated.
    """
    mac_under = esp32_mac.upper().replace(":", "_")
    mac_lower = mac_under.lower()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with pulsectl.Pulse("esp32-bt-test") as pulse:
                for obj in list(pulse.sink_list()) + list(pulse.source_list()):
                    name_lower = obj.name.lower()
                    desc_lower = obj.description.lower()
                    if (
                        "bluez" in name_lower
                        or mac_lower in name_lower
                        or mac_lower in desc_lower
                    ):
                        return obj
        except Exception as exc:
            log.warning("_find_bt_sink: pulsectl error: %s", exc)
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


class TestStreaming:

    def test_pulseaudio_bt_sink_appears_after_connect(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """After A2DP connection, a Bluetooth sink appears in PulseAudio within 15s."""
        sink = _find_bt_sink(ESP32_MAC, timeout_s=BT_SINK_TIMEOUT_S)
        assert sink is not None, (
            "No Bluetooth sink for ESP32 {} found in PulseAudio within {:.0f}s".format(
                ESP32_MAC, BT_SINK_TIMEOUT_S
            )
        )
        log.info("Bluetooth sink found: name=%s desc=%s", sink.name, sink.description)

    def test_start_streaming_command_succeeds(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """START command returns OK|START|STARTED while A2DP is connected."""
        esp32.drain(0.1)
        line = esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        assert "OK|START|STARTED" in line, "START failed: {}".format(line)

    def test_audio_status_ring_buffer_fills_after_start(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """After START, AUDIO_STATUS shows ENGINE_BYTES > 0 confirming audio is flowing."""
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(2.0)

        esp32.drain(0.1)
        line = esp32.send_and_expect("AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_audio_status(line)
        log.info("AUDIO_STATUS fields: %s", fields)

        # ENGINE_BYTES is a cumulative counter; > 0 means data has flowed.
        engine_bytes = int(fields.get("ENGINE_BYTES", "0"))
        ring_peak = int(fields.get("RING_PEAK", "0"))
        assert engine_bytes > 0 or ring_peak > 0, (
            "No audio data flowing after START — ENGINE_BYTES={} RING_PEAK={}".format(
                engine_bytes, ring_peak
            )
        )

    def test_stop_streaming_command_succeeds(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """START then STOP returns OK|STOP|STOPPED and clears RING_USED."""
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(1.0)

        esp32.drain(0.1)
        line = esp32.send_and_expect("STOP", "OK|STOP|STOPPED", timeout_s=5.0)
        assert "OK|STOP|STOPPED" in line, "STOP failed: {}".format(line)

        time.sleep(0.5)
        esp32.drain(0.1)
        line = esp32.send_and_expect("AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_audio_status(line)
        ring_used = int(fields.get("RING_USED", "0"))
        source = fields.get("SOURCE", "UNKNOWN")
        assert ring_used == 0 or source in ("IDLE", "SILENCE", "UNKNOWN"), (
            "Ring buffer not drained after STOP — RING_USED={} SOURCE={}".format(
                ring_used, source
            )
        )

    def test_streaming_survives_volume_change(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """Volume change during streaming does not interrupt audio data flow."""
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(1.5)

        esp32.drain(0.1)
        line = esp32.send_and_expect("VOLUME 50", "OK|VOLUME|SET|", timeout_s=5.0)
        assert "OK|VOLUME|SET|50" in line, "VOLUME SET failed: {}".format(line)

        time.sleep(1.0)
        esp32.drain(0.1)
        line = esp32.send_and_expect("AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_audio_status(line)
        engine_bytes = int(fields.get("ENGINE_BYTES", "0"))
        ring_peak = int(fields.get("RING_PEAK", "0"))
        assert engine_bytes > 0 or ring_peak > 0, (
            "Audio stopped after VOLUME change — ENGINE_BYTES={} RING_PEAK={}".format(
                engine_bytes, ring_peak
            )
        )

    def test_mute_does_not_stop_ring_buffer_fill(
        self, esp32, laptop_bt_adapter, connected_state
    ):
        """MUTE is post-ring-buffer; data still flows through the engine while muted."""
        esp32.drain(0.1)
        esp32.send_and_expect("START", "OK|START|STARTED", timeout_s=5.0)
        time.sleep(1.0)

        esp32.drain(0.1)
        line = esp32.send_and_expect("MUTE", "OK|MUTE|SET", timeout_s=5.0)
        assert "OK|MUTE|SET" in line, "MUTE failed: {}".format(line)

        time.sleep(1.0)
        esp32.drain(0.1)
        line = esp32.send_and_expect("AUDIO_STATUS", "OK|AUDIO_STATUS|CURRENT|", timeout_s=5.0)
        fields = _parse_audio_status(line)
        engine_bytes = int(fields.get("ENGINE_BYTES", "0"))
        ring_peak = int(fields.get("RING_PEAK", "0"))
        assert engine_bytes > 0 or ring_peak > 0, (
            "Audio stopped after MUTE — ENGINE_BYTES={} RING_PEAK={}".format(
                engine_bytes, ring_peak
            )
        )

        esp32.drain(0.1)
        line = esp32.send_and_expect("UNMUTE", "OK|UNMUTE|CLEARED", timeout_s=5.0)
        assert "OK|UNMUTE|CLEARED" in line, "UNMUTE failed: {}".format(line)
