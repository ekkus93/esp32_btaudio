# Pairing serial log symbolizer

This tool helps you convert addresses in the serial capture produced during on-device pairing runs into human-readable file:line symbols using `addr2line` and the built ELF.

Why
- The `build/pairing_e2_logs/serial.log` file contains runtime addresses and allocator traces. Mapping those addresses back to source lines speeds root-cause analysis.

What was added
- `symbolize_pairing.py` — small Python3 helper that scans the serial log for hex addresses (0x...) and runs `addr2line` against the provided ELF. Produces a symbolized log with resolved function names and file:line info.

Usage example

```bash
cd /home/phil/work/esp32/esp32_btaudio
python3 tools/symbolize_pairing/symbolize_pairing.py \
    --log build/pairing_e2_logs/serial.log \
    --elf esp_bt_audio_source/build/esp_bt_audio_source.elf \
    --out build/pairing_e2_logs/serial.symbolized.log
```

Toolchain notes
- The script tries to find a suitable `addr2line` automatically. It prefers common ESP-IDF toolchain binaries such as `xtensa-esp32-elf-addr2line` then falls back to `addr2line` in PATH.
- If your toolchain's addr2line is not on PATH, set the `ADDR2LINE` environment variable to its absolute path before running:

```bash
export ADDR2LINE=/opt/xtensa-esp32-elf/bin/xtensa-esp32-elf-addr2line
```

Troubleshooting
- If the script reports `addr2line not found`, ensure your ESP-IDF toolchain is installed and the tool is available in PATH or set via `ADDR2LINE`.
- Make sure the ELF you pass is the exact binary that produced the run (addresses differ across builds). Prefer the ELF in the same `build/` directory that produced the serial log.

Tips
- If you want only addresses for allocator traces, use `grep` to prefilter the log and pipe into the script (but the script requires a file path). You can create a temporary file first.

Example workflow

1. Capture serial monitor to `build/pairing_e2_logs/serial.log` during the on-device run.
2. Run the symbolizer against the ELF used for that build.
3. Inspect `serial.symbolized.log` for a readable timeline.
