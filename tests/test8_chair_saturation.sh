#!/bin/bash
# Test 8: Chair Saturation Test
# Verify SEM_CHAIRS blocks correctly when all chairs are in transit

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test8_chair_saturation.conf"
LOG_FILE="/tmp/ropeway_test8.log"
MAX_CHAIRS=36

cd "$BUILD_DIR" || exit 1

echo "=== Test 8: Chair Saturation Test ==="
echo "Goal: Verify chair semaphore (max=$MAX_CHAIRS) blocks correctly"
echo "Running simulation with slow chair travel..."

# Run simulation with timeout
timeout 210 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for timeout
if [ $EXIT_CODE -eq 124 ]; then
    echo "FAIL: Simulation timed out - possible chair semaphore deadlock"
    exit 1
fi

# Count chair departures and arrivals
DEPARTURES=$(grep -c "Chair.*departed\|boarded\|departing" "$LOG_FILE" 2>/dev/null || echo "0")
ARRIVALS=$(grep -c "Chair.*arrived\|unboarded\|arriving" "$LOG_FILE" 2>/dev/null || echo "0")

echo "Chair departures logged: $DEPARTURES"
echo "Chair arrivals logged: $ARRIVALS"

# Look for any indication of more than MAX_CHAIRS in transit simultaneously
# This would be logged as an error if it happened
if grep -qi "chairs in transit.*exceed\|semaphore.*negative" "$LOG_FILE"; then
    echo "FAIL: Chair capacity violation detected"
    exit 1
fi

# Check for zombies
ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "FAIL: Found $ZOMBIES zombie processes"
    exit 1
fi

# Check for leftover IPC
IPC_SEM=$(ipcs -s 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_SHM=$(ipcs -m 2>/dev/null | grep "$(id -u)" | wc -l)
IPC_MQ=$(ipcs -q 2>/dev/null | grep "$(id -u)" | wc -l)

if [ "$IPC_SEM" -gt 0 ] || [ "$IPC_SHM" -gt 0 ] || [ "$IPC_MQ" -gt 0 ]; then
    echo "Warning: Found leftover IPC resources"
fi

echo "PASS: Chair saturation test completed successfully"
exit 0
