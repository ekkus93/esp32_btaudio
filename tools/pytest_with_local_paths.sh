#!/usr/bin/env bash
# Helper to run pytest with a PYTHONPATH that includes local component
# directories containing top-level Python modules so tests that import
# repository-local modules (e.g. gen_crt_bundle) can find them.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

# Find directories that contain non-test python files (skip tests and .venv)
PYDIRS=()
while IFS= read -r -d $'\0' dir; do
  PYDIRS+=("$dir")
done < <(find . -type f -name "*.py" -not -path "./.venv_clean/*" -not -path "./.venv/*" -not -path "./tests/*" -not -path "./**/test_*" -print0 | xargs -0 -n1 dirname | sort -u -z)

# Build PYTHONPATH
PYTHONPATH=""
for d in "${PYDIRS[@]}"; do
  # Convert to absolute path and append
  absd=$(cd "$d" && pwd)
  if [ -z "$PYTHONPATH" ]; then
    PYTHONPATH="$absd"
  else
    PYTHONPATH="$PYTHONPATH:$absd"
  fi
done

export PYTHONPATH
echo "Running pytest with PYTHONPATH containing ${#PYDIRS[@]} directories"
echo "PYTHONPATH=$PYTHONPATH"

# Forward any args to pytest
pytest "$@"
