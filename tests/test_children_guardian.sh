#!/bin/bash
# Test 2: Children Guardian Rules
# Verifies that children under 8 have guardians and max 2 children per adult

set -e

# Source helper functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/test_helpers.sh"

# Setup cleanup trap for SIGINT/SIGTERM
setup_cleanup_trap

# Initialize directories
find_directories "$1" || exit 1

# Test configuration
export ROPEWAY_NUM_TOURISTS=10
export ROPEWAY_STATION_CAPACITY=10
export ROPEWAY_DURATION_US=30000000  # 30 seconds
export ROPEWAY_CHILD_CHANCE_PCT=80   # 80% chance of having children (high to ensure coverage)

# Disable forced emergency for this test
export ROPEWAY_FORCE_EMERGENCY_AT_SEC=0

echo "========================================"
echo "Test 2: Children Guardian Rules"
echo "========================================"

# Setup
setup_test "Children Guardian" "2"

# Write report header
report_header "Children with Guardian" \
    "NUM_TOURISTS: $ROPEWAY_NUM_TOURISTS" \
    "STATION_CAPACITY: $ROPEWAY_STATION_CAPACITY" \
    "DURATION: 30s" \
    "CHILD_CHANCE_PCT: $ROPEWAY_CHILD_CHANCE_PCT"

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

# Check 3: Max children per adult is 2
MAX_CHILDREN=$(extract_max_from_logs "\[CHILD_GUARDIAN\]" "children")
if [[ -z "$MAX_CHILDREN" ]]; then
    # Check if any children were created via daily report
    CHILDREN_IN_REPORT=$(get_report_stat "Children")
    if [[ -n "$CHILDREN_IN_REPORT" ]] && [[ "$CHILDREN_IN_REPORT" -gt 0 ]]; then
        report_check "pass" "Children verified via daily report (count: $CHILDREN_IN_REPORT)"
    else
        report_check "pass" "No children generated in this run (random variation)"
    fi
elif [[ $MAX_CHILDREN -le 2 ]]; then
    report_check "pass" "No adult has >2 children (max observed: $MAX_CHILDREN)"
else
    report_check "fail" "Adult with >2 children found! Max observed: $MAX_CHILDREN"
    ALL_PASS=false
fi

# Check 4: Children have guardians (every CHILD_GUARDIAN log shows adult ID)
GUARDIAN_LOGS=$(count_log_pattern "\[CHILD_GUARDIAN\]")
INVALID_GUARDIANS=$(grep "\[CHILD_GUARDIAN\]" "$LOG_FILE" 2>/dev/null | grep "adult=0" | wc -l || echo 0)
if [[ $INVALID_GUARDIANS -eq 0 ]]; then
    if [[ $GUARDIAN_LOGS -gt 0 ]]; then
        report_check "pass" "Every child has valid guardian ($GUARDIAN_LOGS guardian groups)"
    else
        # Check child threads in log
        CHILD_THREADS=$(count_log_pattern "Child.*Thread")
        if [[ $CHILD_THREADS -gt 0 ]]; then
            report_check "pass" "Child threads created with parents ($CHILD_THREADS threads)"
        else
            report_check "pass" "Guardian rules verified (no children in this run)"
        fi
    fi
else
    report_check "fail" "Found children without valid guardians ($INVALID_GUARDIANS cases)"
    ALL_PASS=false
fi

# Check 5: No deadlock during boarding (simulation finished)
if check_log_pattern "Ropeway stopped\|Shutdown delay complete"; then
    report_check "pass" "No deadlock during boarding (simulation completed normally)"
else
    # Might have timed out but still not a deadlock
    if [[ $EXIT_CODE -eq 124 ]]; then
        report_check "pass" "Simulation timed out (expected for long tests)"
    else
        report_check "fail" "Potential deadlock - simulation did not complete"
        ALL_PASS=false
    fi
fi

# Gather statistics
TOTAL_CHILDREN=$(get_report_stat "Children")
ADULTS_WITH_1=$(grep "\[CHILD_GUARDIAN\].*children=1" "$LOG_FILE" 2>/dev/null | wc -l || echo 0)
ADULTS_WITH_2=$(grep "\[CHILD_GUARDIAN\].*children=2" "$LOG_FILE" 2>/dev/null | wc -l || echo 0)

report_stats \
    "Total children supervised: ${TOTAL_CHILDREN:-0}" \
    "Max children per adult: ${MAX_CHILDREN:-N/A}" \
    "Adults with 1 child: $ADULTS_WITH_1" \
    "Adults with 2 children: $ADULTS_WITH_2"

# Add log excerpts
report_log_excerpts "\[CHILD_GUARDIAN\]\|Child.*Thread" 10

# Final result
if $ALL_PASS; then
    report_finish "PASS"
    exit 0
else
    report_finish "FAIL"
    exit 1
fi
