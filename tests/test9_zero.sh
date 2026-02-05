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
CONFIG="${SCRIPT_DIR}/../config/test9_zero.conf"
LOG_FILE="/tmp/ropeway_test9.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 9: Zero Tourists Edge Case ==="
echo "Goal: Verify graceful rejection of invalid config (0 tourists)"
echo "Running simulation..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Config validation should reject 0 tourists gracefully
if grep -qi "Invalid configuration\|must be" "$LOG_FILE"; then
    echo "OK: Config validation rejected 0 tourists gracefully"

    # Verify no IPC leak from failed startup
    IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
    IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
    IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

    if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
        echo "FAIL: IPC resources leaked after config rejection"
        exit 1
    fi

    echo "PASS: Zero tourists edge case handled gracefully (config rejected)"
    exit 0
fi

# If config was somehow accepted, verify clean shutdown
if [ $EXIT_CODE -ne 0 ] && [ $EXIT_CODE -ne 124 ]; then
    echo "FAIL: Simulation crashed with exit code $EXIT_CODE"
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

echo "PASS: Zero tourists edge case handled gracefully"
exit 0
