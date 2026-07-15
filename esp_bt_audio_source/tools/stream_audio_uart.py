#!/usr/bin/env python3
import sys
try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")
