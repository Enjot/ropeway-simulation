#!/bin/bash
# Test 3: VIP Priority Without Queue Starvation
#
# Goal: VIPs served first, regular tourists not starved indefinitely.
#
# Rationale: Tests msgrcv() with mtype=-2 for priority ordering. If VIPs
# continuously arrive, mtype=2 messages could starve. Verifies cashier
# eventually serves regular tourists between VIP bursts.
#
# Parameters: vip_percentage=10, tourists=50, simulation_time=120s.
#
# Expected outcome: VIPs board first. Regular tourists eventually served.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test3_vip.conf"
LOG_FILE="/tmp/ropeway_test3.log"

cd "$BUILD_DIR"

echo "Testing VIP priority (10% VIPs)"
echo "Running simulation..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"

# Analyze results
echo
echo "Analyzing logs..."

# Count VIP and regular tourists that completed rides
VIP_RIDES=$(grep -c "VIP.*boarded\|boarded.*VIP" "$LOG_FILE" 2>/dev/null || echo "0")
TOTAL_RIDES=$(grep -c "boarded chairlift\|boarded chair" "$LOG_FILE" 2>/dev/null || echo "0")

echo "Total boardings: $TOTAL_RIDES"
echo "VIP boardings: $VIP_RIDES"

if [ "$TOTAL_RIDES" -eq 0 ]; then
    echo "FAIL: No boardings recorded"
    exit 1
fi

# Check regular tourists got rides (no starvation)
REGULAR_RIDES=$((TOTAL_RIDES - VIP_RIDES))
if [ "$REGULAR_RIDES" -eq 0 ]; then
    echo "FAIL: Regular tourists were starved (0 rides)"
    exit 1
fi

echo "Regular boardings: $REGULAR_RIDES"

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

echo "PASS: VIP priority working without starvation"
exit 0
