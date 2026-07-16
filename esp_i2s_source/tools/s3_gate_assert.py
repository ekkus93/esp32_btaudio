#!/usr/bin/env python3
"""esp_i2s_source S3 boot-gate assertion.

Reads captured device console (stdin or --file) from a booted ESP32-S3 and
decides pass/fail against the DIAG boot markers printed by main.c. Pure/offline:
the hardware capture lives in s3_device_gate.sh; this only judges text, so its
logic is unit-tested in test_s3_gate_assert.py without a board.

Design: a boot gate answers "did the S3 boot cleanly to a running state?" — not
"is audio playing" or "is the sibling board wired". Those depend on external
state, so they are reported but not gated by default.

Required (gate FAILS if any fails):
  no_crash       no panic / Guru Meditation / abort / boot-loop signature
  boot_complete  DIAG|CONSOLE|READY and DIAG|WEB|READY both present — proves
                 app_main ran through every ESP_ERROR_CHECK init to the end
                 (wifi/radio/stations/console/web) without a fatal abort.

Conditional (pass ✓ / warn ! / fail ✗ — never gate unless a strict flag is set):
  boot_ready     DIAG|BOOT|READY with psram_kb>0. Warn if not captured (it is a
                 one-shot line printed within ~1s of reset and can be missed).
  wifi_sta       DIAG|WIFI|STA — pass if associated; warn otherwise (needs an AP).
  i2s_active     DIAG|I2S bytes climbing + clock ratio ~64. Warn when idle (no
                 station streaming → silence writes nothing). --require-i2s makes
                 a stalled/absent stream FAIL (use when audio is expected).
  link           DIAG|BTLINK|SELFTEST|ok=3/3 — the UART link to the WROOM32.
                 Needs the WROOM32 wired and running responsive firmware, so a
                 degraded link is a WARN unless --require-link is set.
"""
import argparse
import re
import sys

I2S_RATIO_MIN = 55.0
I2S_RATIO_MAX = 72.0

CRASH_SIGNATURES = (
    "Guru Meditation",
    "abort() was called",
    "assert failed",
    "rst:0x3 (SW_RESET)",       # panic-triggered software reset
    "Backtrace:",
    "Task watchdog got triggered",
    "Interrupt wdt timeout",
    "Stack canary watchpoint triggered",
    "stack overflow",
    "CORRUPT HEAP",
    "heap corruption",
    "Brownout detector was triggered",
)

PASS, WARN, FAIL = True, None, False


def evaluate(text, require_i2s=False, require_link=False):
    """Return (ok: bool, results: list[(name, status, detail)]).

    status is PASS/FAIL/WARN (True/False/None). Gate fails iff any status is FAIL.
    """
    results = []

    # --- required: no crash ---
    hit = next((s for s in CRASH_SIGNATURES if s in text), None)
    results.append(("no_crash", FAIL if hit else PASS,
                    f"found '{hit}'" if hit else "no panic/abort signatures"))

    # --- required: boot reached the end of init ---
    console_ready = "DIAG|CONSOLE|READY" in text
    web_ready = "DIAG|WEB|READY" in text
    boot_complete = console_ready and web_ready
    missing = [n for n, ok in (("CONSOLE|READY", console_ready), ("WEB|READY", web_ready)) if not ok]
    results.append(("boot_complete", PASS if boot_complete else FAIL,
                    "console+web up" if boot_complete else f"missing {', '.join(missing)}"))

    # --- conditional: BOOT|READY + psram ---
    boot = re.search(r"DIAG\|BOOT\|READY\|psram_kb=(\d+),heap=(\d+)", text)
    if boot is None:
        results.append(("boot_ready", WARN, "BOOT|READY not captured (one-shot; may be raced)"))
    else:
        psram_kb = int(boot.group(1))
        results.append(("boot_ready", PASS if psram_kb > 0 else FAIL, f"psram_kb={psram_kb}"))

    # --- conditional: wifi ---
    wifi = re.search(r"DIAG\|WIFI\|STA\|ssid=([^,]*),ip=([0-9.]+)", text)
    if wifi and wifi.group(2) not in ("", "0.0.0.0"):
        results.append(("wifi_sta", PASS, f"ssid={wifi.group(1)},ip={wifi.group(2)}"))
    else:
        results.append(("wifi_sta", WARN, "not associated (needs a known AP)"))

    # --- conditional: I2S throughput + clock ---
    byte_counts = [int(m) for m in re.findall(r"DIAG\|I2S\|bytes=(\d+)", text)]
    ratios = [float(m) for m in re.findall(r"DIAG\|I2SFREQ\|[^\n]*ratio=(-?\d+\.\d+)", text)]
    climbing = len(byte_counts) >= 2 and byte_counts[-1] > byte_counts[0]
    good_ratio = next((r for r in ratios if I2S_RATIO_MIN <= r <= I2S_RATIO_MAX), None)
    i2s_healthy = climbing and good_ratio is not None
    if i2s_healthy:
        results.append(("i2s_active", PASS,
                        f"bytes {byte_counts[0]}->{byte_counts[-1]}, ratio={good_ratio}"))
    else:
        last = byte_counts[-1] if byte_counts else None
        detail = f"idle/stalled (bytes={last}, ratios={ratios or 'none'})"
        results.append(("i2s_active", FAIL if require_i2s else WARN, detail))

    # --- conditional: cross-board link ---
    link = re.search(r"DIAG\|BTLINK\|SELFTEST\|ok=(\d+)/3", text)
    if link and int(link.group(1)) == 3:
        results.append(("link", PASS, "ok=3/3"))
    else:
        detail = (f"ok={link.group(1)}/3" if link else "no SELFTEST marker") + " (WROOM32-dependent)"
        results.append(("link", FAIL if require_link else WARN, detail))

    ok = not any(status is FAIL for _, status, _ in results)
    return ok, results


def format_report(ok, results):
    lines = []
    for name, status, detail in results:
        mark = "✓" if status is PASS else ("!" if status is WARN else "✗")
        lines.append(f"  [{mark}] {name:<13} {detail}")
    lines.append(f"GATE: {'PASS' if ok else 'FAIL'}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Judge esp_i2s_source S3 boot console output.")
    ap.add_argument("--file", help="Read console capture from this file (default: stdin).")
    ap.add_argument("--require-i2s", action="store_true",
                    help="Fail (not warn) if I2S output is idle/stalled (audio expected).")
    ap.add_argument("--require-link", action="store_true",
                    help="Fail (not warn) if the BTLINK self-test is not 3/3 "
                         "(WROOM32 wired + running responsive firmware).")
    ap.add_argument("--degraded", action="store_true",
                    help="Relax companion/network requirements (WiFi, I2S, link).")
    args = ap.parse_args()

    text = open(args.file, encoding="utf-8", errors="replace").read() if args.file \
        else sys.stdin.read()

    require_i2s = not args.degraded
    require_link = not args.degraded

    ok, results = evaluate(text, require_i2s=require_i2s, require_link=require_link)
    print(format_report(ok, results))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())