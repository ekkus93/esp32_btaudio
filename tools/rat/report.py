"""Parse/aggregate Unity + ctest output into the canonical summary."""
from __future__ import annotations

import json
import re
import sys
import time
from pathlib import Path

from .proc import run_cmd
from .common import TMP_DIR


def _unity_counts_from_output(stdout: str) -> dict:
    match = re.search(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored", stdout)
    if match:
        return {
            "tests": int(match.group(1)),
            "failures": int(match.group(2)),
            "ignored": int(match.group(3)),
        }

    # Fallback: count per-test PASS/FAIL/IGNORE tokens if the footer is missing.
    try:
        pass_count = len(re.findall(r":PASS\b", stdout))
        fail_count = len(re.findall(r":FAIL\b", stdout))
        ignore_count = len(re.findall(r":IGNORE\b", stdout))
        total = pass_count + fail_count + ignore_count
        if total > 0:
            return {"tests": total, "failures": fail_count, "ignored": ignore_count}
    except Exception:
        pass

    return {"tests": 0, "failures": 0, "ignored": 0}


def aggregate_summary(root: Path) -> dict:
    # Prefer the project aggregator if present, then augment with local parsing to ensure counts.
    agg = {"generated_at": time.asctime(), "by_file": {}}
    agg_script = root / "tools" / "aggregate_unity.py"
    outpath = TMP_DIR / "canonical_unity_summary.json"
    if agg_script.exists():
        rc, _ = run_cmd([sys.executable, str(agg_script), "--output", str(outpath)])
        if rc == 0 and outpath.exists():
            try:
                agg = json.loads(outpath.read_text())
            except Exception:
                pass

    if not isinstance(agg, dict):
        agg = {"generated_at": time.asctime(), "by_file": {}}
    if "by_file" not in agg or not isinstance(agg.get("by_file"), dict):
        agg["by_file"] = {}

    # fallback/augmentation: scan canonical locations for numeric summary lines and merge
    import re

    canonical_pat = re.compile(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored")
    alt_run_pat = re.compile(r"Tests\s*run\s*:\s*(\d+)", re.IGNORECASE)
    alt_failed_pat = re.compile(r"Tests\s*failed\s*:\s*(\d+)", re.IGNORECASE)
    alt_passed_pat = re.compile(r"Tests\s*passed\s*:\s*(\d+)", re.IGNORECASE)
    unity_start_pat = re.compile(r"-----\s*UNITY(?:\s+TEST)?(?:\s+START)?", re.IGNORECASE)

    def parse_log(path: Path) -> dict | None:
        try:
            # Normalize runner exit-code quirks by reading the log as text.
            txt = path.read_text(errors="ignore")
        except Exception:
            return None

        try:
            # If a log captures multiple test restarts, only parse the last Unity block
            # to avoid inflating PASS/FAIL counts from earlier attempts.
            starts = list(unity_start_pat.finditer(txt))
            if starts:
                txt = txt[starts[-1].start():]
        except Exception:
            pass

        m = canonical_pat.search(txt)
        if m:
            return {"tests": int(m.group(1)), "failures": int(m.group(2)), "ignored": int(m.group(3))}

        m_run = alt_run_pat.search(txt)
        m_failed = alt_failed_pat.search(txt)
        m_passed = alt_passed_pat.search(txt)
        if m_run:
            total = int(m_run.group(1))
            failed = int(m_failed.group(1)) if m_failed else None
            passed = int(m_passed.group(1)) if m_passed else None
            if failed is None and passed is not None:
                failed = total - passed
            if passed is None and failed is not None:
                passed = total - failed
            ignored = total - (passed if passed is not None else 0) - (failed if failed is not None else 0)
            if ignored < 0:
                ignored = 0
            return {"tests": total, "failures": (failed if failed is not None else 0), "ignored": ignored}

        try:
            pass_count = len(re.findall(r":PASS\b", txt))
            fail_count = len(re.findall(r":FAIL\b", txt))
            ignore_count = len(re.findall(r":IGNORE\b", txt))
            total = pass_count + fail_count + ignore_count
            if total > 0:
                return {"tests": total, "failures": fail_count, "ignored": ignore_count}
        except Exception:
            return None
        return None

    files = [root / "esp_bt_audio_source" / "test" / "test_bluetooth" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test" / "test_app_audio" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test" / "test_manager" / "build" / "one_run_unity.log"]

    for f in files:
        if not f.exists():
            continue
        parsed = parse_log(f)
        key = str(f)
        existing = agg["by_file"].get(key, {}) if isinstance(agg.get("by_file"), dict) else {}
        if parsed:
            if not existing or int(existing.get("tests", 0) or 0) == 0:
                agg["by_file"][key] = parsed

    outpath.write_text(json.dumps(agg, indent=2))
    return agg


def parse_flash_time_from_log(path: Path) -> float:
    """Return the duration of the largest esptool write in the provided log.

    We only care about the flash stage for the main app image; bootloader and
    table writes are much smaller, so we choose the write with the largest byte
    count. Returns 0.0 on parse failure.
    """
    try:
        txt = path.read_text(errors="ignore")
    except Exception:
        return 0.0

    # capture both byte count and duration: "Wrote 841392 bytes ... in 11.6 seconds"
    pat = re.compile(
        r"Wrote\s+([0-9,]+)\s+bytes[\s\S]*?in\s+([0-9]+(?:\.[0-9]+)?)\s+seconds",
        re.IGNORECASE,
    )

    best_duration = 0.0
    best_bytes = -1
    for match in pat.finditer(txt):
        byte_str, duration_str = match.groups()
        try:
            byte_count = int(byte_str.replace(",", ""))
            duration = float(duration_str)
        except Exception:
            continue
        if byte_count > best_bytes:
            best_bytes = byte_count
            best_duration = duration

    return best_duration


def parse_ctest_duration(ctest_output: str) -> float:
    """Parse the ctest real time summary ("Total Test time (real) =   1.20 sec").

    Returns 0.0 on parse failure.
    """
    try:
        import re

        m = re.search(r"Total Test time \(real\) =\s*([0-9.]+)\s*sec", ctest_output)
        if m:
            return float(m.group(1))
    except Exception:
        pass
    return 0.0


def count_unity_results(path: Path) -> dict:
    """Count Unity per-test PASS/FAIL/IGNORE tokens as a last-resort fallback.

    Returns a dict: {"tests": N, "failures": F, "ignored": I}
    If the file cannot be read or no tokens found, returns {"tests": 0, "failures": 0, "ignored": 0}
    """
    try:
        txt = path.read_text(errors="ignore")
    except Exception:
        return {"tests": 0, "failures": 0, "ignored": 0}

    import re
    try:
        pass_count = len(re.findall(r":PASS\\b", txt))
        fail_count = len(re.findall(r":FAIL\\b", txt))
        ignore_count = len(re.findall(r":IGNORE\\b", txt))
        total = pass_count + fail_count + ignore_count
        if total == 0:
            return {"tests": 0, "failures": 0, "ignored": 0}
        return {"tests": total, "failures": fail_count, "ignored": ignore_count}
    except Exception:
        return {"tests": 0, "failures": 0, "ignored": 0}
