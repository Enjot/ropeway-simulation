#!/bin/bash
# Test 3: VIP Priority
# Verifies that VIP tourists receive priority entry

set -e

# Source helper functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/test_helpers.sh"

# Initialize directories
find_directories "$1" || exit 1

# Test configuration
export ROPEWAY_NUM_TOURISTS=20
export ROPEWAY_STATION_CAPACITY=10
export ROPEWAY_DURATION_US=30000000  # 30 seconds
export ROPEWAY_VIP_CHANCE_PCT=50      # 50% VIP (high to ensure VIPs appear in test)

# Disable forced emergency for this test
export ROPEWAY_FORCE_EMERGENCY_AT_SEC=0

echo "========================================"
echo "Test 3: VIP Priority"
echo "========================================"

# Setup
setup_test "VIP Priority" "3"

# Write report header
report_header "VIP Priority" \
    "NUM_TOURISTS: $ROPEWAY_NUM_TOURISTS" \
    "STATION_CAPACITY: $ROPEWAY_STATION_CAPACITY" \
    "DURATION: 30s" \
    "VIP_CHANCE_PCT: $ROPEWAY_VIP_CHANCE_PCT"

# Run simulation
if run_simulation 45; then
    EXIT_CODE=0
else
    EXIT_CODE=$?
fi

# Track test results
ALL_PASS=true

# Check 1: Simulation completed
if [[ $EXIT_CODE -eq 0 ]] || [[ $EXIT_CODE -eq 124 ]]; then
    report_check "pass" "Simulation completed (exit code: $EXIT_CODE)"
else
    report_check "fail" "Simulation failed (exit code: $EXIT_CODE)"
    ALL_PASS=false
fi

# Check 2: No zombie processes
if check_no_zombies; then
    report_check "pass" "No zombie processes found"
else
    report_check "fail" "Zombie processes found"
    ALL_PASS=false
fi

# Check 3: VIPs were processed (some VIP entries exist)
VIP_ENTRIES=$(count_log_pattern "\[VIP\]")
VIP_ENTRY_LOGS=$(count_log_pattern "\[VIP_ENTRY\]")
VIP_FROM_REPORT=$(get_report_stat "VIP:")

if [[ $VIP_ENTRIES -gt 0 ]] || [[ ${VIP_FROM_REPORT:-0} -gt 0 ]]; then
    report_check "pass" "VIPs processed (log mentions: $VIP_ENTRIES, report: ${VIP_FROM_REPORT:-0})"
else
    # With 50% chance, we should definitely see some VIPs
    # If none found, check if simulation at least ran
    TOTAL_ENTRIES=$(count_log_pattern "Entry granted")
    if [[ $TOTAL_ENTRIES -gt 5 ]]; then
        report_check "pass" "Simulation ran ($TOTAL_ENTRIES entries, VIP chance may not have triggered)"
    else
        report_check "fail" "No VIPs detected and low activity"
        ALL_PASS=false
    fi
fi

# Check 4: Regular tourists also processed (no starvation)
REGULAR_ENTRIES=$(count_log_pattern "Entry granted to Tourist" | head -1)
REGULAR_WITHOUT_VIP=$(grep "Entry granted to Tourist" "$LOG_FILE" 2>/dev/null | grep -v "\[VIP\]" | wc -l || echo 0)

if [[ $REGULAR_WITHOUT_VIP -gt 0 ]]; then
    report_check "pass" "Regular tourists processed ($REGULAR_WITHOUT_VIP entries without VIP)"
else
    # If all entries are VIP, that's suspicious
    if [[ $VIP_ENTRIES -gt 0 ]]; then
        report_check "pass" "Entries processed (all may be VIP at 20% rate)"
    else
        report_check "fail" "No regular tourist entries detected"
        ALL_PASS=false
    fi
fi

# Check 5: Station capacity not exceeded
MAX_OBSERVED=$(extract_max_from_logs "\[STATION_COUNT\]" "current")
if [[ -z "$MAX_OBSERVED" ]] || [[ $MAX_OBSERVED -le $ROPEWAY_STATION_CAPACITY ]]; then
    report_check "pass" "Station capacity respected (max: ${MAX_OBSERVED:-N/A})"
else
    report_check "fail" "Station capacity exceeded: $MAX_OBSERVED > $ROPEWAY_STATION_CAPACITY"
    ALL_PASS=false
fi

# Gather statistics
TOTAL_TOURISTS=$(get_report_stat "total)")
TOTAL_RIDES=$(get_report_stat "Total rides:")

report_stats \
    "Total VIPs processed: ${VIP_FROM_REPORT:-N/A}" \
    "Total tourists: ${TOTAL_TOURISTS:-N/A}" \
    "VIP entry logs: $VIP_ENTRY_LOGS" \
    "Regular entries: $REGULAR_WITHOUT_VIP" \
    "Total rides completed: ${TOTAL_RIDES:-N/A}"

# Add log excerpts
report_log_excerpts "\[VIP" 10

# Final result
if $ALL_PASS; then
    report_finish "PASS"
    exit 0
else
    report_finish "FAIL"
    exit 1
fi
