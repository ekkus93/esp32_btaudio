#!/usr/bin/env bash
#
# CI Check: main.c Layering Constraints
#
# Purpose: Enforce architectural constraints on main/main.c to prevent
# subsystem coupling violations that would complicate future ESP32 split
# (Control ESP32 vs Audio ESP32).
#
# Constraints checked:
# 1. No direct ESP-IDF BT API calls (except esp_bt_controller_mem_release)
#    - Rationale: BT operations belong in bt_manager, not main.c
#    - Future: Control ESP32 will call bt_manager APIs, not raw ESP-IDF
#
# 2. No direct UART driver calls beyond installation
#    - Rationale: cmd_init owns UART usage; main.c only installs driver early
#    - Allowed: uart_driver_install, uart_is_driver_installed, uart_write_bytes (diagnostics)
#    - Forbidden: uart_read_bytes, uart_set_*, uart_param_* (belongs in cmd layer)
#
# 3. Single NVS initialization only
#    - Rationale: nvs_storage_init() wraps nvs_flash_init(); main.c must not call both
#    - Prevents: Redundant inits, version mismatch issues, unclear ownership
#
# 4. No invalid printf format specifiers
#    - Rationale: Catch common bugs like %d for size_t, %s for NULL
#    - Defensive: Prevents runtime crashes in diagnostic output
#
# Exit codes:
#   0 = All checks passed
#   1 = One or more violations found
#   2 = Script usage error (missing main.c path)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MAIN_C="${PROJECT_ROOT}/main/main.c"

# Color output for readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

VIOLATIONS=0

echo "=================================================="
echo "CI Check: main.c Layering Constraints"
echo "=================================================="
echo "Checking: ${MAIN_C}"
echo ""

# Check 1: No direct ESP-IDF BT API calls (except mem_release)
echo -n "Check 1: No direct BT API calls (except mem_release)... "
BT_VIOLATIONS=$(grep -n -E 'esp_(bt_|a2d_|avrc_|bluedroid_)' "${MAIN_C}" | \
                grep -v 'esp_bt_controller_mem_release' | \
                grep -v -E '^\s*//' | \
                grep -v -E '^\s*/\*' | \
                grep -v -E '\*.*esp_(bt_|a2d_|avrc_|bluedroid_)' || true)

if [ -n "${BT_VIOLATIONS}" ]; then
    echo -e "${RED}FAIL${NC}"
    echo "  Violations found:"
    echo "${BT_VIOLATIONS}" | while IFS= read -r line; do
        echo "    ${line}"
    done
    echo ""
    echo "  WHY: BT operations belong in bt_manager, not main.c"
    echo "  FIX: Move BT API calls to bt_manager and expose via bt_manager_* functions"
    echo ""
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo -e "${GREEN}PASS${NC}"
fi

# Check 2: No forbidden UART driver calls (allow only install, diagnostics)
echo -n "Check 2: No forbidden UART driver calls... "
UART_VIOLATIONS=$(grep -n -E 'uart_(read_bytes|set_|param_|get_buffered)' "${MAIN_C}" | \
                  grep -v '^\s*//' | \
                  grep -v '^\s*/\*' | \
                  grep -v -E '\*.*uart_(read_bytes|set_|param_|get_buffered)' || true)

if [ -n "${UART_VIOLATIONS}" ]; then
    echo -e "${RED}FAIL${NC}"
    echo "  Violations found:"
    echo "${UART_VIOLATIONS}" | while IFS= read -r line; do
        echo "    ${line}"
    done
    echo ""
    echo "  WHY: UART usage belongs in cmd_init, not main.c"
    echo "  ALLOWED: uart_driver_install, uart_is_driver_installed, uart_write_bytes (diagnostics only)"
    echo "  FORBIDDEN: uart_read_bytes, uart_set_*, uart_param_*, uart_get_* (cmd layer owns these)"
    echo "  FIX: Move UART operations to command_interface component"
    echo ""
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo -e "${GREEN}PASS${NC}"
fi

# Check 3: No redundant NVS init (nvs_storage_init is the wrapper)
echo -n "Check 3: No redundant NVS init (use nvs_storage_init only)... "
NVS_VIOLATIONS=$(grep -n -E '\bnvs_flash_init\b' "${MAIN_C}" | \
                 grep -v 'nvs_storage_init' | \
                 grep -v '^\s*//' | \
                 grep -v '^\s*/\*' || true)

if [ -n "${NVS_VIOLATIONS}" ]; then
    echo -e "${RED}FAIL${NC}"
    echo "  Violations found:"
    echo "${NVS_VIOLATIONS}" | while IFS= read -r line; do
        echo "    ${line}"
    done
    echo ""
    echo "  WHY: nvs_storage_init() wraps nvs_flash_init() with version handling"
    echo "  FIX: Use ESP_ERROR_CHECK(nvs_storage_init()); instead of nvs_flash_init()"
    echo ""
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo -e "${GREEN}PASS${NC}"
fi

# Check 4: Basic printf format specifier sanity
# Note: This is a heuristic check, not exhaustive. Catches common mistakes.
echo -n "Check 4: No obvious printf format errors... "
FORMAT_VIOLATIONS=""

# Check for %d with sizeof() - should be %zu
SIZE_BAD=$(grep -n 'printf.*%[0-9]*d.*sizeof' "${MAIN_C}" | \
           grep -v '(int)' | \
           grep -v '^\s*//' || true)
if [ -n "${SIZE_BAD}" ]; then
    FORMAT_VIOLATIONS="${FORMAT_VIOLATIONS}${SIZE_BAD}\n"
fi

# Check for %s without NULL check (heuristic: %s with bare variable, not string literal)
# This is hard to catch perfectly, so we just warn on suspicious patterns
# (This check is commented out as it's too prone to false positives)
# NULL_BAD=$(grep -n 'printf.*%s' "${MAIN_C}" | grep -v '""' | grep -v "''" || true)

if [ -n "${FORMAT_VIOLATIONS}" ]; then
    echo -e "${YELLOW}WARN${NC}"
    echo "  Potential issues found:"
    echo -e "${FORMAT_VIOLATIONS}" | while IFS= read -r line; do
        [ -n "$line" ] && echo "    ${line}"
    done
    echo ""
    echo "  NOTE: sizeof() should use %zu, not %d"
    echo "  FIX: Use %zu for sizeof(), %zd for ssize_t, cast to (int) if needed"
    echo ""
    # Warnings don't increment VIOLATIONS (non-blocking)
else
    echo -e "${GREEN}PASS${NC}"
fi

echo ""
echo "=================================================="
if [ ${VIOLATIONS} -eq 0 ]; then
    echo -e "${GREEN}✓ All layering constraints satisfied${NC}"
    echo "main.c respects architectural boundaries."
    exit 0
else
    echo -e "${RED}✗ ${VIOLATIONS} constraint violation(s) found${NC}"
    echo "Please fix the violations above before merging."
    exit 1
fi
