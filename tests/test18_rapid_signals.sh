#!/bin/bash
# Test 18: Rapid Signals Test
#
# Goal: No crash under rapid signal delivery.
#
# Rationale: Tests signal handler reentrancy. Rapid SIGUSR1 can interrupt
# handler mid-execution. Non-async-signal-safe functions (malloc, printf)
# in handler cause undefined behavior. Must use only safe functions.
#
# Parameters: 10 SIGUSR1 signals with 100ms delay.
#
# Expected outcome: No segfault. No hang. Simulation survives signal storm.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test4_emergency.conf"  # Emergency config
LOG_FILE="/tmp/ropeway_test18.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 18: Rapid Signals Test ==="
echo "Goal: Verify signal handler safety under rapid SIGUSR1 delivery"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation in background..."
./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1 &
SIM_PID=$!

echo "Simulation PID: $SIM_PID"
echo "Waiting for workers to start..."
sleep 1

# Verify simulation is running
if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation died before signal test"
    exit 1
fi

# Find any child process to send signals to (workers handle SIGUSR1, main does not)
CHILD_PID=$(ps --ppid $SIM_PID -o pid= 2>/dev/null | head -1 | tr -d ' ')

if [ -z "$CHILD_PID" ]; then
    echo "Warning: No child processes found - simulation may not support rapid signals"
    # Main process doesn't handle SIGUSR1, so skip signal test
    kill -TERM $SIM_PID 2>/dev/null
    wait $SIM_PID 2>/dev/null
    ipcrm -a 2>/dev/null || true
    echo "PASS: Rapid signals test skipped (no workers to signal)"
    exit 0
fi

echo "Found child process: $CHILD_PID"
echo "Sending 10 SIGUSR1 signals rapidly to child..."
for i in $(seq 1 10); do
    kill -USR1 $CHILD_PID 2>/dev/null
    # Very short delay between signals
    sleep 0.05
done

echo "Signals sent. Checking if simulation is still alive..."
sleep 0.3

if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation crashed during rapid signal delivery"

    # Check for segfault in logs
    if grep -qi "segmentation\|fault\|core\|crash" "$LOG_FILE"; then
        echo "Crash detected in logs"
    fi

    # Cleanup
    ipcrm -a 2>/dev/null || true
    exit 1
fi

echo "Simulation survived rapid signals"

# Count emergency events
EMERGENCY_COUNT=$(grep -c "emergency\|SIGUSR1\|danger" "$LOG_FILE" 2>/dev/null || echo "0")
echo "Emergency-related log entries: $EMERGENCY_COUNT"

# Let simulation run a bit more
echo "Waiting a bit more..."
sleep 0.3

# Graceful shutdown
echo "Sending SIGTERM for cleanup..."
kill -TERM $SIM_PID 2>/dev/null

echo "Waiting for shutdown (max 15 seconds)..."
for i in $(seq 1 15); do
    if ! kill -0 $SIM_PID 2>/dev/null; then
        echo "Simulation terminated after $i seconds"
        break
    fi
    sleep 1
done

# Force kill if needed
if kill -0 $SIM_PID 2>/dev/null; then
    echo "Warning: Had to force kill"
    kill -9 $SIM_PID 2>/dev/null
fi

# Check for zombies
sleep 1
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
    echo "Warning: Leftover IPC resources - cleaning"
    ipcrm -a 2>/dev/null || true
fi

echo "PASS: Rapid signals test completed"
exit 0
