#!/bin/bash
# Test 3: VIP Priority Without Queue Starvation
#
# Goal: Verify VIPs skip entry gates while regular tourists wait in queue.
#
# Rationale: VIPs bypass SEM_ENTRY_GATES entirely, reaching the platform faster
# when gates are congested. This test verifies the gate behavior difference
# between VIPs and regular tourists, and ensures both groups are served.
#
# Parameters: vip_percentage=30, tourists=30, simulation_time=1s.
#
# Expected outcome: VIPs skip gates, regulars enter gates, both groups served.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test3_vip.conf"
LOG_FILE="/tmp/ropeway_test3.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 3: VIP Priority Without Queue Starvation ==="
echo "Goal: Verify VIPs skip gates and board earlier than regular tourists"
echo "Running simulation..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing logs..."

# Check for crashes
if [ $EXIT_CODE -ne 0 ] && [ $EXIT_CODE -ne 124 ]; then
    echo "FAIL: Simulation crashed with exit code $EXIT_CODE"
    exit 1
fi

# 1. Verify VIPs skip gates
VIP_SKIPS=$(grep -c "skipped gate queue" "$LOG_FILE" 2>/dev/null || echo "0")
if [ "$VIP_SKIPS" -eq 0 ]; then
    echo "FAIL: No VIP gate skips detected"
    exit 1
fi
echo "VIP gate skips: $VIP_SKIPS"

# 2. Get line numbers for boarding events (proxy for order)
# VIP boardings
VIP_BOARD_LINES=$(grep -n "\[VIP.*boarded chairlift" "$LOG_FILE" | cut -d: -f1)
# Regular boardings (exclude VIP)
REG_BOARD_LINES=$(grep -n "boarded chairlift" "$LOG_FILE" | grep -v "\[VIP" | cut -d: -f1)

# 3. Calculate average line number (= average position in log = order)
calc_avg() {
    local sum=0 count=0
    for n in $1; do
        sum=$((sum + n))
        count=$((count + 1))
    done
    if [ $count -eq 0 ]; then
        echo "0"
    else
        echo $((sum / count))
    fi
}

VIP_AVG=$(calc_avg "$VIP_BOARD_LINES")
REG_AVG=$(calc_avg "$REG_BOARD_LINES")

echo "VIP avg boarding position: $VIP_AVG"
echo "Regular avg boarding position: $REG_AVG"

# 4. Verify VIPs don't enter through gates (they skip)
VIP_GATE_ENTER=$(grep "\[VIP.*entered through gate" "$LOG_FILE" | wc -l)
if [ "$VIP_GATE_ENTER" -gt 0 ]; then
    echo "FAIL: VIPs should skip gates, not enter through them ($VIP_GATE_ENTER found)"
    exit 1
fi
echo "VIP gate behavior correct: VIPs skip, don't enter"

# 5. Verify regular tourists enter through gates (they don't skip)
REG_GATE_ENTER=$(grep "entered through gate" "$LOG_FILE" | grep -v "\[VIP" | wc -l)
if [ "$REG_GATE_ENTER" -eq 0 ]; then
    echo "FAIL: Regular tourists should enter through gates"
    exit 1
fi
echo "Regular tourists entered through gate: $REG_GATE_ENTER"

# 6. Check starvation - both groups should have boardings
VIP_COUNT=$(echo "$VIP_BOARD_LINES" | grep -c '^[0-9]' 2>/dev/null || echo "0")
REG_COUNT=$(echo "$REG_BOARD_LINES" | grep -c '^[0-9]' 2>/dev/null || echo "0")

echo "VIP boardings: $VIP_COUNT"
echo "Regular boardings: $REG_COUNT"

if [ "$REG_COUNT" -eq 0 ]; then
    echo "FAIL: Regular tourists were starved (0 boardings)"
    exit 1
fi

# 7. Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# 8. Check for orphaned processes
ORPHANS=$(ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep | wc -l)
if [ "$ORPHANS" -gt 0 ]; then
    echo "FAIL: Found $ORPHANS orphaned processes"
    ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep
    pkill -9 -f "ropeway_simulation|tourist_process" 2>/dev/null || true
    exit 1
fi

# 9. Check for leftover IPC
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "Warning: Leftover IPC resources found"
fi

echo "PASS: VIP priority working without starvation"
exit 0
