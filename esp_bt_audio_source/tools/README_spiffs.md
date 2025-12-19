Ensure Unity test apps use the same SPIFFS partition

For reliable host/device tests you requested that the Unity test suites (for example `test_app`, `test_app2`, `test_app_audio`) use the same `spiffs` partition as the main app. There are two ways this can be achieved:
# SPIFFS image creation and flashing (256 KiB SPIFFS)

This document shows how to create a 256 KiB SPIFFS image and flash it to the project's `spiffs` partition. The partition entry may be defined at the project level (e.g. `esp_bt_audio_source/partitions.csv`) or per-app (for example `esp_bt_audio_source/test/test_app/partitions.csv`).

Partition layout change (applied in repo)
- `factory` shrunk from `0x1F0000` -> `0x1B0000` (was 2,031,616 bytes -> now 1,775,472 bytes)
- `spiffs` created at offset `0x1C0000` size `0x40000` (262,144 bytes = 256 KiB)

Files you can include:
- Use the existing normalized WAV in the repo:
  - `test/test_app/build/worker_long_norm.wav` (already present in your build outputs)

## Repo helpers (2025-11-10 refresh)

- `tools/make_spiffs.py` now auto-detects whether `mkspiffs` supports the required flags; if not, it falls back to ESP-IDF's `spiffsgen.py` so the generated image always honors `CONFIG_SPIFFS_META_LENGTH` and `CONFIG_SPIFFS_OBJ_NAME_LEN`.
- `tools/run_unity.py` invokes the helper above before flashing Unity apps and reuses the shared SPIFFS image located under `esp_bt_audio_source/main/assets/spiffs`.
- Test apps include a lightweight `spiffs_dep` component that wires in the shared assets directory and a symlinked `partitions.csv`, keeping the partition layout aligned with the main project.

Recommended approach (using mkspiffs from ESP-IDF)
1) Ensure IDF environment is exported in your shell (so `$IDF_PATH` is set):

```bash
. $HOME/esp/esp-idf/export.sh
```

2) Create a folder with files to pack into SPIFFS (example):

```bash
mkdir -p /tmp/spiffs_root
# Example: copy a WAV produced by the *main* app build. Adjust path if your build places artifacts elsewhere.
cp esp_bt_audio_source/main/build/worker_long_norm.wav /tmp/spiffs_root/worker_long_norm.wav
# Or copy from another app/build output if you prefer: cp esp_bt_audio_source/test/test_app/build/... /tmp/spiffs_root/
# Add any other files you want under /tmp/spiffs_root
```

3) Use the `mkspiffs` binary shipped with ESP-IDF to build the image. Typical path:

```bash
$IDF_PATH/components/spiffs/mkspiffs/mkspiffs -c /tmp/spiffs_root -b 4096 -p 256 -s 0x40000 spiffs.bin
```

Explanation:
- `-c`: source dir
- `-b 4096`: block size (use default 4096)
- `-p 256`: page size
- `-s 0x40000`: total image size (must match partition size: 0x40000 = 262,144)

Alternative: If your system has `mkspiffs` in PATH you can call it directly. If the binary bundled with your IDF does not support long file names/meta lengths, rerun the helper with `--prefer-spiffsgen` to force the Python fallback:

```bash
python3 esp_bt_audio_source/tools/make_spiffs.py \
  -c esp_bt_audio_source/main/assets/spiffs \
  -s 0x40000 \
  -o esp_bt_audio_source/main/assets/spiffs/spiffs.bin \
  --prefer-spiffsgen
```

4) Flash the generated `spiffs.bin` to the partition offset. First locate the `spiffs` entry and offset for the app you will flash (project-level CSV or per-app CSV):

```bash
# Find the spiffs entry in the project-level CSV (or check the app folder for its own partitions.csv)
grep -i '^spiffs' esp_bt_audio_source/partitions.csv || true
# Or search all app partition CSVs
grep -R "^spiffs" -n esp_bt_audio_source || true
```

Once you have the offset (for example `0x1C0000`) flash the image to that offset:

```bash
# Using esptool.py directly (explicit offset)
python $IDF_PATH/components/esptool_py/esptool/esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x1C0000 spiffs.bin

# Or flash the entire app (application + partitions) from the app directory:
idf.py -p /dev/ttyUSB0 flash

# Note: flashing the entire app will write the partition table too. To update only SPIFFS prefer the esptool write_flash approach.

```

`tools/run_unity.py` performs both the SPIFFS build and flash steps automatically when you pass `--project-dir`; only use the manual commands above when you need to refresh the filesystem outside the Unity runner.

Optional helper: a Python script to build the SPIFFS image

If you prefer a small helper that locates `mkspiffs` (via `$IDF_PATH` or your PATH) and builds the image for you, the repository includes `tools/make_spiffs.py`. Example:

```bash
# From the repository root (example):
python3 esp_bt_audio_source/tools/make_spiffs.py \
  -c esp_bt_audio_source/main/assets/spiffs \
  -s 0x40000 \
  -o esp_bt_audio_source/main/assets/spiffs/spiffs.bin
```

The script prints the command it runs (either `mkspiffs` or `spiffsgen.py`) and reports the created image size. If neither tool can be located it explains how to install them or export `$IDF_PATH`.
```

5) Verify the SPIFFS image
- There are multiple ways to verify: use `mkspiffs` to list contents (some builds support a listing flag), or add a small firmware test that mounts SPIFFS and lists files.

Notes and safety
- I edited `esp_bt_audio_source/partitions.csv` in the repo to shrink `factory` and add `spiffs`. This will change the partition table built into subsequent builds. Make sure the final production app image (release) fits into the new `factory` size. I recommend leaving at least 32–128 KiB slack for future growth.
- If you later want a larger SPIFFS (e.g., 512 KiB), change `0x40000` to `0x80000` and adjust the `factory` size accordingly.

1) Use the project-level `esp_bt_audio_source/partitions.csv` (recommended)
  - Remove per-app `partitions.csv` files (if present) so the build system uses the project-level CSV, or configure the per-app builds to include/point to the top-level CSV.

2) Keep per-app `partitions.csv` files but ensure they contain an identical `spiffs` entry (same offset and size):

```bash
# Example: check per-app CSVs for a spiffs entry
grep -R "^spiffs" -n esp_bt_audio_source || true

# If a per-app partitions.csv exists, edit it so the spiffs line matches the main app:
# spiffs, data, 0x1C0000, 0x40000, spiffs
```

Recommendation: prefer option (1) so all builds automatically share the same partition layout and you avoid divergence between test and main images.

If you want, I can:
- Create the `spiffs.bin` here (if a suitable `mkspiffs` binary is available in this environment), and flash it for you (requires a connected device and correct port). Ask me to run these steps and I will attempt them.
- Or I can produce the exact `apply_patch` diff (already applied) and a small test firmware snippet to read the file from SPIFFS and log its size.

