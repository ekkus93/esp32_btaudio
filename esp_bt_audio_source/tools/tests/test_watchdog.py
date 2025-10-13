import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"


def run_host_driver(cmd_args, timeout=5):
    """Run host pairing driver as a subprocess and return (returncode, stdout, stderr).

    Args:
        cmd_args: list of command-line arguments (first element is the python executable).
        timeout: seconds to wait for process to exit.
    """
    proc = subprocess.Popen(
        cmd_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    try:
        out, err = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
    return proc.returncode, out, err


def test_watchdog_emits_json(tmp_path, monkeypatch):
    # Locate the host driver script
    script = TOOLS / "host_pairing_driver.py"
    assert script.exists(), f"host driver not found: {script}"

    # Run the script in simulate mode; it should emit a single JSON line
    rc, out, err = run_host_driver([sys.executable, str(script), '--simulate'])

    assert rc == 0, f"host driver exited nonzero: rc={rc} stderr={err}"

    # The script may write both stdout and stderr; find the JSON in stdout first
    line = None
    for candidate in out.splitlines():
        candidate = candidate.strip()
        if not candidate:
            continue
        try:
            obj = json.loads(candidate)
            line = candidate
            break
        except json.JSONDecodeError:
            continue

    # Fallback: try stderr if stdout didn't contain JSON
    if line is None:
        for candidate in err.splitlines():
            candidate = candidate.strip()
            if not candidate:
                continue
            try:
                obj = json.loads(candidate)
                line = candidate
                break
            except json.JSONDecodeError:
                continue

    assert line is not None, "No JSON line emitted by host driver"

    # Basic JSON shape checks
    obj = json.loads(line)
    assert isinstance(obj, dict)
    assert "status" in obj
    # host_pairing_driver uses 'success'/'failed'/'timeout' for its status; accept those
    assert obj["status"] in ("success", "failed", "timeout", "ok", "fail")


if __name__ == "__main__":
    # Quick local smoke run
    rc, out, err = run_host_driver([sys.executable, str(TOOLS / "host_pairing_driver.py")])
    print("rc:", rc)
    print("out:", out)
    print("err:", err)
