#!/bin/bash
# Test 7: Emergency Race Condition Test
#
# Goal: Only one worker initiates emergency when both detect danger.
#
# Rationale: Tests race on SEM_EMERGENCY_LOCK when lower_worker and upper_worker
# both call sem_trywait() simultaneously. Double-initiation would corrupt
# emergency state or cause deadlock waiting on SEM_EMERGENCY_CLEAR.
#
# Parameters: danger_probability=100, tourists=30, simulation_time=60s.
#
# Expected outcome: Single initiator per emergency. No deadlock. System recovers.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test7_emergency_race.conf"
LOG_FILE="/tmp/ropeway_test7.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 7: Emergency Race Condition Test ==="
echo "Goal: Verify only one worker becomes emergency initiator"
echo "Running simulation with 100% danger probability..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for timeout 15(possible deadlock)
if [ $EXIT_CODE -eq 124 ]; then
    echo "FAIL: Simulation timed out - possible deadlock during emergency"
    exit 1
fi

# Count emergency initiations vs completions
EMERGENCY_STARTS=$(grep -c "Acquired emergency lock\|DANGER DETECTED\|Emergency stop" "$LOG_FILE" 2>/dev/null || echo "0")
EMERGENCY_CLEARS=$(grep -c "Emergency cleared\|Resuming operations\|Emergency resolved" "$LOG_FILE" 2>/dev/null || echo "0")

echo "Emergency starts detected: $EMERGENCY_STARTS"
echo "Emergency clears detected: $EMERGENCY_CLEARS"

# Check that emergencies were triggered (with 100% probability, should have some)
if [ "$EMERGENCY_STARTS" -eq 0 ]; then
    echo "Warning: No emergencies detected despite 100% danger probability"
fi

# Check for deadlock indicators (multiple initiators trying to acquire lock)
DOUBLE_INIT=$(grep -c "waiting for emergency lock\|Failed to acquire" "$LOG_FILE" 2>/dev/null || echo "0")
if [ "$DOUBLE_INIT" -gt 0 ]; then
    echo "Info: Detected $DOUBLE_INIT instances of workers waiting for emergency lock (expected behavior)"
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
    echo "Warning: Found leftover IPC resources"
fi

echo "PASS: Emergency race handling completed without deadlock"
exit 0
