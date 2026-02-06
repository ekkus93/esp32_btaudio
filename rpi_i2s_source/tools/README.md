# Test Tools

Automated testing tools for the RPi I2S Audio Source project.

## Available Tools

### `run_test_scenarios.py` - Automated Test Scenario Runner

Comprehensive test runner that executes various test scenarios against a running RPi I2S Source instance and generates detailed HTML or Markdown reports.

**Features:**
- 7 test scenarios covering all major functionality
- Automated API calls with validation
- Pass/Fail/Skip status tracking
- Detailed step-by-step execution logs
- HTML and Markdown report generation
- Timing and performance metrics
- Hardware-aware (skips UART/Bluetooth tests if unavailable)

**Usage:**

```bash
# List available scenarios
python tools/run_test_scenarios.py --list

# Run all scenarios
python tools/run_test_scenarios.py --all

# Run specific scenarios
python tools/run_test_scenarios.py --scenario tone --scenario sweep

# Generate HTML report
python tools/run_test_scenarios.py --all --format html

# Generate Markdown report
python tools/run_test_scenarios.py --all --format markdown

# Generate both formats
python tools/run_test_scenarios.py --all --format both

# Custom API URL
python tools/run_test_scenarios.py --all --api-url http://192.168.1.100:5000

# Custom output path
python tools/run_test_scenarios.py --all --output my_report.html
```

**Test Scenarios:**

1. **System Health** - Verifies API accessibility, I2S driver status, and system resources
2. **Tone Generation** - Tests single-tone with frequency/amplitude/stereo mode changes
3. **Frequency Sweep** - Tests 20 Hz → 20 kHz logarithmic sweep
4. **WAV Playback** - Tests WAV file loading (skips if no files available)
5. **Multi-Tone** - Tests simultaneous multi-tone generation (chords)
6. **UART Communication** - Tests ESP32 UART interface (requires hardware)
7. **Bluetooth Control** - Tests Bluetooth commands (requires hardware)

**Requirements:**
- Running RPi I2S Source instance (`python main.py`)
- `requests` library (`pip install requests`)
- Optional: ESP32 with UART for hardware tests

**Output:**

Reports are generated in `test_results/` directory by default:
- HTML reports: Beautiful, responsive web pages with color-coded results
- Markdown reports: Plain text format for version control/CI/CD

**Exit Codes:**
- `0`: All tests passed or skipped
- `1`: One or more tests failed

**Example Output:**

```
============================================================
Running scenario: tone
============================================================
  Step 1: Setting 1 kHz tone...
  ✓ 1 kHz tone set
  Step 2: Verifying audio source...
  ✓ Audio source confirmed as tone
  Step 3: Changing to 440 Hz...
  ✓ Frequency changed to 440 Hz
  Step 4: Testing stereo modes...
    ✓ Mode 'mono' OK
    ✓ Mode 'left' OK
    ✓ Mode 'right' OK
    ✓ Mode 'dual' OK

============================================================
TEST SUMMARY
============================================================
Total scenarios: 7
  ✅ Passed:  5
  ❌ Failed:  0
  ⊘ Skipped:  2
Total duration: 12.34s
============================================================

✓ HTML report generated: test_results/test_report_20260206_143022.html
```

---

## Adding New Test Scenarios

To add a new test scenario:

1. Add method to `TestScenarioRunner` class:
   ```python
   def _scenario_your_test(self) -> ScenarioResult:
       """Test scenario: Your test description."""
       result = ScenarioResult(
           name="Your Test Name",
           description="What this test validates",
           status=TestResult.PASS,
           duration=0.0
       )
       
       # Step 1: Do something
       print("  Step 1: Doing something...")
       response = self._api_post("/api/endpoint", {...})
       if response and response.get('status') == 'ok':
           result.add_step("Step name", TestResult.PASS)
       else:
           result.status = TestResult.FAIL
           result.add_step("Step name", TestResult.FAIL)
       
       return result
   ```

2. Add scenario to `run_scenario()` dispatch dict:
   ```python
   scenarios = {
       ...
       'your_test': self._scenario_your_test,
   }
   ```

3. Add to scenario list in `run_all_scenarios()` and `list_scenarios()`

4. Test your new scenario:
   ```bash
   python tools/run_test_scenarios.py --scenario your_test
   ```

---

## CI/CD Integration

Use in continuous integration pipelines:

```yaml
# GitHub Actions example
- name: Run automated test scenarios
  run: |
    python main.py &  # Start server in background
    sleep 5
    python tools/run_test_scenarios.py --all --format both
    kill %1  # Stop server

- name: Upload test reports
  uses: actions/upload-artifact@v3
  with:
    name: test-reports
    path: test_results/
```

```bash
# Jenkins example
#!/bin/bash
python main.py &
SERVER_PID=$!
sleep 5

python tools/run_test_scenarios.py --all --format html
EXIT_CODE=$?

kill $SERVER_PID
exit $EXIT_CODE
```

---

## Troubleshooting

**"API not accessible"**
- Ensure `main.py` is running
- Check API URL with `--api-url` flag
- Verify firewall/network settings

**"UART not available" (skipped)**
- Normal if ESP32 not connected
- Tests skip gracefully

**Tests timing out**
- Increase timeout in `_api_get()` and `_api_post()` methods
- Check system load

**Import errors**
- Install requirements: `pip install requests`
- Check Python version (3.7+)

---

## Future Enhancements

Potential additions to the test suite:

- Screenshot capture of web UI during tests
- Performance benchmarking (CPU/memory during tests)
- Email notifications on failure
- Slack/Teams webhook integration
- Test result database (SQLite) for historical tracking
- Parallel scenario execution
- Custom test configuration files (YAML/JSON)
