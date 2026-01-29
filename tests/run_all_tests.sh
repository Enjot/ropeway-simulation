#!/bin/bash
# Main test runner for ropeway simulation tests
# Runs all integration tests and generates summary report

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Source helper functions
source "$SCRIPT_DIR/lib/test_helpers.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse command line arguments
BUILD_DIR=""
VERBOSE=false
SINGLE_TEST=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -t|--test)
            SINGLE_TEST="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -b, --build-dir DIR   Path to build directory (default: ../build)"
            echo "  -v, --verbose         Show detailed output"
            echo "  -t, --test NUM        Run only test number NUM (1-4)"
            echo "  -h, --help            Show this help"
            exit 0
            ;;
        *)
            BUILD_DIR="$1"
            shift
            ;;
    esac
done

# Initialize directories
if ! find_directories "$BUILD_DIR"; then
    exit 1
fi

echo -e "${BLUE}========================================"
echo "=== Running Ropeway Simulation Tests ==="
echo -e "========================================${NC}"
echo ""
echo "Build directory: $BUILD_DIR"
echo "Project directory: $PROJECT_DIR"
echo ""

# Track results
declare -a RESULTS
TOTAL=0
PASSED=0

# Function to run a test
run_test() {
    local test_num="$1"
    local test_script="$2"
    local test_name="$3"

    TOTAL=$((TOTAL + 1))

    echo -e "${YELLOW}----------------------------------------${NC}"
    echo -e "${YELLOW}Running Test $test_num: $test_name${NC}"
    echo -e "${YELLOW}----------------------------------------${NC}"

    local report_file="$PROJECT_DIR/tests/reports/test${test_num}_*.txt"

    local test_script_path="$SCRIPT_DIR/$test_script"

    if $VERBOSE; then
        if bash "$test_script_path" "$BUILD_DIR"; then
            PASSED=$((PASSED + 1))
            RESULTS+=("$test_name:PASS")
            echo -e "${GREEN}Test $test_num: PASS${NC}"
        else
            RESULTS+=("$test_name:FAIL")
            echo -e "${RED}Test $test_num: FAIL${NC}"
        fi
    else
        # Capture output for summary
        local output_file="/tmp/test_${test_num}_output.txt"
        if bash "$test_script_path" "$BUILD_DIR" > "$output_file" 2>&1; then
            PASSED=$((PASSED + 1))
            RESULTS+=("$test_name:PASS")
            echo -e "Test $test_num: $test_name... ${GREEN}PASS${NC}"
            # Show report location
            local actual_report=$(ls $PROJECT_DIR/tests/reports/test${test_num}_*.txt 2>/dev/null | grep -v simulation | head -1)
            if [[ -n "$actual_report" ]]; then
                echo "  Report: $actual_report"
            fi
        else
            RESULTS+=("$test_name:FAIL")
            echo -e "Test $test_num: $test_name... ${RED}FAIL${NC}"
            # Show last few lines of output on failure
            echo "  Last output:"
            tail -5 "$output_file" 2>/dev/null | sed 's/^/    /'
            local actual_report=$(ls $PROJECT_DIR/tests/reports/test${test_num}_*.txt 2>/dev/null | grep -v simulation | head -1)
            if [[ -n "$actual_report" ]]; then
                echo "  Report: $actual_report"
            fi
        fi
        rm -f "$output_file"
    fi

    echo ""
}

# Run tests
if [[ -n "$SINGLE_TEST" ]]; then
    case $SINGLE_TEST in
        1) run_test 1 "test_station_capacity.sh" "Station Capacity Limit" ;;
        2) run_test 2 "test_children_guardian.sh" "Children with Guardian" ;;
        3) run_test 3 "test_vip_priority.sh" "VIP Priority" ;;
        4) run_test 4 "test_emergency_stop.sh" "Emergency Stop/Resume" ;;
        *)
            echo -e "${RED}Invalid test number: $SINGLE_TEST${NC}"
            echo "Valid tests: 1-4"
            exit 1
            ;;
    esac
else
    run_test 1 "test_station_capacity.sh" "Station Capacity Limit"
    run_test 2 "test_children_guardian.sh" "Children with Guardian"
    run_test 3 "test_vip_priority.sh" "VIP Priority"
    run_test 4 "test_emergency_stop.sh" "Emergency Stop/Resume"
fi

# Generate summary
echo -e "${BLUE}========================================"
echo "=== Test Summary ==="
echo -e "========================================${NC}"
echo ""

# Generate summary report file
generate_summary "${RESULTS[@]}"

# Print summary
if [[ $PASSED -eq $TOTAL ]]; then
    echo -e "${GREEN}=== $PASSED/$TOTAL tests passed ===${NC}"
else
    echo -e "${RED}=== $PASSED/$TOTAL tests passed ===${NC}"
fi

echo ""
echo "Summary report: $PROJECT_DIR/tests/reports/test_summary.txt"
echo "Detailed reports: $PROJECT_DIR/tests/reports/"
echo ""

# List report files
echo "Generated reports:"
ls -1 "$PROJECT_DIR/tests/reports/"*.txt 2>/dev/null | sed 's/^/  /'

# Exit with appropriate code
if [[ $PASSED -eq $TOTAL ]]; then
    exit 0
else
    exit 1
fi
