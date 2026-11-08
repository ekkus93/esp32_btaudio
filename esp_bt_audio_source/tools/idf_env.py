#!/usr/bin/env python3
"""Locate the shell script that activates an ESP-IDF environment.

Supports both layouts we have used:

* EIM installs (ESP-IDF 6.x): ``<IDF_TOOLS_PATH>/activate_idf_<version>.sh``,
  default ``~/.espressif/tools``.
* Legacy installs: ``$IDF_PATH/export.sh`` or ``~/esp/esp-idf/export.sh``.

Selection order:

1. ``IDF_PATH`` is set: the EIM script that reports that same ``IDF_PATH``,
   else ``$IDF_PATH/export.sh``.
2. Otherwise the newest EIM script.
3. Otherwise ``~/esp/esp-idf/export.sh``.

IMPORTANT: run the result with ``bash -c`` / ``sh -c`` (``. script && cmd``),
never ``source`` it from inside a script file. EIM's activate script decides
"sourced or executed" from ``$0`` and calls ``exit 1`` when it is sourced from
a script file, which silently kills the caller. It also fails under ``set -u``.

CLI: prints the script path (``--idf-path`` prints that environment's
IDF_PATH instead); exits 1 with a message on stderr when nothing is found.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Mapping, Optional

_EIM_SCRIPT_RE = re.compile(r"^activate_idf_v?(\d+(?:\.\d+)*)\.sh$")


def _eim_scripts(tools_dir: Path) -> list:
    """EIM activation scripts in ``tools_dir``, newest version first."""
    if not tools_dir.is_dir():
        return []
    found = []
    for entry in tools_dir.iterdir():
        match = _EIM_SCRIPT_RE.match(entry.name)
        if match and entry.is_file():
            key = tuple(int(part) for part in match.group(1).split("."))
            found.append((key, entry))
    return [entry for _, entry in sorted(found, key=lambda item: item[0], reverse=True)]


def idf_path_for_script(script) -> Optional[Path]:
    """Return the ESP-IDF directory an activation script belongs to, or None.

    EIM scripts report it through their supported ``-e`` option, which prints
    the environment without sourcing anything. ``export.sh`` lives in the
    ESP-IDF directory itself.
    """
    script = Path(script)
    if script.name == "export.sh":
        return script.resolve().parent
    try:
        result = subprocess.run(
            ["sh", str(script), "-e"], capture_output=True, text=True, timeout=30
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    for line in result.stdout.splitlines():
        if line.startswith("IDF_PATH="):
            return Path(line.split("=", 1)[1]).resolve()
    return None


def find_idf_env_script(
    environ: Optional[Mapping[str, str]] = None, home: Optional[Path] = None
) -> Optional[str]:
    """Return the path of an ESP-IDF activation script, or None if not found."""
    environ = os.environ if environ is None else environ
    home = Path(home) if home else Path(os.path.expanduser("~"))
    tools_dir = Path(environ.get("IDF_TOOLS_PATH") or home / ".espressif" / "tools")
    candidates = _eim_scripts(tools_dir)

    idf_path = environ.get("IDF_PATH")
    if idf_path:
        wanted = Path(idf_path).resolve()
        for script in candidates:
            if idf_path_for_script(script) == wanted:
                return str(script)
        legacy = Path(idf_path) / "export.sh"
        if legacy.is_file():
            return str(legacy)

    if candidates:
        return str(candidates[0])

    legacy_default = home / "esp" / "esp-idf" / "export.sh"
    return str(legacy_default) if legacy_default.is_file() else None


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--idf-path",
        action="store_true",
        help="print the ESP-IDF directory of the selected environment instead",
    )
    args = parser.parse_args(argv)

    script = find_idf_env_script()
    if script is None:
        print(
            "ERROR: no ESP-IDF environment script found (install with EIM, "
            "or set IDF_PATH to an ESP-IDF checkout containing export.sh)",
            file=sys.stderr,
        )
        return 1
    if args.idf_path:
        idf_dir = idf_path_for_script(script)
        if idf_dir is None:
            print(f"ERROR: could not determine IDF_PATH from {script}", file=sys.stderr)
            return 1
        print(idf_dir)
    else:
        print(script)
    return 0


if __name__ == "__main__":
    sys.exit(main())
