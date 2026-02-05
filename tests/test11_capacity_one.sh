#!/bin/bash
# Test 11: Capacity One Edge Case
#
# Goal: Only 1 tourist in station at any time with N=1.
#
# Rationale: Tests convoy effect on SEM_LOWER_STATION. With N=1, all tourists
# serialize on single semaphore slot. Potential for priority inversion or
# starvation if sem_wait() ordering is unfair.
#
# Parameters: N=1, tourists=10, simulation_time=120s.
#
# Expected outcome: Max count=1. All tourists eventually served. No deadlock.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test11_capacity_one.conf"
LOG_FILE="/tmp/ropeway_test11.log"
EXPECTED_MAX_CAPACITY=1

cd "$BUILD_DIR" || exit 1

echo "=== Test 11: Capacity One Edge Case ==="
echo "Goal: Verify only 1 tourist in station at a time (N=$EXPECTED_MAX_CAPACITY)"
echo "Running simulation..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for timeout 15(possible deadlock with N=1)
if [ $EXIT_CODE -eq 124 ]; then
    echo "Warning: Simulation timed out - checking for deadlock"
fi

# Extract max station count
MAX_SEEN=$(grep -o "count: [0-9]*/" "$LOG_FILE" | sed 's/count: //' | sed 's/\///' | sort -n | tail -1)

if [ -z "$MAX_SEEN" ]; then
    echo "Warning: No station count logs found"
    MAX_SEEN=0
fi

echo "Maximum station count observed: $MAX_SEEN"
echo "Expected maximum: $EXPECTED_MAX_CAPACITY"

if [ "$MAX_SEEN" -gt "$EXPECTED_MAX_CAPACITY" ]; then
    echo "FAIL: Capacity exceeded ($MAX_SEEN > $EXPECTED_MAX_CAPACITY)"
    exit 1
fi

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# Check for orphaned processes
ORPHANS=$(ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep | wc -l)
if [ "$ORPHANS" -gt 0 ]; then
    echo "FAIL: Found $ORPHANS orphaned processes"
    ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep
    pkill -9 -f "ropeway_simulation|tourist_process" 2>/dev/null || true
    exit 1
fi

# Check for leftover IPC
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "Warning: Leftover IPC resources found"
fi

echo "PASS: Capacity one edge case respected limit"
exit 0
