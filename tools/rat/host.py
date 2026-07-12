"""Host-side test runners: CTest bundle, standalone build, cmake Unity, coverage."""
from __future__ import annotations

import os
import re
from pathlib import Path

from .proc import run_cmd
from .report import _unity_counts_from_output
from .common import TMP_DIR


def generate_coverage_report(build_dir: Path, output_dir: Path) -> dict:
    """
    Generate code coverage report using lcov/genhtml.
    
    Args:
        build_dir: Build directory containing .gcda files
        output_dir: Directory to store coverage reports
        
    Returns:
        dict with success status, report paths, and summary
    """
    result = {
        "success": False,
        "lcov_available": False,
        "genhtml_available": False,
        "coverage_info": None,
        "html_report": None,
        "summary": None,
        "error": None,
    }
    
    # Check if lcov is available
    rc_lcov, _ = run_cmd(["which", "lcov"])
    result["lcov_available"] = (rc_lcov == 0)
    
    rc_genhtml, _ = run_cmd(["which", "genhtml"])
    result["genhtml_available"] = (rc_genhtml == 0)
    
    if not result["lcov_available"]:
        result["error"] = "lcov not found - install with: sudo apt-get install lcov"
        return result
    
    try:
        # Capture coverage data
        coverage_info = output_dir / "coverage.info"
        coverage_filtered = output_dir / "coverage_filtered.info"
        html_dir = output_dir / "coverage_html"
        
        print(f"  Capturing coverage data from {build_dir}...")
        rc, out = run_cmd([
            "lcov",
            "--capture",
            "--directory", str(build_dir),
            "--output-file", str(coverage_info),
            "--quiet"
        ])
        
        if rc != 0:
            result["error"] = f"lcov capture failed (rc={rc})"
            return result
        
        # Filter out system files, build artifacts, and mocks
        print(f"  Filtering coverage data...")
        rc, out = run_cmd([
            "lcov",
            "--remove", str(coverage_info),
            "/usr/*",
            "*/build/*",
            "*/mocks/*",
            "*/test_*",
            "*/_deps/*",
            "--output-file", str(coverage_filtered),
            "--quiet"
        ])
        
        if rc != 0:
            result["error"] = f"lcov filter failed (rc={rc})"
            return result
        
        result["coverage_info"] = str(coverage_filtered)
        
        # Extract summary statistics
        rc, summary_out = run_cmd([
            "lcov",
            "--summary", str(coverage_filtered)
        ])
        
        # Parse summary for lines coverage percentage
        import re
        match = re.search(r"lines\.*:\s*([\d.]+)%", summary_out)
        if match:
            result["summary"] = f"Line coverage: {match.group(1)}%"
        
        # Generate HTML report if genhtml is available
        if result["genhtml_available"]:
            print(f"  Generating HTML report...")
            rc, out = run_cmd([
                "genhtml",
                str(coverage_filtered),
                "--output-directory", str(html_dir),
                "--quiet"
            ])
            
            if rc == 0:
                result["html_report"] = str(html_dir / "index.html")
            else:
                result["error"] = f"genhtml failed (rc={rc})"
        
        result["success"] = True
        
    except Exception as e:
        result["error"] = f"Exception during coverage generation: {str(e)}"
    
    return result


