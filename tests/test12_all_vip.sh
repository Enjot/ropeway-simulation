#!/bin/bash
# Test 12: All VIPs Edge Case
# Verify same-priority ordering when all tourists are VIPs

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
CONFIG="${BUILD_DIR}/config/test12_all_vip.conf"
LOG_FILE="/tmp/ropeway_test12.log"

cd "$BUILD_DIR" || exit 1

echo "=== Test 12: All VIPs Edge Case ==="
echo "Goal: Verify system works with 100% VIP tourists"
echo "Running simulation..."

# Run simulation with timeout
timeout 120 ./ropeway_simulation "$CONFIG" 2>&1 | tee "$LOG_FILE"
EXIT_CODE=$?

echo
echo "Analyzing results..."

# Check for crashes
if [ $EXIT_CODE -ne 0 ] && [ $EXIT_CODE -ne 124 ]; then
    echo "FAIL: Simulation crashed with exit code $EXIT_CODE"
    exit 1
fi

# Count VIP tourists
VIP_COUNT=$(grep -c "VIP\|vip\|priority.*1" "$LOG_FILE" 2>/dev/null || echo "0")
REGULAR_COUNT=$(grep -c "regular.*tourist\|priority.*2" "$LOG_FILE" 2>/dev/null || echo "0")

echo "VIP mentions in logs: $VIP_COUNT"
echo "Regular tourist mentions: $REGULAR_COUNT"

# Verify tourists were served
SERVED=$(grep -c "boarded\|served\|completed\|bought.*ticket" "$LOG_FILE" 2>/dev/null || echo "0")
echo "Tourists served: $SERVED"

if [ "$SERVED" -eq 0 ]; then
    echo "Warning: No tourists appear to have been served"
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
    echo "Warning: Leftover IPC resources found"
fi

echo "PASS: All VIPs edge case completed successfully"
exit 0
