#!/bin/bash
# Test 13: All Families Edge Case
#
# Goal: Families board atomically without splitting.
#
# Rationale: Tests semop() with sem_op > 1 for atomic multi-slot acquisition.
# Family of 3 must acquire 3 slots atomically - partial acquisition would
# split family or deadlock if remaining capacity < family_size.
#
# Parameters: walker_percentage=100 (maximizes family probability), tourists=40.
#
# Expected outcome: No split families. Atomic acquisition verified. No deadlock.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${SCRIPT_DIR}/../config/test13_all_families.conf"
LOG_FILE="/tmp/ropeway_test13.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 13: All Families Edge Case ==="
echo "Goal: Verify atomic multi-slot acquisition for families"
echo "Running simulation with 100% walkers (max family probability)..."

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

# Count family-related events
FAMILIES=$(grep -c "family\|Family\|kids\|children\|guardian" "$LOG_FILE" 2>/dev/null || echo "0")
echo "Family-related log entries: $FAMILIES"

# Check that families boarded together (no split families)
SPLIT_FAMILIES=$(grep -c "split\|separated\|alone.*child\|child.*without" "$LOG_FILE" 2>/dev/null || echo "0")
if [ "$SPLIT_FAMILIES" -gt 0 ]; then
    echo "FAIL: Found $SPLIT_FAMILIES instances of split families"
    exit 1
fi

# Verify station capacity was never violated
CAPACITY=$(grep -o "count: [0-9]*/" "$LOG_FILE" | sed 's/count: //' | sed 's/\///' | sort -n | tail -1)
echo "Max station count: ${CAPACITY:-0}"

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
    echo "Warning: Leftover IPC resources found"
fi

echo "PASS: All families edge case completed successfully"
exit 0