def run_host_tests(root: Path, build_dir_name: str = "build_host_tests", jobs: int = 0, valgrind: bool = False, coverage: bool = False, asan: bool = False) -> dict:
    host_dir = root / "esp_bt_audio_source" / "test" / "host_test"
    build_dir = host_dir / build_dir_name
    build_dir.mkdir(parents=True, exist_ok=True)
    summary = {"host": {"configured": False, "build": False, "ctest_rc": None, "ctest_output": None, "lasttest_path": None, "valgrind_enabled": valgrind, "coverage_enabled": coverage, "asan_enabled": asan}}

    # configure
    cmake_cmd = ["cmake", ".."]
    if coverage:
        cmake_cmd.insert(1, "-DENABLE_COVERAGE=ON")
    if asan:
        cmake_cmd.insert(1, "-DENABLE_ASAN=ON")
    rc, out = run_cmd(cmake_cmd, cwd=str(build_dir))
    summary["host"]["configured"] = (rc == 0)
    summary["host"]["configure_output"] = out

    # build
    build_cmd = ["cmake", "--build", "."]
    if jobs and jobs > 1:
        build_cmd += ["--", f"-j{jobs}"]
    rc, out = run_cmd(build_cmd, cwd=str(build_dir))
    summary["host"]["build"] = (rc == 0)
    summary["host"]["build_output"] = out

    # run ctest
    rc, out = run_cmd(["ctest", "--output-on-failure"], cwd=str(build_dir))
    summary["host"]["ctest_rc"] = rc
    summary["host"]["ctest_output"] = out

    # try to locate LastTest.log
    lasttest = build_dir / "Testing" / "Temporary" / "LastTest.log"
    if lasttest.exists():
        summary["host"]["lasttest_path"] = str(lasttest)
    else:
        # sometimes builds put host tests in other directories; attempt common alternates
        alternates = [root / "esp_bt_audio_source" / "test" / "host_test" / "build-host",
                      root / "test" / "host_test" / "build_host_tests"]
        for alt in alternates:
            p = alt / "Testing" / "Temporary" / "LastTest.log"
            if p.exists():
                summary["host"]["lasttest_path"] = str(p)
                break

    # persist ctest output
    outpath = TMP_DIR / "host_ctest_output.log"
    outpath.write_text(out)
    summary["host"]["ctest_log"] = str(outpath)
    
    # After ctest completes, run each host test binary directly to capture Unity case counts
    # (ctest only reports test targets, not per-Unity test cases).
    # Look for executable files named test_* in the build directory.
    per_binary = {}
    total_cases = 0
    total_failures = 0
    total_ignored = 0
    zero_test_binaries = []
    valgrind_errors = {}
    try:
        for entry in build_dir.iterdir():
            if not entry.is_file():
                continue
            if not entry.name.startswith("test_"):
                continue
            if not os.access(entry, os.X_OK):
                continue
            
            # Build command with optional Valgrind wrapper
            if valgrind:
                cmd = [
                    "valgrind",
                    "--leak-check=full",
                    "--error-exitcode=1",
                    "--track-origins=yes",
                    "--errors-for-leak-kinds=definite,possible",
                    str(entry)
                ]
                print(f"  Running {entry.name} under Valgrind...")
            else:
                cmd = [str(entry)]
            
            rc_bin, out_bin = run_cmd(cmd, cwd=str(build_dir))
            counts = _unity_counts_from_output(out_bin)
            
            # Track Valgrind-specific failures
            valgrind_failed = False
            if valgrind and rc_bin == 1:
                # Exit code 1 from Valgrind means memory errors detected
                if "ERROR SUMMARY:" in out_bin or "LEAK SUMMARY:" in out_bin:
                    valgrind_failed = True
                    valgrind_errors[entry.name] = out_bin
            
            per_binary[entry.name] = {
                "rc": rc_bin,
                "stdout": out_bin,
                "tests": counts.get("tests", 0),
                "failures": counts.get("failures", 0),
                "ignored": counts.get("ignored", 0),
                "valgrind_failed": valgrind_failed,
            }
            if counts.get("tests", 0) == 0:
                zero_test_binaries.append(entry.name)
            total_cases += counts.get("tests", 0)
            total_failures += counts.get("failures", 0)
            total_ignored += counts.get("ignored", 0)
    except Exception as exc:
        per_binary["_count_error"] = str(exc)

    summary["host"]["case_counts"] = {
        "total": total_cases,
        "failures": total_failures,
        "ignored": total_ignored,
        "per_binary": per_binary,
        "zero_test_binaries": zero_test_binaries,
        "valgrind_errors": valgrind_errors,
    }
    
    # Print Valgrind summary if enabled
    if valgrind and valgrind_errors:
        print(f"\n⚠️  Valgrind detected memory errors in {len(valgrind_errors)} test(s):")
        for binary_name in valgrind_errors.keys():
            print(f"  - {binary_name}")
    
    # Generate coverage report if enabled
    if coverage:
        print("\n📊 Generating code coverage report...")
        coverage_result = generate_coverage_report(build_dir, TMP_DIR)
        summary["host"]["coverage"] = coverage_result
        if coverage_result.get("success"):
            print(f"  ✅ Coverage report generated: {coverage_result.get('html_report')}")
            if coverage_result.get("summary"):
                print(f"  📈 {coverage_result.get('summary')}")
        else:
            print(f"  ⚠️  Coverage generation had issues: {coverage_result.get('error', 'unknown error')}")
    
    return summary


