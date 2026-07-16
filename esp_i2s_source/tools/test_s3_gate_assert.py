#!/usr/bin/env python3
"""Offline unit tests for s3_gate_assert.evaluate() — no hardware needed.

Run: python3 -m pytest tools/test_s3_gate_assert.py   (from esp_i2s_source/)
"""
import s3_gate_assert as g

PASS, WARN, FAIL = g.PASS, g.WARN, g.FAIL


def _verdict(text, **kw):
    ok, results = g.evaluate(text, **kw)
    return ok, {name: status for name, status, _ in results}


# A fully-healthy boot: reached end of init, WiFi up, audio streaming, link 3/3.
HEALTHY = """
DIAG|BOOT|READY|psram_kb=8192,heap=300000
DIAG|BTLINK|SELFTEST|ok=3/3
DIAG|WIFI|STA|ssid=CircuitLaunch,ip=10.1.2.52
DIAG|CONSOLE|READY|cmds=WIFI,STATUS
DIAG|WEB|READY|port=80
DIAG|I2S|bytes=1024,und=0,undev=0,ringpeak=100
DIAG|I2SFREQ|bclk_hz=2822400,ws_hz=44100,ratio=64.00
DIAG|I2S|bytes=94208,und=0,undev=0,ringpeak=120
DIAG|I2SFREQ|bclk_hz=2822400,ws_hz=44100,ratio=64.00
"""

# A real idle boot (as captured on hardware): booted cleanly, but no audio
# playing (I2S idle) and the WROOM32 is on a test image (link 0/3).
IDLE_BOOT = """
DIAG|BTLINK|cmd=VERSION,state=TIMEOUT,result=
DIAG|BTLINK|SELFTEST|ok=0/3
DIAG|CONSOLE|READY|cmds=WIFI,STATUS
DIAG|WIFI|STA|ssid=CircuitLaunch,ip=10.1.2.52
DIAG|WEB|READY|port=80
DIAG|I2S|bytes=0,und=0,undev=0,ringpeak=0
DIAG|I2SFREQ|bclk_hz=0,ws_hz=0,ratio=-1.00
DIAG|I2S|bytes=0,und=0,undev=0,ringpeak=0
DIAG|I2SFREQ|bclk_hz=0,ws_hz=0,ratio=-1.00
"""


def test_healthy_boot_all_pass():
    ok, checks = _verdict(HEALTHY)
    assert ok is True
    assert all(v is PASS for v in checks.values())


def test_idle_boot_passes_with_warnings():
    # The core gate (no_crash + boot_complete) passes; audio + link are warnings.
    ok, checks = _verdict(IDLE_BOOT)
    assert ok is True
    assert checks["no_crash"] is PASS
    assert checks["boot_complete"] is PASS
    assert checks["boot_ready"] is WARN      # one-shot BOOT|READY not in this capture
    assert checks["i2s_active"] is WARN       # idle, no stream
    assert checks["link"] is WARN             # WROOM32 on a test image


def test_panic_fails():
    text = HEALTHY + "\nGuru Meditation Error: Core 0 panic'ed (LoadProhibited)\nBacktrace: 0x..\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_abort_fails():
    text = HEALTHY + "\nabort() was called\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_assert_fails():
    text = HEALTHY + "\nassert failed\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_watchdog_fails():
    text = HEALTHY + "\nTask watchdog got triggered\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_heap_corruption_fails():
    text = HEALTHY + "\nCORRUPT HEAP: malloc caps\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_stack_overflow_fails():
    text = HEALTHY + "\nstack overflow\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_incomplete_boot_fails():
    # web server never came up (init aborted before web_ui_start)
    text = IDLE_BOOT.replace("DIAG|WEB|READY|port=80", "")
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["boot_complete"] is FAIL


def test_zero_psram_fails_when_boot_ready_present():
    text = HEALTHY.replace("psram_kb=8192", "psram_kb=0")
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["boot_ready"] is FAIL


def test_require_i2s_promotes_idle_to_failure():
    ok, checks = _verdict(IDLE_BOOT, require_i2s=True)
    assert ok is False
    assert checks["i2s_active"] is FAIL


def test_require_link_promotes_degraded_to_failure():
    ok, checks = _verdict(IDLE_BOOT, require_link=True)
    assert ok is False
    assert checks["link"] is FAIL


def test_require_i2s_still_passes_when_audio_healthy():
    ok, checks = _verdict(HEALTHY, require_i2s=True, require_link=True)
    assert ok is True
    assert checks["i2s_active"] is PASS
    assert checks["link"] is PASS


def test_wifi_unassociated_is_warning_not_failure():
    text = IDLE_BOOT.replace("DIAG|WIFI|STA|ssid=CircuitLaunch,ip=10.1.2.52", "")
    ok, checks = _verdict(text)
    assert ok is True
    assert checks["wifi_sta"] is WARN


def test_i2s_flat_bytes_not_healthy():
    # two beacons but the counter never moved → not climbing
    text = HEALTHY.replace("bytes=94208", "bytes=1024")
    ok, checks = _verdict(text)
    assert checks["i2s_active"] is WARN     # warn by default
    assert ok is True


def test_brownout_fails():
    text = HEALTHY + "\nBrownout detector was triggered\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_int_wdt_fails():
    text = HEALTHY + "\nInterrupt wdt timeout on GPIO0\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL


def test_sw_reset_fails():
    text = HEALTHY + "\nrst:0x3 (SW_RESET)\n"
    ok, checks = _verdict(text)
    assert ok is False
    assert checks["no_crash"] is FAIL