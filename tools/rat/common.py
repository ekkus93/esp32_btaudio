"""Shared paths for the rat package."""
from pathlib import Path

# tools/rat/common.py -> parents[2] is the repo root.
ROOT = Path(__file__).resolve().parents[2]
TMP_DIR = ROOT / "tmp"
TMP_DIR.mkdir(exist_ok=True)
