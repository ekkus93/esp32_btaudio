"""Result types shared across the scenario runner."""
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Any, Dict, List, Optional


class TestResult(Enum):
    """Test result status."""
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    ERROR = "ERROR"


@dataclass
class ScenarioResult:
    """Result of a test scenario."""
    name: str
    description: str
    status: TestResult
    duration: float
    steps: List[Dict[str, Any]] = field(default_factory=list)
    error_message: Optional[str] = None
    
    def add_step(self, step_name: str, status: TestResult, details: str = ""):
        """Add a test step result."""
        self.steps.append({
            'name': step_name,
            'status': status.value,
            'details': details,
            'timestamp': datetime.now().isoformat()
        })
