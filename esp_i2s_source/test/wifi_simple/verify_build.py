#!/usr/bin/env python3
"""Verify the generated ESP-IDF configuration and flash layout."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SDKCONFIG = ROOT / "sdkconfig"
BUILD = ROOT / "build"


class VerificationError(RuntimeError):
    pass


def parse_sdkconfig(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise VerificationError(
            f"missing {path}; run a clean idf.py build first"
        )

    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value.strip().strip('"')
    return values


def require_config(
    values: dict[str, str],
    key: str,
    expected: str,
) -> None:
    actual = values.get(key)
    if actual != expected:
        raise VerificationError(
            f"{key}: expected {expected!r}, got {actual!r}"
        )


def reject_enabled(values: dict[str, str], key: str) -> None:
    if values.get(key) == "y":
        raise VerificationError(f"{key} must not be enabled")


def parse_flash_files() -> dict[int, str]:
    json_path = BUILD / "flasher_args.json"
    if json_path.is_file():
        data = json.loads(json_path.read_text(encoding="utf-8"))
        raw_files = data.get("flash_files")
        if isinstance(raw_files, dict):
            result: dict[int, str] = {}
            for raw_offset, filename in raw_files.items():
                result[int(str(raw_offset), 0)] = str(filename)
            return result

    text_path = BUILD / "flash_args"
    if text_path.is_file():
        result: dict[int, str] = {}
        pattern = re.compile(r"^(0x[0-9a-fA-F]+)\s+(.+\.bin)\s*$")
        for raw in text_path.read_text(encoding="utf-8").splitlines():
            match = pattern.match(raw.strip())
            if match:
                result[int(match.group(1), 16)] = match.group(2)
        if result:
            return result

    raise VerificationError(
        "could not find flash file mapping in "
        "build/flasher_args.json or build/flash_args"
    )


def require_file_at(
    files: dict[int, str],
    offset: int,
    expected_fragment: str,
) -> None:
    actual = files.get(offset)
    if actual is None:
        raise VerificationError(
            f"no image mapped at 0x{offset:x}; got {files!r}"
        )
    if expected_fragment not in actual:
        raise VerificationError(
            f"0x{offset:x}: expected path containing "
            f"{expected_fragment!r}, got {actual!r}"
        )


def main() -> int:
    try:
        cfg = parse_sdkconfig(SDKCONFIG)

        require_config(cfg, "CONFIG_IDF_TARGET", "esp32s3")
        require_config(cfg, "CONFIG_ESPTOOLPY_FLASHSIZE", "16MB")
        require_config(
            cfg,
            "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG",
            "y",
        )

        reject_enabled(cfg, "CONFIG_SPIRAM")
        require_config(cfg, "CONFIG_PARTITION_TABLE_CUSTOM", "y")
        require_config(
            cfg,
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME",
            "../../partitions.csv",
        )

        files = parse_flash_files()
        require_file_at(files, 0x0000, "bootloader")
        require_file_at(files, 0x8000, "partition")
        require_file_at(files, 0x20000, "wifi_simple_test.bin")

    except (OSError, ValueError, json.JSONDecodeError, VerificationError) as exc:
        print(f"VERIFY|WIFI_SCAN_BUILD|FAIL|{exc}", file=sys.stderr)
        return 1

    print("VERIFY|WIFI_SCAN_BUILD|PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())