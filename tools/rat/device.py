"""On-device Unity suite runner (drives tools/run_unity.py over a pty)."""
from __future__ import annotations

import os
import shlex
import sys
from pathlib import Path

from .proc import run_with_pty
from .common import TMP_DIR


def run_device_suite(project_root: Path, runner_script: Path, port: str, timeout: int, source_idf: str | None, spiffs_image: str | None = None, spiffs_offset: str | None = None, force_spiffs: bool = False) -> dict:
    # runner_script is a path to tools/run_unity.py inside the repo
    proj = project_root.resolve()
    summary = {"project": str(proj), "rc": None, "output_file": None, "stdout": None}
    # prepare per-suite runner stdout capture path (we'll tee pty output here)
    name = proj.name
    capture_outpath = TMP_DIR / f"runner_{name}_stdout.log"
    # start fresh
    try:
        capture_outpath.parent.mkdir(parents=True, exist_ok=True)
        if capture_outpath.exists():
            capture_outpath.unlink()
    except Exception:
        pass

    # Build the invocation
    cmd = [sys.executable, str(runner_script), "--project-root", str(proj), "--port", port, "--timeout", str(timeout)]
    # If caller provided a spiffs image / offset, forward them to the runner
    if spiffs_image:
        cmd += ["--spiffs-image", str(spiffs_image)]
    if spiffs_offset:
        cmd += ["--spiffs-offset", str(spiffs_offset)]
    if force_spiffs:
        cmd += ["--force-spiffs"]

    # If caller requested sourcing the IDF export, run under bash -lc to source first
    # Use an interactive/non-capturing subprocess.run here so the runner can attach
    # to the terminal/pty and block until completion (the runner writes canonical
    # logs itself which we then collect). We still catch timeouts and return codes.
    if source_idf:
        # create a single shell command
        shell_cmd = f". {shlex.quote(source_idf)} >/dev/null 2>&1 && {shlex.join([shlex.quote(p) for p in cmd])}"
        # run under a pty so the monitor/runner can work interactively; tee output to capture_outpath
        rc, out = run_with_pty(shell_cmd, shell=True, timeout=timeout + 60, capture_path=str(capture_outpath))
    else:
        # run under a pty to allow idf.py monitor and other interactive operations; tee output
        rc, out = run_with_pty(cmd, shell=False, timeout=timeout + 60, capture_path=str(capture_outpath))

    summary["rc"] = rc
    summary["stdout"] = out

    # find canonical capture
    one_run = proj / "build" / "one_run_unity.log"
    if one_run.exists():
        summary["output_file"] = str(one_run)
    else:
        # if runner stores elsewhere, try to find idf monitor captures
        alt = proj / "build" / "log"
        if alt.exists():
            # pick most recent file containing "one_run_unity" or any .log
            logs = sorted([p for p in alt.iterdir() if p.suffix in (".log", "")], key=os.path.getmtime)
            if logs:
                summary["output_file"] = str(logs[-1])

    # runner output was teed to capture_outpath by run_with_pty
    summary["runner_stdout_log"] = str(capture_outpath)
    return summary
