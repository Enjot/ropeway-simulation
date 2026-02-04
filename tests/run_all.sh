#!/bin/bash
# Run all tests
# Usage: ./run_all.sh [category]
# Categories: all, integration, stress, edge, recovery, signal, sync

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0

run_test() {
    local test_name="$1"
    local test_script="$2"

    echo "=========================================="
    echo "Running: $test_name"
    echo "=========================================="

    if [ ! -f "$test_script" ]; then
        echo -e "${YELLOW}SKIPPED${NC}: $test_name (script not found)"
        ((SKIPPED++))
        return
    fi

    if bash "$test_script"; then
        echo -e "${GREEN}PASSED${NC}: $test_name"
        ((PASSED++))
    else
        echo -e "${RED}FAILED${NC}: $test_name"
        ((FAILED++))
    fi
    echo
}

# Check build directory
if [ ! -f "${BUILD_DIR}/ropeway_simulation" ]; then
    echo "Error: ropeway_simulation not found. Run cmake and make first."
    exit 1
fi

if [ ! -f "${BUILD_DIR}/tourist" ]; then
    echo "Error: tourist executable not found. Run cmake and make first."
    exit 1
fi

# Parse category argument
CATEGORY="${1:-all}"

cd "$BUILD_DIR"

echo "=========================================="
echo "ROPEWAY SIMULATION TEST SUITE"
echo "Category: $CATEGORY"
echo "=========================================="
echo

# Integration tests (Tests 1-4)
if [ "$CATEGORY" = "all" ] || [ "$CATEGORY" = "integration" ]; then
    echo ">>> INTEGRATION TESTS <<<"
    run_test "Test 1: Capacity Limit" "${SCRIPT_DIR}/test1_capacity.sh"
    run_test "Test 2: Children/Guardians" "${SCRIPT_DIR}/test2_children.sh"
    run_test "Test 3: VIP Priority" "${SCRIPT_DIR}/test3_vip.sh"
    run_test "Test 4: Emergency Stop" "${SCRIPT_DIR}/test4_emergency.sh"
fi

# Stress tests (Tests 5-8)
if [ "$CATEGORY" = "all" ] || [ "$CATEGORY" = "stress" ]; then
    echo ">>> STRESS TESTS <<<"
    run_test "Test 5: High Throughput" "${SCRIPT_DIR}/test5_stress.sh"
    run_test "Test 6: Entry Gate Race" "${SCRIPT_DIR}/test6_race.sh"
    run_test "Test 7: Emergency Race" "${SCRIPT_DIR}/test7_emergency_race.sh"
    run_test "Test 8: Chair Saturation" "${SCRIPT_DIR}/test8_chair_saturation.sh"
fi

# Edge case tests (Tests 9-13)
if [ "$CATEGORY" = "all" ] || [ "$CATEGORY" = "edge" ]; then
    echo ">>> EDGE CASE TESTS <<<"
    run_test "Test 9: Zero Tourists" "${SCRIPT_DIR}/test9_zero.sh"
    run_test "Test 10: Single Tourist" "${SCRIPT_DIR}/test10_single.sh"
    run_test "Test 11: Capacity One" "${SCRIPT_DIR}/test11_capacity_one.sh"
    run_test "Test 12: All VIPs" "${SCRIPT_DIR}/test12_all_vip.sh"
    run_test "Test 13: All Families" "${SCRIPT_DIR}/test13_all_families.sh"
fi

# Recovery tests (Tests 14-16)
if [ "$CATEGORY" = "all" ] || [ "$CATEGORY" = "recovery" ]; then
    echo ">>> RECOVERY TESTS <<<"
    run_test "Test 14: SIGTERM Cleanup" "${SCRIPT_DIR}/test14_sigterm.sh"
    run_test "Test 15: SIGKILL Recovery" "${SCRIPT_DIR}/test15_sigkill_recovery.sh"
    run_test "Test 16: Child Death" "${SCRIPT_DIR}/test16_child_death.sh"
fi

# Signal tests (Tests 17-18)
if [ "$CATEGORY" = "all" ] || [ "$CATEGORY" = "signal" ]; then
    echo ">>> SIGNAL TESTS <<<"
    run_test "Test 17: Pause/Resume" "${SCRIPT_DIR}/test17_pause_resume.sh"
    run_test "Test 18: Rapid Signals" "${SCRIPT_DIR}/test18_rapid_signals.sh"
fi

# Sync correctness tests (Test 19)
if [ "$CATEGORY" = "all" ] || [ "$CATEGORY" = "sync" ]; then
    echo ">>> SYNC CORRECTNESS TESTS <<<"
    run_test "Test 19: SIGALRM Sync" "${SCRIPT_DIR}/test19_sigalrm_sync.sh"
fi

# Summary
echo "=========================================="
echo "SUMMARY"
echo "=========================================="
echo -e "Passed:  ${GREEN}${PASSED}${NC}"
echo -e "Failed:  ${RED}${FAILED}${NC}"
echo -e "Skipped: ${YELLOW}${SKIPPED}${NC}"
TOTAL=$((PASSED + FAILED))
echo "Total:   $TOTAL"
echo

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
