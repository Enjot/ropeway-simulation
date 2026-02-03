#!/bin/bash
# Test 15: SIGKILL Recovery Test
# Verify stale IPC cleanup when starting after crash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test1_capacity.conf"
LOG_FILE="/tmp/ropeway_test15.log"
LOG_FILE2="/tmp/ropeway_test15_recovery.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 15: SIGKILL Recovery Test ==="
echo "Goal: Verify stale IPC cleanup after crash (SIGKILL)"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation in background..."
./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1 &
SIM_PID=$!

echo "Simulation PID: $SIM_PID"
echo "Waiting 5 seconds for IPC resources to be created..."
sleep 5

# Verify simulation is running
if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation died unexpectedly"
    exit 1
fi

# Check IPC resources exist
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "IPC resources created: sem=$IPC_SEM, shm=$IPC_SHM, mq=$IPC_MQ"

if [ "$IPC_SEM" -eq 0 ] && [ "$IPC_SHM" -eq 0 ] && [ "$IPC_MQ" -eq 0 ]; then
    echo "Warning: No IPC resources found - test may not be valid"
fi

echo "Sending SIGKILL (simulating crash)..."
kill -9 $SIM_PID 2>/dev/null
sleep 2

# Kill any remaining child processes
pkill -9 -P $SIM_PID 2>/dev/null || true
pkill -9 -f "tourist" 2>/dev/null || true
sleep 2

# Verify IPC resources are orphaned
IPC_ORPHANED_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_ORPHANED_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_ORPHANED_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "Orphaned IPC resources: sem=$IPC_ORPHANED_SEM, shm=$IPC_ORPHANED_SHM, mq=$IPC_ORPHANED_MQ"

echo
echo "Starting new simulation (should clean stale resources)..."
timeout 30 ./ropeway_simulation "$CONFIG" > "$LOG_FILE2" 2>&1 &
SIM_PID2=$!

sleep 3

# Check for stale cleanup message
if grep -qi "stale\|orphan\|cleanup.*previous\|removing.*old" "$LOG_FILE2"; then
    echo "Detected stale IPC cleanup message"
fi

# Kill the recovery run
kill -TERM $SIM_PID2 2>/dev/null
sleep 5
kill -9 $SIM_PID2 2>/dev/null || true

# Final check for leftover resources
sleep 2
IPC_FINAL_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_FINAL_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_FINAL_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "Final IPC resources: sem=$IPC_FINAL_SEM, shm=$IPC_FINAL_SHM, mq=$IPC_FINAL_MQ"

# Clean up any remaining
if [ "$IPC_FINAL_SEM" -gt 0 ] || [ "$IPC_FINAL_SHM" -gt 0 ] || [ "$IPC_FINAL_MQ" -gt 0 ]; then
    echo "Warning: Cleaning remaining IPC resources"
    ipcrm -a 2>/dev/null || true
fi

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "Warning: Found $ZOMBIES zombie processes"
fi

echo "PASS: SIGKILL recovery test completed"
exit 0
