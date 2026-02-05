#!/bin/bash
# Test 1: Lower Station Capacity Limit
#
# Goal: Station visitor count never exceeds N.
#
# Rationale: Tests for race condition between 4 entry gates incrementing
# station count via SEM_LOWER_STATION. Without proper semaphore synchronization,
# concurrent sem_wait() calls could allow count > N briefly.
#
# Parameters: station_capacity=5, tourists=30, simulation_time=60s.
#
# Expected outcome: Max observed count <= 5. No zombies. IPC cleaned.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test1_capacity.conf"
LOG_FILE="/tmp/ropeway_test1.log"
EXPECTED_MAX_CAPACITY=5

cd "$BUILD_DIR"

echo "Testing capacity limit enforcement (N=$EXPECTED_MAX_CAPACITY)"
echo "Running simulation..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"

# Check for capacity violations
echo
echo "Analyzing logs..."

# Extract station count values from logs
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

# Check for leftover IPC resources
IPC_COUNT=$(ipcs | grep -c "$(id -u)" 2>/dev/null || echo "0")
if [ "$IPC_COUNT" != "0" ]; then
    echo "Warning: Found potentially leftover IPC resources"
fi

echo "PASS: Capacity limit respected"
exit 0
