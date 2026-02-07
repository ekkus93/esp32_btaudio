"""
Pytest configuration for integration tests.

Defines markers and fixtures for hardware-dependent integration tests.
"""

import pytest


def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers",
        "hardware: mark test as requiring physical hardware (BeagleBone Green Wireless, ESP32, etc.)"
    )
    config.addinivalue_line(
        "markers",
        "slow: mark test as slow running (minutes to hours)"
    )


def pytest_collection_modifyitems(config, items):
    """
    Automatically skip hardware tests if not running on BeagleBone Green Wireless.
    
    Hardware tests can be forced to run with: pytest --run-hardware
    """
    skip_hardware = pytest.mark.skip(reason="Hardware tests require BeagleBone Green Wireless setup")
    
    # Check if we should run hardware tests
    run_hardware = config.getoption("--run-hardware", default=False)
    
    for item in items:
        if "hardware" in item.keywords and not run_hardware:
            item.add_marker(skip_hardware)


def pytest_addoption(parser):
    """Add command line options for integration tests."""
    parser.addoption(
        "--run-hardware",
        action="store_true",
        default=False,
        help="Run hardware integration tests (requires BeagleBone Green Wireless setup)"
    )
