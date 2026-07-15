# Host Unit Tests

## Quick Start

```bash
cd test/host_test/build_host_tests
ctest --output-on-failure
```

## Setup

Create a virtual environment if you don't have one:

```bash
python3 -m venv test_env
. test_env/bin/activate
```

## Testing Workflow

1. Activate your Python virtual environment
2. Build the test binaries
3. Run ctest

```bash
. test_env/bin/activate
cd test/host_test/build_host_tests
ctest --output-on-failure
```
