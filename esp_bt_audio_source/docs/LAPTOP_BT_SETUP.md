# Laptop BT Integration Tests Setup

## Prerequisites

- Python virtual environment with required packages:

```bash
. .venv/bin/activate
pip install pydbus pulsectl pyserial PyGObject
```

- ESP32 on `/dev/ttyUSB0` flashed with production firmware
- Laptop Bluetooth adapter powered on

## Running Tests

```bash
cd test/laptop_bt_tests
. .venv/bin/activate
python -m pytest test_connection.py -v --timeout=120
```

## Notes

- Tests require physical hardware
- CI environments without hardware will skip the suite
