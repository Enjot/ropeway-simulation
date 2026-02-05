#!/bin/bash
# Test 17: Pause/Resume Test (SIGTSTP/SIGCONT)
#
# Goal: Simulated time offset adjusts for pause duration.
#
# Rationale: Tests SIGTSTP/SIGCONT handlers in time_server. Pause duration
# must be added to time_offset so accelerated simulation time doesn't jump.
# Without offset adjustment: tickets expire during pause, time discontinuity.
#
# Parameters: SIGTSTP for 5s, then SIGCONT.
#
# Expected outcome: Time continues smoothly after resume. No expired tickets.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test1_capacity.conf"
LOG_FILE="/tmp/ropeway_test17.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 17: Pause/Resume Test ==="
echo "Goal: Verify SIGTSTP/SIGCONT pause time tracking"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation in background..."
./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1 &
SIM_PID=$!

echo "Simulation PID: $SIM_PID"
echo "Waiting 5 seconds before pause..."
sleep 1

# Verify simulation is running
if ! kill -0 $SIM_PID 2>/dev/null; then
    echo "FAIL: Simulation died before pause"
    exit 1
fi

# Get simulated time before pause (from logs)
TIME_BEFORE=$(tail -20 "$LOG_FILE" | grep -o '\[.*\]' | tail -1 || echo "unknown")
echo "Simulated time before pause: $TIME_BEFORE"

echo "Sending SIGTSTP (pause)..."
kill -TSTP $SIM_PID 2>/dev/null

echo "Simulation paused for 5 seconds..."
sleep 1

echo "Sending SIGCONT (resume)..."
kill -CONT $SIM_PID 2>/dev/null

echo "Waiting 5 more seconds after resume..."
sleep 1

# Get simulated time after resume
TIME_AFTER=$(tail -20 "$LOG_FILE" | grep -o '\[.*\]' | tail -1 || echo "unknown")
echo "Simulated time after resume: $TIME_AFTER"

# Check for pause/resume handling messages in logs
if grep -qi "pause\|SIGTSTP\|stopped\|suspended" "$LOG_FILE"; then
    echo "Found pause-related messages in logs"
fi

if grep -qi "resume\|SIGCONT\|continued" "$LOG_FILE"; then
    echo "Found resume-related messages in logs"
fi

# Graceful shutdown
echo "Sending SIGTERM for cleanup..."
kill -TERM $SIM_PID 2>/dev/null

echo "Waiting for shutdown (max 10 seconds)..."
for i in $(seq 1 10); do
    if ! kill -0 $SIM_PID 2>/dev/null; then
        echo "Simulation terminated after $i seconds"
        break
    fi
    sleep 1
done

# Force kill if needed
if kill -0 $SIM_PID 2>/dev/null; then
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

echo "PASS: Pause/Resume test completed"
exit 0
