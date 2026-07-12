"""Remove canonical logs/JSON from prior runs so fresh artifacts appear."""
from __future__ import annotations

from pathlib import Path

from .common import TMP_DIR


def _unlink_artifact(path: Path) -> bool:
    try:
        path.unlink()
        print(f"Removed artifact: {path}")
        return True
    except FileNotFoundError:
        return False
    except IsADirectoryError:
        return False
    except Exception as exc:
        print(f"WARNING: failed to remove {path}: {exc}")
        return False


def cleanup_previous_artifacts(root: Path, remove_host: bool, remove_device: bool) -> None:
    """Remove canonical logs/json outputs from prior runs so fresh artifacts are produced."""
    print("\n== Cleaning previous run artifacts ==")

    # Always clear summary JSON outputs and runner captures in tmp/.
    for candidate in TMP_DIR.glob("run_all_tests_summary*.json"):
        _unlink_artifact(candidate)
    for candidate in TMP_DIR.glob("canonical_unity_summary*.json"):
        _unlink_artifact(candidate)
    for candidate in TMP_DIR.glob("runner_*_stdout.log"):
        _unlink_artifact(candidate)

    if remove_host:
        for candidate in TMP_DIR.glob("host_ctest_output*.log"):
            _unlink_artifact(candidate)

    if remove_device:
        unity_projects = [
            root / "esp_bt_audio_source" / "test" / "test_bluetooth",
            root / "esp_bt_audio_source" / "test" / "test_app_audio",
            root / "esp_bt_audio_source" / "test" / "test_manager",
        ]
        for proj in unity_projects:
            _unlink_artifact(proj / "build" / "one_run_unity.log")
