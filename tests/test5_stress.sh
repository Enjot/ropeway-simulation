#!/bin/bash
# Test 5: High Throughput Stress Test
# Verify no deadlocks under high load with 500 tourists, rapid spawning

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test5_stress.conf"
LOG_FILE="/tmp/ropeway_test5.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 5: High Throughput Stress Test ==="
echo "Goal: Verify no deadlocks with 500 tourists, rapid spawn"
echo "Running simulation (this may take a while)..."

# Run simulation with timeout (5 minutes + buffer)
timeout 360 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for timeout (deadlock indicator)
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
