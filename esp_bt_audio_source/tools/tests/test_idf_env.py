import os
import stat
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import idf_env  # noqa: E402


def make_eim_script(tools_dir: Path, version: str, idf_dir: Path) -> Path:
    """Create a fake EIM activation script that supports the -e option."""
    tools_dir.mkdir(parents=True, exist_ok=True)
    script = tools_dir / f"activate_idf_v{version}.sh"
    script.write_text(
        '#!/bin/sh\n'
        'if [ "$1" = "-e" ]; then\n'
        f'  echo "IDF_VERSION={version}"\n'
        f'  echo "IDF_PATH={idf_dir}"\n'
        '  exit 0\n'
        'fi\n'
    )
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    return script


def make_legacy(idf_dir: Path) -> Path:
    idf_dir.mkdir(parents=True, exist_ok=True)
    export = idf_dir / "export.sh"
    export.write_text("# fake export.sh\n")
    return export


def test_picks_newest_eim_script(tmp_path):
    tools = tmp_path / ".espressif" / "tools"
    make_eim_script(tools, "5.5", tmp_path / ".espressif" / "v5.5" / "esp-idf")
    newest = make_eim_script(tools, "6.1", tmp_path / ".espressif" / "v6.1" / "esp-idf")
    make_eim_script(tools, "6.0", tmp_path / ".espressif" / "v6.0" / "esp-idf")
    assert idf_env.find_idf_env_script(environ={}, home=tmp_path) == str(newest)


def test_version_ordering_is_numeric_not_lexical(tmp_path):
    tools = tmp_path / ".espressif" / "tools"
    make_eim_script(tools, "6.9", tmp_path / "a")
    newest = make_eim_script(tools, "6.10", tmp_path / "b")
    assert idf_env.find_idf_env_script(environ={}, home=tmp_path) == str(newest)


def test_idf_path_selects_matching_eim_script(tmp_path):
    tools = tmp_path / ".espressif" / "tools"
    old_dir = tmp_path / ".espressif" / "v6.0" / "esp-idf"
    old_dir.mkdir(parents=True)
    old = make_eim_script(tools, "6.0", old_dir)
    make_eim_script(tools, "6.1", tmp_path / ".espressif" / "v6.1" / "esp-idf")
    assert idf_env.find_idf_env_script(
        environ={"IDF_PATH": str(old_dir)}, home=tmp_path
    ) == str(old)


def test_idf_path_falls_back_to_its_export_sh(tmp_path):
    export = make_legacy(tmp_path / "custom" / "esp-idf")
    got = idf_env.find_idf_env_script(
        environ={"IDF_PATH": str(export.parent)}, home=tmp_path
    )
    assert got == str(export)


def test_legacy_default_used_when_no_eim(tmp_path):
    export = make_legacy(tmp_path / "esp" / "esp-idf")
    assert idf_env.find_idf_env_script(environ={}, home=tmp_path) == str(export)


def test_eim_preferred_over_legacy_default(tmp_path):
    make_legacy(tmp_path / "esp" / "esp-idf")
    eim = make_eim_script(
        tmp_path / ".espressif" / "tools", "6.1", tmp_path / ".espressif" / "v6.1" / "esp-idf"
    )
    assert idf_env.find_idf_env_script(environ={}, home=tmp_path) == str(eim)


def test_nothing_found_returns_none(tmp_path):
    assert idf_env.find_idf_env_script(environ={}, home=tmp_path) is None


def test_idf_tools_path_override(tmp_path):
    custom = tmp_path / "elsewhere"
    script = make_eim_script(custom, "6.1", tmp_path / "idf")
    assert idf_env.find_idf_env_script(
        environ={"IDF_TOOLS_PATH": str(custom)}, home=tmp_path
    ) == str(script)


def test_idf_path_for_eim_script_uses_dash_e(tmp_path):
    idf_dir = tmp_path / "v6.1" / "esp-idf"
    idf_dir.mkdir(parents=True)
    script = make_eim_script(tmp_path / "tools", "6.1", idf_dir)
    assert idf_env.idf_path_for_script(script) == idf_dir.resolve()


def test_idf_path_for_export_sh_is_its_directory(tmp_path):
    export = make_legacy(tmp_path / "esp-idf")
    assert idf_env.idf_path_for_script(export) == export.parent.resolve()


def test_cli_exits_nonzero_when_nothing_found(tmp_path):
    env = {k: v for k, v in os.environ.items() if k not in ("IDF_PATH", "IDF_TOOLS_PATH")}
    env["HOME"] = str(tmp_path)
    proc = subprocess.run(
        [sys.executable, str(TOOLS / "idf_env.py")],
        capture_output=True, text=True, env=env,
    )
    assert proc.returncode == 1
    assert "no ESP-IDF environment script found" in proc.stderr


def test_cli_prints_script_path(tmp_path):
    script = make_eim_script(
        tmp_path / ".espressif" / "tools", "6.1", tmp_path / ".espressif" / "v6.1" / "esp-idf"
    )
    env = {k: v for k, v in os.environ.items() if k not in ("IDF_PATH", "IDF_TOOLS_PATH")}
    env["HOME"] = str(tmp_path)
    proc = subprocess.run(
        [sys.executable, str(TOOLS / "idf_env.py")],
        capture_output=True, text=True, env=env,
    )
    assert proc.returncode == 0
    assert proc.stdout.strip() == str(script)
