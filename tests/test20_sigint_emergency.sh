#!/bin/bash
# Test 20: SIGINT During Emergency Stop
#
# Goal: Graceful shutdown when Ctrl+C is sent during emergency stop.
#
# Rationale: Workers blocked on msgrcv() during emergency handshake must wake up
# on SIGINT (EINTR) and proceed with shutdown. Emergency lock (SEM_EMERGENCY_LOCK)
# and emergency clear semaphore must be released. IPC cleanup must complete.
#
# Parameters: danger_probability=100, long danger duration to ensure SIGINT hits
# during emergency state.
#
# Expected outcome: Clean shutdown. No zombies. No leftover IPC resources.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test20_sigint_emergency.conf"
LOG_FILE="/tmp/ropeway_test20.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 20: SIGINT During Emergency Stop ==="
echo "Goal: Verify graceful shutdown when Ctrl+C sent during emergency"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation in background..."
./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1 &
SIM_PID=$!

echo "Simulation PID: $SIM_PID"
echo "Waiting for emergency stop to occur..."

# Wait for emergency stop (max 10 seconds)
EMERGENCY_DETECTED=0
for i in $(seq 1 20); do
    if grep -q "Emergency stop\|Acquired emergency lock" "$LOG_FILE" 2>/dev/null; then
        EMERGENCY_DETECTED=1
        echo "Emergency stop detected after ${i}x0.5 seconds"
        break
    fi

    # Check if simulation died
    if ! kill -0 $SIM_PID 2>/dev/null; then
        echo "FAIL: Simulation died before emergency could occur"
        cat "$LOG_FILE"
        exit 1
    fi

    sleep 0.5
done

if [ "$EMERGENCY_DETECTED" -eq 0 ]; then
    echo "Warning: Emergency not detected within timeout, proceeding anyway"
fi

# Give a moment for emergency state to settle
sleep 0.3

# Check IPC resources exist before SIGINT
IPC_BEFORE_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_BEFORE_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_BEFORE_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "IPC resources before SIGINT: sem=$IPC_BEFORE_SEM, shm=$IPC_BEFORE_SHM, mq=$IPC_BEFORE_MQ"

echo "Sending SIGINT (Ctrl+C) during emergency..."
kill -INT $SIM_PID 2>/dev/null

echo "Waiting for cleanup (max 15 seconds)..."
TERMINATED=0
for i in $(seq 1 15); do
    if ! kill -0 $SIM_PID 2>/dev/null; then
        echo "Simulation terminated after $i seconds"
        TERMINATED=1
        break
    fi
    sleep 1
done

# Force kill if still running
if [ "$TERMINATED" -eq 0 ]; then
    echo "Warning: Simulation still running after SIGINT, sending SIGKILL"
    kill -9 $SIM_PID 2>/dev/null
    sleep 1
    echo "FAIL: Simulation did not respond to SIGINT during emergency"

    # Cleanup
    ipcrm -a 2>/dev/null || true
    exit 1
fi

# Check IPC resources after
sleep 1
IPC_AFTER_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_AFTER_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_AFTER_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

echo "IPC resources after SIGINT: sem=$IPC_AFTER_SEM, shm=$IPC_AFTER_SHM, mq=$IPC_AFTER_MQ"

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep
    exit 1
fi

# Check for orphaned processes
ORPHANS=$(ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep | wc -l)
if [ "$ORPHANS" -gt 0 ]; then
    echo "FAIL: Found $ORPHANS orphaned processes"
    ps aux | grep -E "(ropeway_simulation|tourist_process)" | grep -v grep
    # Kill them
    pkill -9 -f "ropeway_simulation|tourist_process" 2>/dev/null || true
    exit 1
fi

# Verify IPC cleanup
if [ "$IPC_AFTER_SEM" -gt 0 ] || [ "$IPC_AFTER_SHM" -gt 0 ] || [ "$IPC_AFTER_MQ" -gt 0 ]; then
    echo "FAIL: Leftover IPC resources after SIGINT during emergency"
    echo "Cleaning up manually..."
    ipcrm -a 2>/dev/null || true
    exit 1
fi

# Check log for clean shutdown messages
if grep -q "Shutdown complete\|cleanup\|Cleaning" "$LOG_FILE" 2>/dev/null; then
    echo "OK: Shutdown messages found in log"
fi

echo "PASS: SIGINT during emergency handled gracefully"
exit 0
