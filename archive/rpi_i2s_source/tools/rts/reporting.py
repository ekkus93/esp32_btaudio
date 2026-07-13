"""HTML/Markdown report + summary output, as a mixin (uses self.*)."""
from datetime import datetime
from pathlib import Path

from .types import TestResult


class ReportingMixin:
    """Report-generation methods mixed into TestScenarioRunner."""

    def generate_html_report(self, output_path: Path = None) -> Path:
        """Generate HTML test report."""
        if output_path is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = self.output_dir / f"test_report_{timestamp}.html"
        
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == TestResult.PASS)
        failed = sum(1 for r in self.results if r.status == TestResult.FAIL)
        skipped = sum(1 for r in self.results if r.status == TestResult.SKIP)
        errors = sum(1 for r in self.results if r.status == TestResult.ERROR)
        
        total_duration = sum(r.duration for r in self.results)
        
        html = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>RPi I2S Source - Test Report</title>
    <style>
        body {{ font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 20px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 30px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
        h1 {{ color: #333; border-bottom: 3px solid #007bff; padding-bottom: 10px; }}
        h2 {{ color: #555; margin-top: 30px; }}
        .summary {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 15px; margin: 20px 0; }}
        .stat-card {{ background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 20px; border-radius: 8px; text-align: center; }}
        .stat-card.pass {{ background: linear-gradient(135deg, #84fab0 0%, #8fd3f4 100%); }}
        .stat-card.fail {{ background: linear-gradient(135deg, #fa709a 0%, #fee140 100%); }}
        .stat-card.skip {{ background: linear-gradient(135deg, #a8edea 0%, #fed6e3 100%); }}
        .stat-number {{ font-size: 36px; font-weight: bold; }}
        .stat-label {{ font-size: 14px; opacity: 0.9; margin-top: 5px; }}
        .scenario {{ margin: 20px 0; padding: 20px; border: 1px solid #ddd; border-radius: 8px; background: #fafafa; }}
        .scenario-header {{ display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px; }}
        .scenario-name {{ font-size: 18px; font-weight: bold; }}
        .status {{ padding: 5px 15px; border-radius: 20px; font-weight: bold; font-size: 12px; text-transform: uppercase; }}
        .status.PASS {{ background: #28a745; color: white; }}
        .status.FAIL {{ background: #dc3545; color: white; }}
        .status.SKIP {{ background: #6c757d; color: white; }}
        .status.ERROR {{ background: #ffc107; color: black; }}
        .steps {{ margin-top: 15px; }}
        .step {{ margin: 8px 0; padding: 10px; background: white; border-left: 4px solid #007bff; }}
        .step.PASS {{ border-left-color: #28a745; }}
        .step.FAIL {{ border-left-color: #dc3545; }}
        .step.SKIP {{ border-left-color: #6c757d; }}
        .meta {{ color: #666; font-size: 14px; margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>🧪 RPi I2S Source - Automated Test Report</h1>
        <p><strong>Generated:</strong> {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
        <p><strong>API URL:</strong> {self.api_url}</p>
        
        <h2>Summary</h2>
        <div class="summary">
            <div class="stat-card">
                <div class="stat-number">{total}</div>
                <div class="stat-label">Total Scenarios</div>
            </div>
            <div class="stat-card pass">
                <div class="stat-number">{passed}</div>
                <div class="stat-label">Passed</div>
            </div>
            <div class="stat-card fail">
                <div class="stat-number">{failed}</div>
                <div class="stat-label">Failed</div>
            </div>
            <div class="stat-card skip">
                <div class="stat-number">{skipped}</div>
                <div class="stat-label">Skipped</div>
            </div>
        </div>
        
        <p><strong>Total Duration:</strong> {total_duration:.2f} seconds</p>
        
        <h2>Test Scenarios</h2>
"""
        
        for scenario in self.results:
            status_class = scenario.status.value
            steps_html = ""
            for step in scenario.steps:
                step_status = step['status']
                step_details = f" - {step['details']}" if step['details'] else ""
                steps_html += f'<div class="step {step_status}">{step["name"]}{step_details}</div>\n'
            
            error_html = ""
            if scenario.error_message:
                error_html = f'<p style="color: #dc3545; margin-top: 10px;"><strong>Error:</strong> {scenario.error_message}</p>'
            
            html += f"""
        <div class="scenario">
            <div class="scenario-header">
                <div>
                    <div class="scenario-name">{scenario.name}</div>
                    <div style="color: #666; font-size: 14px;">{scenario.description}</div>
                </div>
                <span class="status {status_class}">{status_class}</span>
            </div>
            <p><strong>Duration:</strong> {scenario.duration:.2f}s</p>
            {error_html}
            <div class="steps">
                <strong>Steps:</strong>
                {steps_html}
            </div>
        </div>
"""
        
        html += f"""
        <div class="meta">
            <p><strong>System Information:</strong></p>
            <ul>
                <li>Start Time: {self.start_time.strftime("%Y-%m-%d %H:%M:%S") if self.start_time else "N/A"}</li>
                <li>End Time: {self.end_time.strftime("%Y-%m-%d %H:%M:%S") if self.end_time else "N/A"}</li>
                <li>Report Path: {output_path}</li>
            </ul>
        </div>
    </div>
</body>
</html>
"""
        
        output_path.write_text(html, encoding='utf-8')
        return output_path
    
    def generate_markdown_report(self, output_path: Path = None) -> Path:
        """Generate Markdown test report."""
        if output_path is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            output_path = self.output_dir / f"test_report_{timestamp}.md"
        
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == TestResult.PASS)
        failed = sum(1 for r in self.results if r.status == TestResult.FAIL)
        skipped = sum(1 for r in self.results if r.status == TestResult.SKIP)
        total_duration = sum(r.duration for r in self.results)
        
        md = f"""# RPi I2S Source - Automated Test Report

**Generated:** {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}  
**API URL:** {self.api_url}

## Summary

| Metric | Value |
|--------|-------|
| Total Scenarios | {total} |
| Passed | {passed} ✅ |
| Failed | {failed} ❌ |
| Skipped | {skipped} ⊘ |
| Total Duration | {total_duration:.2f}s |

## Test Scenarios

"""
        
        for scenario in self.results:
            status_emoji = {
                TestResult.PASS: "✅",
                TestResult.FAIL: "❌",
                TestResult.SKIP: "⊘",
                TestResult.ERROR: "⚠️"
            }[scenario.status]
            
            md += f"""### {status_emoji} {scenario.name}

**Description:** {scenario.description}  
**Status:** {scenario.status.value}  
**Duration:** {scenario.duration:.2f}s

**Steps:**
"""
            
            for step in scenario.steps:
                step_emoji = {
                    'PASS': '✓',
                    'FAIL': '✗',
                    'SKIP': '⊘',
                    'ERROR': '⚠'
                }[step['status']]
                details = f" - {step['details']}" if step['details'] else ""
                md += f"- {step_emoji} {step['name']}{details}\n"
            
            if scenario.error_message:
                md += f"\n**Error:** {scenario.error_message}\n"
            
            md += "\n"
        
        md += f"""---

**Report generated:** {datetime.now().isoformat()}  
**Output path:** `{output_path}`
"""
        
        output_path.write_text(md, encoding='utf-8')
        return output_path
    
    def print_summary(self):
        """Print test summary to console."""
        total = len(self.results)
        passed = sum(1 for r in self.results if r.status == TestResult.PASS)
        failed = sum(1 for r in self.results if r.status == TestResult.FAIL)
        skipped = sum(1 for r in self.results if r.status == TestResult.SKIP)
        total_duration = sum(r.duration for r in self.results)
        
        print(f"\n{'='*60}")
        print("TEST SUMMARY")
        print(f"{'='*60}")
        print(f"Total scenarios: {total}")
        print(f"  ✅ Passed:  {passed}")
        print(f"  ❌ Failed:  {failed}")
        print(f"  ⊘ Skipped:  {skipped}")
        print(f"Total duration: {total_duration:.2f}s")
        print(f"{'='*60}\n")
