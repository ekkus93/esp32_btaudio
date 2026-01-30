#!/bin/bash
# CI Check: Enforce "No Legacy BT APIs in main.c"
# 
# This script ensures that main.c remains a clean bootstrap file and does NOT
# contain direct ESP-IDF Bluetooth API calls (except esp_bt_controller_mem_release).
# ALL Bluetooth initialization and callbacks MUST go through the bt_manager component.
#
# Usage: ./tools/ci_check_main_no_bt_apis.sh
# Exit 0: Pass (no forbidden APIs found)
# Exit 1: Fail (forbidden APIs detected)

set -e

MAIN_FILE="esp_bt_audio_source/main/main.c"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

if [[ ! -f "$MAIN_FILE" ]]; then
    echo "ERROR: $MAIN_FILE not found"
    exit 1
fi

# Forbidden BT API patterns (these should ONLY appear in bt_manager component)
FORBIDDEN_PATTERNS=(
    "esp_a2d_"
    "esp_avrc_"
    "esp_bt_gap_"
    "esp_bluedroid_init"
    "esp_bluedroid_enable"
    "esp_bluedroid_disable"
    "esp_bt_controller_init"
    "esp_bt_controller_enable"
    "esp_bt_controller_disable"
    "esp_bt_dev_set_device_name"
)

# Allowed exceptions (legitimate uses in main.c bootstrap)
ALLOWED_EXCEPTIONS=(
    "esp_bt_controller_mem_release"  # OK: free BLE memory in main.c
)

VIOLATIONS_FOUND=0

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "CI Check: No Legacy BT APIs in main.c"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "File: $MAIN_FILE"
echo ""

for pattern in "${FORBIDDEN_PATTERNS[@]}"; do
    # Search for the pattern, excluding comments
    matches=$(grep -n "$pattern" "$MAIN_FILE" | grep -v "//" | grep -v "/\*" || true)
    
    if [[ -n "$matches" ]]; then
        echo "❌ VIOLATION: Found forbidden BT API pattern: $pattern"
        echo "$matches"
        echo ""
        VIOLATIONS_FOUND=$((VIOLATIONS_FOUND + 1))
    fi
done

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ $VIOLATIONS_FOUND -gt 0 ]]; then
    echo "❌ FAIL: Found $VIOLATIONS_FOUND forbidden BT API pattern(s) in main.c"
    echo ""
    echo "Policy: main.c must ONLY contain:"
    echo "  - Bootstrap code (diagnostics, initialization orchestration)"
    echo "  - bt_manager_init() call (NOT raw BT API calls)"
    echo "  - cmd_init() call"
    echo "  - audio_processor setup"
    echo ""
    echo "ALL Bluetooth API calls must go through bt_manager component!"
    echo "Allowed exception: esp_bt_controller_mem_release (memory optimization)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    exit 1
else
    echo "✅ PASS: main.c contains no forbidden BT APIs"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    exit 0
fi
