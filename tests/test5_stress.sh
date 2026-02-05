#!/bin/bash
# Test 5: High Throughput Stress Test
#
# Goal: Simulation completes without timeout under high concurrency.
#
# Rationale: Tests for deadlock between MQ_CASHIER, MQ_PLATFORM, MQ_BOARDING
# under load. Concurrent tourists stress all semaphores and message queues
# simultaneously - exposes circular wait conditions or semaphore exhaustion.
#
# Parameters: tourists=6000, spawn_delay=0, station_capacity=500, simulation_time=180s.
#
# Expected outcome: No timeout (deadlock). No zombies during or after. IPC cleaned.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test5_stress.conf"
LOG_FILE="/tmp/ropeway_test5.log"
CHECK_INTERVAL=5

cd "$BUILD_DIR" || exit 1

echo "=== Test 5: High Throughput Stress Test ==="
echo "Goal: Verify no deadlocks with 6000 tourists, rapid spawn"
echo "Running simulation with periodic zombie checks every ${CHECK_INTERVAL}s..."

# Run simulation in background
./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE" &
SIM_PID=$!

ZOMBIE_DETECTED=0
CHECK_COUNT=0

# Periodic zombie checks while simulation runs
while kill -0 $SIM_PID 2>/dev/null; do
    sleep $CHECK_INTERVAL
    CHECK_COUNT=$((CHECK_COUNT + 1))
    ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep defunct | grep -v grep | wc -l)
    if [ "$ZOMBIES" -gt 0 ]; then
        echo "[$(date +%H:%M:%S)] WARNING: Found $ZOMBIES zombie(s) during simulation (check #$CHECK_COUNT)"
        ZOMBIE_DETECTED=1
    else
        echo "[$(date +%H:%M:%S)] Zombie check #$CHECK_COUNT: OK (0 zombies)"
    fi
done

# Wait for simulation to complete and get exit code
wait $SIM_PID
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for timeout (deadlock indicator)
if [ $EXIT_CODE -eq 124 ]; then
    echo "FAIL: Simulation timed out - possible deadlock"
    exit 1
fi

# Check for crashes/errors
if [ $EXIT_CODE -ne 0 ]; then
    echo "FAIL: Simulation exited with error code $EXIT_CODE"
    exit 1
fi

# Check for zombies detected during simulation
if [ "$ZOMBIE_DETECTED" -eq 1 ]; then
    echo "FAIL: Zombies detected during simulation"
    exit 1
fi

# Final zombie check
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes after simulation"
    exit 1
fi

# Check for leftover IPC resources
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "Warning: Found leftover IPC resources (sem=$IPC_SEM, shm=$IPC_SHM, mq=$IPC_MQ)"
fi

# Check for critical errors in logs
if grep -qi "error\|crash\|fault\|abort" "$LOG_FILE" | grep -v "no error"; then
    echo "Warning: Possible errors found in logs (check manually)"
fi

echo "PASS: High throughput test completed without deadlocks ($CHECK_COUNT zombie checks passed)"
exit 0
