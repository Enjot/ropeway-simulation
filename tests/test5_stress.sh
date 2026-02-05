#!/bin/bash
# Test 5: High Throughput Stress Test
#
# Goal: Simulation completes without timeout 30under high concurrency.
#
# Rationale: Tests for deadlock between MQ_CASHIER, MQ_PLATFORM, MQ_BOARDING
# under load. 500 concurrent tourists stress all semaphores and message queues
# simultaneously - exposes circular wait conditions or semaphore exhaustion.
#
# Parameters: tourists=500, spawn_delay=0, station_capacity=50, simulation_time=300s.
#
# Expected outcome: No timeout 30(deadlock). No zombies. IPC cleaned.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test5_stress.conf"
LOG_FILE="/tmp/ropeway_test5.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 5: High Throughput Stress Test ==="
echo "Goal: Verify no deadlocks with 500 tourists, rapid spawn"
echo "Running simulation (this may take a while)..."

# Run simulation with timeout 30(5 minutes + buffer)
timeout 30 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for timeout 30(deadlock indicator)
if [ $EXIT_CODE -eq 124 ]; then
    echo "FAIL: Simulation timed out - possible deadlock"
    exit 1
fi

# Check for crashes/errors
if [ $EXIT_CODE -ne 0 ]; then
    echo "FAIL: Simulation exited with error code $EXIT_CODE"
    exit 1
fi

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# Check for leftover IPC resources
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "Warning: Found leftover IPC resources (sem=$IPC_SEM, shm=$IPC_SHM, mq=$IPC_MQ)"
fi

# Check for critical errors in logs
if grep -qi "error\|crash\|fault\|abort" "$LOG_FILE" | grep -v "no error"; then
    echo "Warning: Possible errors found in logs (check manually)"
fi

echo "PASS: High throughput test completed without deadlocks"
exit 0
