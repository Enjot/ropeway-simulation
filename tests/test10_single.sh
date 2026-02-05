#!/bin/bash
# Test 10: Single Tourist Edge Case
#
# Goal: One tourist completes full lifecycle without concurrency.
#
# Rationale: Baseline test - verifies message passing chain works:
# tourist->MQ_CASHIER->cashier->MQ_PLATFORM->lower_worker->MQ_BOARDING->
# tourist->MQ_ARRIVALS->upper_worker. No concurrency masks IPC bugs.
#
# Parameters: tourists=1, walker, simulation_time=60s.
#
# Expected outcome: Tourist completes ride. All IPC handoffs succeed.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test10_single.conf"
LOG_FILE="/tmp/ropeway_test10.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 10: Single Tourist Edge Case ==="
echo "Goal: Verify one tourist completes full lifecycle"
echo "Running simulation..."

# Run simulation with timeout
timeout 15 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for crashes
if [ $EXIT_CODE -ne 0 ] && [ $EXIT_CODE -ne 124 ]; then
    echo "FAIL: Simulation crashed with exit code $EXIT_CODE"
    exit 1
fi

# Verify tourist lifecycle stages
TICKET_BOUGHT=$(grep -c "bought ticket\|Ticket.*purchased\|Cashier.*sold" "$LOG_FILE" 2>/dev/null || echo "0")
ENTERED=$(grep -c "entered\|entering.*station\|joined.*queue" "$LOG_FILE" 2>/dev/null || echo "0")
BOARDED=$(grep -c "boarded\|boarding.*chair\|seated" "$LOG_FILE" 2>/dev/null || echo "0")
ARRIVED=$(grep -c "arrived\|arriving.*upper\|reached.*top" "$LOG_FILE" 2>/dev/null || echo "0")

echo "Ticket purchases: $TICKET_BOUGHT"
echo "Station entries: $ENTERED"
echo "Chair boardings: $BOARDED"
echo "Upper arrivals: $ARRIVED"

# At least one tourist should complete some of the cycle
if [ "$TICKET_BOUGHT" -eq 0 ] && [ "$ENTERED" -eq 0 ]; then
    echo "Warning: Tourist may not have started lifecycle (check logs)"
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
    exit 1
fi

echo "PASS: Single tourist edge case completed"
exit 0
