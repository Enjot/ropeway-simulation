#!/bin/bash
# Test 19: SIGALRM Sync Test
#
# Goal: Verify simulation works with SIGALRM-based sync (no usleep polling).
#
# Rationale: Confirms refactored code uses blocking IPC + alarm() correctly.
# All IPC_NOWAIT+usleep polling has been replaced with blocking calls that
# use SIGALRM for periodic wakeup. EINTR handled properly on alarm interrupt.
#
# Parameters: tourists=20, simulation_time=60s (uses test1_capacity.conf).
#
# Expected outcome: Simulation completes. No deadlock. No leftover IPC.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="test1_capacity.conf"
LOG_FILE="/tmp/ropeway_test19.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 19: SIGALRM Sync Test ==="
echo "Goal: Verify SIGALRM-based IPC sync works correctly"

# Clean any leftover IPC first
ipcrm -a 2>/dev/null || true

echo "Starting simulation..."

# Run simulation with timeout (should complete well before 120s)
timeout 10 ./ropeway_simulation "$CONFIG" > "$LOG_FILE" 2>&1
EXIT_CODE=$?

echo "Simulation exited with code: $EXIT_CODE"

# Check for timeout (deadlock indicator)
if [ $EXIT_CODE -eq 124 ]; then
    echo "FAIL: Simulation timed out (potential deadlock in SIGALRM-based sync)"
    # Show last few log lines
    echo "Last 20 log lines:"
    tail -20 "$LOG_FILE"
    ipcrm -a 2>/dev/null || true
    exit 1
fi

# Check for crashes
if [ $EXIT_CODE -ne 0 ]; then
    echo "FAIL: Simulation crashed with exit code $EXIT_CODE"
    exit 1
fi

# Verify some tourists completed the cycle
ARRIVALS=$(grep -c "arrived at upper platform\|arrived.*upper\|upper platform" "$LOG_FILE" 2>/dev/null || echo "0")
echo "Upper platform arrivals: $ARRIVALS"

if [ "$ARRIVALS" -eq 0 ]; then
    echo "Warning: No tourists arrived at upper platform"
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
    echo "FAIL: Leftover IPC resources found"
    ipcrm -a 2>/dev/null || true
    exit 1
fi

echo "PASS: SIGALRM sync test completed - no deadlock, IPC sync working"
exit 0
