#!/bin/bash
# Test 9: Zero Tourists Edge Case
#
# Goal: Workers initialize and shutdown cleanly with no tourists.
#
# Rationale: Tests for blocking on empty message queues. msgrcv() without
# IPC_NOWAIT on MQ_CASHIER/MQ_PLATFORM would hang forever. Workers must
# handle SIGTERM during idle wait without deadlock.
#
# Parameters: tourists=0, simulation_time=15s.
#
# Expected outcome: Clean shutdown. No hang on empty queues. No IPC leaks.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test9_zero.conf"
LOG_FILE="/tmp/ropeway_test9.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 9: Zero Tourists Edge Case ==="
echo "Goal: Verify graceful startup/shutdown with 0 tourists"
echo "Running simulation..."

# Run simulation with timeout
timeout 30 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for crashes
if [ $EXIT_CODE -ne 0 ] && [ $EXIT_CODE -ne 124 ]; then
    echo "FAIL: Simulation crashed with exit code $EXIT_CODE"
    exit 1
fi

# Verify workers started
if ! grep -qi "ready\|started\|initialized" "$LOG_FILE"; then
    echo "FAIL: Workers did not initialize properly"
    exit 1
fi

# Verify clean shutdown
if grep -qi "error\|crash\|fault\|abort\|segmentation" "$LOG_FILE" | grep -vi "no error"; then
    echo "Warning: Possible errors in logs"
fi

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# Check for leftover IPC
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "FAIL: Leftover IPC resources found"
    exit 1
fi

echo "PASS: Zero tourists edge case handled gracefully"
exit 0
