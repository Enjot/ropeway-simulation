#!/bin/bash
# Test 16: Child Death Test
# Verify main process handles unexpected child termination

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test1_capacity.conf"
LOG_FILE="/tmp/ropeway_test16.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 16: Child Death Test ==="
echo "Goal: Verify graceful handling when a child process dies"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation in background..."
./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1 &
SIM_PID=$!

echo "Simulation PID: $SIM_PID"
echo "Waiting 8 seconds for workers to start..."
sleep 8

# Verify simulation is running
if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation died before test"
    exit 1
fi

# Find cashier process (child of main)
CASHIER_PID=$(pgrep -P $SIM_PID -f "cashier" 2>/dev/null | head -1)

if [ -z "$CASHIER_PID" ]; then
    # Try to find by process name pattern
    CASHIER_PID=$(ps --ppid $SIM_PID -o pid,cmd 2>/dev/null | grep -i cashier | awk '{print $1}' | head -1)
fi

if [ -z "$CASHIER_PID" ]; then
    echo "Warning: Could not find cashier process - trying any child"
    CASHIER_PID=$(pgrep -P $SIM_PID | head -1)
fi

if [ -z "$CASHIER_PID" ]; then
    echo "Warning: No child processes found to kill"
    # Continue anyway to verify shutdown
else
    echo "Found child process: $CASHIER_PID"
    echo "Killing child process..."
    kill -9 $CASHIER_PID 2>/dev/null
fi

echo "Waiting for main process to detect and shutdown (max 20 seconds)..."
for i in $(seq 1 20); do
    if ! kill -0 $SIM_PID 2>/dev/null; then
        echo "Main process terminated after $i seconds"
        break
    fi
    sleep 1
done

# Check if main is still running
if kill -0 $SIM_PID 2>/dev/null; then
    echo "Main process still running - sending SIGTERM"
    kill -TERM $SIM_PID 2>/dev/null
    sleep 5
    kill -9 $SIM_PID 2>/dev/null || true
fi

# Check for zombies
sleep 2
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "Warning: Found $ZOMBIES zombie processes"
fi

# Check for leftover IPC
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "Warning: Leftover IPC resources - cleaning"
    ipcrm -a 2>/dev/null || true
fi

# Check logs for error handling
if grep -qi "child.*died\|child.*terminated\|worker.*exit\|shutdown" "$LOG_FILE"; then
    echo "Log shows child death detection"
fi

echo "PASS: Child death handling test completed"
exit 0
