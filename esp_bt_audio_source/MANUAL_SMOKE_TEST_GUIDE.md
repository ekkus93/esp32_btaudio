# Manual Smoke Test Guide

## Prerequisites

- ESP32 WROOM32
- Python virtual environment with pyserial:

```bash
. .venv/bin/activate
pip install pyserial
```

## Running UARTAUDIO Test

```bash
. .venv/bin/activate
python tools/stream_audio_uart.py \
    -p /dev/ttyUSB0 \
    --input /path/to/wav/file.wav \
    --baud 921600
```

## Command Interface

Connect via USB serial to send commands:

```bash
screen /dev/ttyUSB0 115200
```

Or use a terminal program of your choice.
