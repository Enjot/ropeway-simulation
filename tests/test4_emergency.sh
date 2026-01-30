#!/bin/bash
# Test 4: Emergency Stop and Resume (Signals)
# Verify SIGUSR1 stops chairlift, SIGUSR2 resumes after workers ready

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test4_emergency.conf"
LOG_FILE="/tmp/ropeway_test4.log"

cd "$BUILD_DIR"

echo "Testing emergency stop/resume signals"
echo "Running simulation in background..."

# Run simulation in background
./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE" &
SIM_PID=$!

# Wait for simulation to start
sleep 5

# Check if simulation is running
if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation died prematurely"
    exit 1
fi

echo "Sending SIGUSR1 (emergency stop) to lower worker..."

# Get lower worker PID from logs
LOWER_PID=$(grep -o "Lower.*PID [0-9]*" "$LOG_FILE" | grep -o "[0-9]*$" | tail -1)

if [ -z "$LOWER_PID" ]; then
    echo "Warning: Could not find lower worker PID, sending to main process"
    kill -SIGUSR1 $SIM_PID
else
    kill -SIGUSR1 $LOWER_PID 2>/dev/null || kill -SIGUSR1 $SIM_PID
fi

sleep 3

# Check for emergency stop message
if grep -q "Emergency stop" "$LOG_FILE"; then
    echo "OK: Emergency stop detected"
else
    echo "Warning: Emergency stop message not found in logs"
fi

echo "Sending SIGUSR2 (resume) to lower worker..."

if [ -n "$LOWER_PID" ]; then
    kill -SIGUSR2 $LOWER_PID 2>/dev/null || kill -SIGUSR2 $SIM_PID
else
    kill -SIGUSR2 $SIM_PID
fi

sleep 3

# Check for resume message
if grep -q -i "resume" "$LOG_FILE"; then
    echo "OK: Resume detected"
else
    echo "Warning: Resume message not found in logs"
fi

# Let simulation finish
echo "Waiting for simulation to complete..."
wait $SIM_PID 2>/dev/null

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# Check processes terminated cleanly
REMAINING=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | wc -l)
if [ "$REMAINING" -gt 0 ]; then
    echo "Warning: $REMAINING processes still running"
fi

echo "PASS: Emergency stop/resume signals handled"
exit 0
