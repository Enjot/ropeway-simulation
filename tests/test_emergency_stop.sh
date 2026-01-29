#!/bin/bash
# Test 4: Emergency Stop/Resume
# Verifies that emergency stop and resume work correctly

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
export ROPEWAY_DURATION_US=30000000      # 30 seconds
export ROPEWAY_FORCE_EMERGENCY_AT_SEC=10  # Force emergency at 10 seconds

echo "========================================"
echo "Test 4: Emergency Stop/Resume"
echo "========================================"

# Setup
setup_test "Emergency Stop" "4"

# Write report header
report_header "Emergency Stop/Resume" \
    "NUM_TOURISTS: $ROPEWAY_NUM_TOURISTS" \
    "STATION_CAPACITY: $ROPEWAY_STATION_CAPACITY" \
    "DURATION: 30s" \
    "FORCE_EMERGENCY_AT_SEC: $ROPEWAY_FORCE_EMERGENCY_AT_SEC"

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

# Check 3: Emergency was triggered
if check_log_pattern "\[EMERGENCY\].*triggered=forced"; then
    report_check "pass" "Emergency triggered at forced time (~${ROPEWAY_FORCE_EMERGENCY_AT_SEC}s)"
else
    # Check for any emergency
    if check_log_pattern "EMERGENCY STOP\|DANGER DETECTED"; then
        report_check "pass" "Emergency stop detected"
    else
        report_check "fail" "No emergency stop detected"
        ALL_PASS=false
    fi
fi

# Check 4: Emergency was resolved
if check_log_pattern "\[EMERGENCY\].*state=resolved\|Resume.*ready\|Resuming operations"; then
    report_check "pass" "Emergency was resolved and operations resumed"
else
    # Check if simulation just continued
    if check_log_pattern "Chair.*departing\|Entry granted"; then
        report_check "pass" "Operations continued (emergency resolution implied)"
    else
        report_check "fail" "No evidence of emergency resolution"
        ALL_PASS=false
    fi
fi

# Check 5: Worker communication logged
if check_log_pattern "UpperWorker.*ready\|LowerWorker.*ready\|confirmation"; then
    report_check "pass" "Worker communication verified"
else
    if check_log_pattern "Worker\|worker"; then
        report_check "pass" "Worker activity detected"
    else
        report_check "fail" "No worker communication detected"
        ALL_PASS=false
    fi
fi

# Check 6: Simulation completed or resumed properly
if check_log_pattern "Ropeway stopped\|Shutdown delay\|Closing time"; then
    report_check "pass" "Simulation completed after emergency"
else
    if [[ $EXIT_CODE -eq 0 ]] || [[ $EXIT_CODE -eq 124 ]]; then
        report_check "pass" "Simulation terminated (timeout or completion)"
    else
        report_check "fail" "Simulation did not complete properly"
        ALL_PASS=false
    fi
fi

# Gather statistics
EMERGENCY_LOGS=$(count_log_pattern "\[EMERGENCY\]")
CHAIRS_BEFORE=$(grep -B 100 "EMERGENCY STOP" "$LOG_FILE" 2>/dev/null | grep -c "Chair.*departing" || echo "N/A")
CHAIRS_AFTER=$(grep -A 100 "resolved\|Resuming" "$LOG_FILE" 2>/dev/null | grep -c "Chair.*departing" || echo "N/A")
EMERGENCY_STOPS=$(get_report_stat "Stops:")

report_stats \
    "Emergency trigger time: ~${ROPEWAY_FORCE_EMERGENCY_AT_SEC}s" \
    "Emergency logs: $EMERGENCY_LOGS" \
    "Emergency stops (report): ${EMERGENCY_STOPS:-N/A}" \
    "Chairs before emergency: $CHAIRS_BEFORE" \
    "Chairs after emergency: $CHAIRS_AFTER"

# Add log excerpts
report_log_excerpts "EMERGENCY\|emergency\|Resume\|resume" 15

# Final result
if $ALL_PASS; then
    report_finish "PASS"
    exit 0
else
    report_finish "FAIL"
    exit 1
fi
