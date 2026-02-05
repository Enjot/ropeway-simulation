#!/bin/bash
# Test 14: SIGTERM Cleanup Test
#
# Goal: No orphaned IPC resources after SIGTERM shutdown.
#
# Rationale: Tests ipc_cleanup() is called from SIGTERM handler. Semaphores,
# shared memory, and message queues must be released via semctl(RMID),
# shmctl(RMID), msgctl(RMID). Leaked IPC persists until reboot.
#
# Parameters: Run 10s, send SIGTERM, verify ipcs shows no resources.
#
# Expected outcome: ipcs empty after shutdown. No zombies. All children terminated.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test1_capacity.conf"  # Use existing config
LOG_FILE="/tmp/ropeway_test14.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 14: SIGTERM Cleanup Test ==="
echo "Goal: Verify IPC resources are cleaned up after SIGTERM"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation in background..."
./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1 &
SIM_PID=$!

echo "Simulation PID: $SIM_PID"
echo "Waiting 10 seconds for simulation to start..."
sleep 1

# Verify simulation is running
if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation died before SIGTERM"
    exit 1
fi

# Check IPC resources exist
IPC_BEFORE_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_BEFORE_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_BEFORE_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "IPC resources before SIGTERM: sem=$IPC_BEFORE_SEM, shm=$IPC_BEFORE_SHM, mq=$IPC_BEFORE_MQ"

echo "Sending SIGTERM..."
kill -TERM $SIM_PID 2>/dev/null

echo "Waiting for cleanup (max 15 seconds)..."
for i in $(seq 1 15); do
    if ! kill -0 $SIM_PID 2>/dev/null; then
        echo "Simulation terminated after $i seconds"
        break
    fi
    sleep 1
done

# Force kill if still running
if kill -0 $SIM_PID 2>/dev/null; then
    echo "Warning: Simulation still running, sending SIGKILL"
    kill -9 $SIM_PID 2>/dev/null
    sleep 1
fi

# Check IPC resources after
sleep 1  # Give time for cleanup
IPC_AFTER_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_AFTER_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_AFTER_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "IPC resources after SIGTERM: sem=$IPC_AFTER_SEM, shm=$IPC_AFTER_SHM, mq=$IPC_AFTER_MQ"

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# Verify cleanup
if [ "$IPC_AFTER_SEM" -gt 0 ] || [ "$IPC_AFTER_SHM" -gt 0 ] || [ "$IPC_AFTER_MQ" -gt 0 ]; then
    echo "FAIL: Leftover IPC resources after SIGTERM"
    echo "Cleaning up manually..."
    ipcrm -a 2>/dev/null || true
    exit 1
fi

echo "PASS: SIGTERM cleanup successful - no leftover IPC resources"
exit 0
