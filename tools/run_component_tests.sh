#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

COMP_ROOT="esp_i2s_source/components/components"
if [ ! -d "$COMP_ROOT" ]; then
  echo "Component root $COMP_ROOT not found" >&2
  exit 1
fi

for comp in "$COMP_ROOT"/*; do
  [ -d "$comp" ] || continue
  echo "\n=== Running tests for component: $(basename "$comp") ==="
  # Run pytest for tests under this component. Set PYTHONPATH so local module
  # imports resolve to this component first.
  # Safely prepend component directory to PYTHONPATH. If PYTHONPATH is unset,
  # avoid unbound variable expansion under `set -u`.
  if [ -z "${PYTHONPATH+x}" ]; then
    export PYTHONPATH="$ROOT/$comp"
  else
    export PYTHONPATH="$ROOT/$comp:$PYTHONPATH"
  fi
  # Run tests under the component directory only to limit scope.
  # Use the component directory as the working directory so tests that use
  # relative paths (e.g. subprocess('../fatfsgen.py')) resolve correctly.
  # If the component contains test directories (test_*/tests*/...), run pytest
  # from each test directory so tests that use relative paths resolve properly.
  tests_found=0
  for td in "$comp"/test* "$comp"/tests*; do
    [ -d "$td" ] || continue
    tests_found=1
    # Prefer running from deeper 'scripts/thread-cert' if present (openthread
    # test suites expect tests/scripts/thread-cert to be on the import path).
    if [ -d "$td/scripts/thread-cert" ]; then
      run_dir="$td/scripts/thread-cert"
    elif [ -d "$td/scripts" ]; then
      run_dir="$td/scripts"
    else
      run_dir="$td"
    fi
    echo "Running tests in $(realpath --relative-to="$ROOT" "$run_dir")"
    pushd "$run_dir" > /dev/null
    pytest -q . --maxfail=1 || rc=$? || rc=$?
    popd > /dev/null
    rc=${rc:-0}
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 5 ]; then
      break
    fi
  done
  if [ "$tests_found" -eq 0 ]; then
    # No explicit test directories found; run pytest at component root
    pushd "$comp" > /dev/null
    pytest -q . --maxfail=1 || rc=$? || rc=$?
    popd > /dev/null
  fi
  rc=${rc:-0}
  if [ "$rc" -eq 0 ]; then
    echo "Component $(basename "$comp"): OK"
  elif [ "$rc" -eq 5 ]; then
    echo "Component $(basename "$comp"): no tests collected, skipping"
  else
    echo "Component $(basename "$comp"): FAILED or ERROR (rc=$rc) - stopping" >&2
    exit 1
  fi
done

echo "\nAll component tests under $COMP_ROOT passed (or no tests found)."
