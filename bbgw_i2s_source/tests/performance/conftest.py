"""
Pytest configuration for performance tests.

Performance tests validate non-functional requirements (NFRs) such as CPU usage,
memory consumption, and I2S timing accuracy. These tests require hardware and
should only run on the target BeagleBone Green Wireless platform.
"""

import pytest


def pytest_configure(config):
    """Register custom markers for performance tests."""
    config.addinivalue_line(
        "markers",
        "performance: mark test as performance validation (requires hardware)"
    )
    
    # Note: 'hardware' marker already defined in root conftest.py
    # Performance tests are a subset of hardware tests


def pytest_addoption(parser):
    """Add command-line options for performance tests."""
    # Note: --run-hardware option already defined in root conftest.py
    # Performance tests use the same flag
    pass


def pytest_collection_modifyitems(config, items):
    """
    Auto-skip performance tests unless running on hardware.
    
    Performance tests are automatically skipped unless the --run-hardware
    flag is provided. This prevents false failures on development machines.
    """
    # Check if --run-hardware flag is set
    run_hardware = config.getoption("--run-hardware", default=False)
    
    if not run_hardware:
        skip_hardware = pytest.mark.skip(reason="Performance tests require --run-hardware flag")
        for item in items:
            # Skip all tests in performance directory
            if "performance" in str(item.fspath):
                item.add_marker(skip_hardware)


@pytest.fixture
def api_url():
    """
    Provide the base URL for the Flask API.
    
    For local testing: http://localhost:5000
    For remote Pi: http://<pi-ip>:5000
    """
    return "http://localhost:5000"


@pytest.fixture
def verify_hardware():
    """
    Verify hardware dependencies before running performance tests.
    
    Checks:
    - I2S device available (ALSA)
    - Flask web server running
    - UART device available (optional)
    """
    import subprocess
    import os
    import requests
    
    errors = []
    
    # Check for I2S ALSA device
    try:
        result = subprocess.run(
            ["aplay", "-l"],
            capture_output=True,
            text=True,
            timeout=5
        )
        if "snd_rpi_i2s" not in result.stdout and "bcm2835" not in result.stdout and "BBGW-I2S" not in result.stdout and "davinci-mcasp" not in result.stdout:
            errors.append("I2S ALSA device not found (check McASP Device Tree overlay)")
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        errors.append(f"Cannot check ALSA devices: {e}")
    
    # Check if Flask web server is running
    try:
        response = requests.get("http://localhost:5000/status", timeout=5)
        if response.status_code != 200:
            errors.append(f"Flask API returned status {response.status_code}")
    except requests.exceptions.RequestException as e:
        errors.append(f"Flask web server not running: {e}")
    
    # Check for UART device (optional - some tests don't need it)
    uart_available = os.path.exists("/dev/ttyAMA0")
    
    # If there are errors, fail the test
    if errors:
        pytest.fail("Hardware verification failed:\n  - " + "\n  - ".join(errors))
    
    return {
        "i2s": True,
        "web_server": True,
        "uart": uart_available
    }