def run_standalone_host_tests(root: Path, jobs: int = 0) -> dict:
    """Run standalone host tests (matches CI build exactly).
    
    This performs a clean build from scratch in test/host_test/build_host_tests,
    exactly matching the GitHub Actions CI workflow. This catches linking errors
    that incremental builds might miss.
    
    Returns a summary dict with build and test results.
    """
    host_dir = root / "esp_bt_audio_source" / "test" / "host_test"
    build_dir = host_dir / "build_host_tests"
    
    summary = {
        "configured": False,
        "build": False,
        "ctest_rc": None,
        "ctest_output": None,
        "total_tests": 0,
        "failures": 0,
    }
    
    print(f"  Standalone build dir: {build_dir}")
    
    # Clean build (like CI)
    if build_dir.exists():
        import shutil
        print(f"  Removing existing build dir...")
        shutil.rmtree(build_dir)
    
    build_dir.mkdir(parents=True, exist_ok=True)
    
    # Configure
    print(f"  Running cmake...")
    rc, out = run_cmd(["cmake", ".."], cwd=str(build_dir))
    summary["configured"] = (rc == 0)
    summary["configure_output"] = out
    
    if rc != 0:
        print(f"  ❌ Configure FAILED")
        return summary
    
    # Build
    print(f"  Building with cmake --build...")
    build_cmd = ["cmake", "--build", "."]
    if jobs and jobs > 1:
        build_cmd += ["--", f"-j{jobs}"]
    rc, out = run_cmd(build_cmd, cwd=str(build_dir))
    summary["build"] = (rc == 0)
    summary["build_output"] = out
    
    if rc != 0:
        print(f"  ❌ Build FAILED")
        return summary
    
    # Run ctest
    print(f"  Running ctest...")
    rc, out = run_cmd(["ctest", "--output-on-failure"], cwd=str(build_dir))
    summary["ctest_rc"] = rc
    summary["ctest_output"] = out
    
    # Parse test counts from ctest output
    # Look for pattern like: "100% tests passed, 0 tests failed out of 36"
    match = re.search(r"(\d+)%\s+tests\s+passed,\s+(\d+)\s+tests\s+failed\s+out\s+of\s+(\d+)", out)
    if match:
        summary["failures"] = int(match.group(2))
        summary["total_tests"] = int(match.group(3))
    
    if rc == 0:
        print(f"  ✅ Standalone tests PASSED ({summary['total_tests']} tests)")
    else:
        print(f"  ❌ Standalone tests FAILED ({summary['failures']}/{summary['total_tests']} failures)")
    
    return summary


def run_cmake_unity_suite(root: Path, suite_rel_path: str, target_name: str, jobs: int = 0) -> dict:
    suite_dir = root / suite_rel_path
    build_dir = suite_dir / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    summary = {
        "configured": False,
        "build": False,
        "ctest_rc": None,
        "ctest_output": None,
        "binary_rc": None,
        "binary_output": None,
        "tests": 0,
        "failures": 0,
        "ignored": 0,
    }

    # configure
    rc, out = run_cmd(["cmake", "-S", str(suite_dir), "-B", str(build_dir)])
    summary["configured"] = (rc == 0)
    summary["configure_output"] = out

    # build
    build_cmd = ["cmake", "--build", str(build_dir)]
    if jobs and jobs > 1:
        build_cmd += ["--", f"-j{jobs}"]
    rc, out = run_cmd(build_cmd)
    summary["build"] = (rc == 0)
    summary["build_output"] = out

    # run ctest
    rc, out = run_cmd(["ctest", "--output-on-failure"], cwd=str(build_dir))
    summary["ctest_rc"] = rc
    summary["ctest_output"] = out

    # persist ctest output for debugging
    outpath = TMP_DIR / f"{target_name}_ctest_output.log"
    outpath.write_text(out)
    summary["ctest_log"] = str(outpath)

    # run the Unity binary directly to get case counts if present
    try:
        binary = build_dir / target_name
        if binary.exists() and os.access(binary, os.X_OK):
            rc_bin, out_bin = run_cmd([str(binary)])
            summary["binary_rc"] = rc_bin
            summary["binary_output"] = out_bin
            counts = _unity_counts_from_output(out_bin)
            summary.update(counts)
        else:
            counts = _unity_counts_from_output(out)
            summary.update(counts)
    except Exception:
        counts = _unity_counts_from_output(out)
        summary.update(counts)

    return summary
