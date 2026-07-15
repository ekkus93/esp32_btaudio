# Laptop BT Tests TODO

## Prerequisites

- Python virtual environment with required packages:

```bash
. .venv/bin/activate
pip install pydbus pulsectl pyserial
```

## Running Tests

```bash
cd test/laptop_bt_tests
. .venv/bin/activate
python -m pytest test_connection.py test_autoconnect.py test_streaming.py test_control.py test_e2e.py -v --timeout=120
```

## CI Note

Tests are tagged `@pytest.mark.laptop_bt`; CI environments without the adapter will skip the entire suite cleanly.
