#!/bin/bash
# Run all tests

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

PASSED=0
FAILED=0

run_test() {
    local test_name="$1"
    local test_script="$2"

    echo "=========================================="
    echo "Running: $test_name"
    echo "=========================================="

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

# Run tests
cd "$BUILD_DIR"

run_test "Test 1: Capacity Limit" "${SCRIPT_DIR}/test1_capacity.sh"
run_test "Test 3: VIP Priority" "${SCRIPT_DIR}/test3_vip.sh"
run_test "Test 4: Emergency Stop" "${SCRIPT_DIR}/test4_emergency.sh"

# Summary
echo "=========================================="
echo "SUMMARY"
echo "=========================================="
echo -e "Passed: ${GREEN}${PASSED}${NC}"
echo -e "Failed: ${RED}${FAILED}${NC}"
echo

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed.${NC}"
    exit 1
fi
