#!/bin/bash
cd test/laptop_bt_tests
. .venv/bin/activate
python -m pytest test_connection.py test_autoconnect.py test_streaming.py test_control.py test_e2e.py -v --timeout=120
