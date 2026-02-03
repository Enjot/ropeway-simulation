#!/bin/bash
# Test 6: Entry Gate Race Condition Test
# Verify capacity is never exceeded under rapid concurrent access
# Run multiple iterations to catch intermittent race conditions

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test6_race.conf"
LOG_FILE="/tmp/ropeway_test6.log"
EXPECTED_MAX_CAPACITY=5
ITERATIONS=10

cd "$BUILD_DIR" || exit 1

echo "=== Test 6: Entry Gate Race Condition Test ==="
echo "Goal: Verify capacity N=$EXPECTED_MAX_CAPACITY is never exceeded"
echo "Running $ITERATIONS iterations to catch race conditions..."

FAILURES=0

for i in $(seq 1 $ITERATIONS); do
    echo
    echo "--- Iteration $i/$ITERATIONS ---"

    # Run simulation with timeout
    timeout 150 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
    EXIT_CODE=$?

    if [ $EXIT_CODE -eq 124 ]; then
        echo "Warning: Iteration $i timed out"
        ((FAILURES++))
        continue
    fi

    # Extract max station count
    MAX_SEEN=$(grep -o "count: [0-9]*/" "$LOG_FILE" | sed 's/count: //' | sed 's/\///' | sort -n | tail -1)

    if [ -z "$MAX_SEEN" ]; then
        echo "Warning: No station count logs found in iteration $i"
        MAX_SEEN=0
    fi

    echo "Max capacity observed: $MAX_SEEN (limit: $EXPECTED_MAX_CAPACITY)"

    if [ "$MAX_SEEN" -gt "$EXPECTED_MAX_CAPACITY" ]; then
        echo "FAIL: Capacity exceeded in iteration $i ($MAX_SEEN > $EXPECTED_MAX_CAPACITY)"
        ((FAILURES++))
    fi

    # Check for zombies
    ZOMBIES=$(ps aux | grep -E "(ropeway|tourist)" | grep -v grep | grep defunct | wc -l)
    if [ "$ZOMBIES" -gt 0 ]; then
        echo "Warning: Found $ZOMBIES zombie processes after iteration $i"
    fi
done

echo
echo "=== Summary ==="
echo "Iterations: $ITERATIONS"
echo "Failures: $FAILURES"

if [ $FAILURES -gt 0 ]; then
    echo "FAIL: $FAILURES/$ITERATIONS iterations had capacity violations or timeouts"
    exit 1
fi

echo "PASS: All $ITERATIONS iterations respected capacity limit"
exit 0
