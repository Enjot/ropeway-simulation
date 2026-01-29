#!/bin/bash
# Test helper functions for ropeway simulation tests

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Global variables
TEST_NAME=""
REPORT_FILE=""
LOG_FILE=""
SIMULATION_PID=""
BUILD_DIR=""
PROJECT_DIR=""

# Find project and build directories
find_directories() {
    # Determine project directory based on where this helper file is
    local helper_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_DIR="$(cd "$helper_dir/../.." && pwd)"

    # Build directory can be passed as argument or default to project/build
    if [[ -n "$1" ]]; then
        BUILD_DIR="$1"
        # If relative path, make it relative to project dir
        if [[ ! "$BUILD_DIR" = /* ]]; then
            BUILD_DIR="$PROJECT_DIR/$BUILD_DIR"
        fi
    else
        BUILD_DIR="$PROJECT_DIR/build"
    fi

    if [[ ! -d "$BUILD_DIR" ]]; then
        echo -e "${RED}ERROR: Build directory not found: $BUILD_DIR${NC}"
        echo "Please build the project first: cd build && cmake .. && make"
        return 1
    fi

    if [[ ! -f "$BUILD_DIR/main" ]]; then
        echo -e "${RED}ERROR: main executable not found in $BUILD_DIR${NC}"
        echo "Please build the project first: cd build && cmake .. && make"
        return 1
    fi

    return 0
}

# Clean up IPC resources (shared memory, semaphores, message queues)
cleanup_ipc() {
    # Remove System V IPC resources that might be left over
    # Use ipcs to list and ipcrm to remove

    # Get current user
    local user=$(whoami)

    # Clean shared memory
    ipcs -m 2>/dev/null | grep "$user" | awk '{print $2}' | while read shmid; do
        ipcrm -m "$shmid" 2>/dev/null
    done

    # Clean semaphores
    ipcs -s 2>/dev/null | grep "$user" | awk '{print $2}' | while read semid; do
        ipcrm -s "$semid" 2>/dev/null
    done

    # Clean message queues
    ipcs -q 2>/dev/null | grep "$user" | awk '{print $2}' | while read msqid; do
        ipcrm -q "$msqid" 2>/dev/null
    done
}

# Kill any leftover simulation processes
cleanup_processes() {
    # Kill any remaining simulation processes (ignore errors if none found)
    pkill -f "main$" 2>/dev/null || true
    pkill -f "tourist_process" 2>/dev/null || true
    pkill -f "cashier_process" 2>/dev/null || true
    pkill -f "lower_worker_process" 2>/dev/null || true
    pkill -f "upper_worker_process" 2>/dev/null || true
    pkill -f "logger_process" 2>/dev/null || true

    # Wait a moment for processes to die
    sleep 1
}

# Setup test environment
# Usage: setup_test "Test Name"
setup_test() {
    TEST_NAME="$1"
    local test_id="$2"

    echo -e "${YELLOW}Setting up: $TEST_NAME${NC}"

    # Create reports directory if needed
    mkdir -p "$PROJECT_DIR/tests/reports"

    # Set report file path
    REPORT_FILE="$PROJECT_DIR/tests/reports/test${test_id}_$(echo "$TEST_NAME" | tr ' ' '_' | tr '[:upper:]' '[:lower:]').txt"
    LOG_FILE="$PROJECT_DIR/tests/reports/test${test_id}_simulation.log"

    # Cleanup
    cleanup_processes
    cleanup_ipc

    # Clear old log and report
    rm -f "$LOG_FILE" "$REPORT_FILE"

    echo "Setup complete"
}

# Run the simulation with timeout
# Usage: run_simulation <timeout_seconds>
run_simulation() {
    local timeout_sec="${1:-60}"

    echo "Running simulation (timeout: ${timeout_sec}s)..."

    # Change to build directory to run simulation
    cd "$BUILD_DIR" || return 1

    # Cross-platform timeout implementation
    # Start simulation in background, strip ANSI codes for clean logs
    ./main 2>&1 | strip_ansi > "$LOG_FILE" &
    local sim_pid=$!

    # Wait with timeout
    local waited=0
    while [[ $waited -lt $timeout_sec ]]; do
        if ! kill -0 "$sim_pid" 2>/dev/null; then
            # Process finished
            wait "$sim_pid"
            return $?
        fi
        sleep 1
        waited=$((waited + 1))
    done

    # Timeout reached - kill process
    echo -e "${YELLOW}Simulation timed out after ${timeout_sec}s${NC}"
    kill -TERM "$sim_pid" 2>/dev/null
    sleep 1
    kill -KILL "$sim_pid" 2>/dev/null
    wait "$sim_pid" 2>/dev/null
    cleanup_processes
    return 124
}

# Check for zombie processes
# Returns 0 if no zombies, 1 if zombies found
check_no_zombies() {
    local zombies=$(ps aux | grep -E "(tourist_process|cashier_process|worker_process|logger_process)" | grep -v grep | wc -l)

    if [[ $zombies -eq 0 ]]; then
        return 0
    else
        echo "Found $zombies leftover processes"
        return 1
    fi
}

# Check if a pattern exists in the log
# Usage: check_log_pattern "pattern"
check_log_pattern() {
    local pattern="$1"

    if grep -q "$pattern" "$LOG_FILE" 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

# Count occurrences of pattern in log
# Usage: count_log_pattern "pattern"
count_log_pattern() {
    local pattern="$1"
    local count
    count=$(grep -c "$pattern" "$LOG_FILE" 2>/dev/null) || count=0
    echo "$count"
}

# Extract numeric value from log pattern
# Usage: extract_max_value "pattern" <field_number>
# Example: extract_max_value "\[STATION_COUNT\] current=" 1
extract_max_from_logs() {
    local pattern="$1"
    local field="$2"

    grep "$pattern" "$LOG_FILE" 2>/dev/null | \
        sed "s/.*$field=\([0-9]*\).*/\1/" | \
        sort -n | tail -1
}

# Assert that a numeric value in logs never exceeds max
# Usage: assert_max_value "pattern" "field" max_allowed
# Returns 0 if all values <= max, 1 otherwise
assert_max_value() {
    local pattern="$1"
    local field="$2"
    local max_allowed="$3"

    local max_found=$(extract_max_from_logs "$pattern" "$field")

    if [[ -z "$max_found" ]]; then
        echo "No matches found for pattern: $pattern"
        return 2
    fi

    if [[ $max_found -le $max_allowed ]]; then
        return 0
    else
        echo "Max value $max_found exceeds allowed $max_allowed"
        return 1
    fi
}

# Write report header
# Usage: report_header "Test Name" "var1=val1" "var2=val2" ...
report_header() {
    local test_name="$1"
    shift

    {
        echo "================================================================================"
        echo "TEST REPORT: $test_name"
        echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
        echo "================================================================================"
        echo ""
        echo "CONFIGURATION:"
        for var in "$@"; do
            echo "  - $var"
        done
        echo ""
        echo "RESULTS:"
    } > "$REPORT_FILE"
}

# Add pass/fail check to report
# Usage: report_check <pass|fail> "message"
report_check() {
    local status="$1"
    local message="$2"

    if [[ "$status" == "pass" ]]; then
        echo "  [PASS] $message" >> "$REPORT_FILE"
    else
        echo "  [FAIL] $message" >> "$REPORT_FILE"
    fi
}

# Add statistics section to report
# Usage: report_stats "stat1: value1" "stat2: value2" ...
report_stats() {
    {
        echo ""
        echo "STATISTICS:"
        for stat in "$@"; do
            echo "  - $stat"
        done
    } >> "$REPORT_FILE"
}

# Strip ANSI color codes from text
strip_ansi() {
    sed 's/\x1b\[[0-9;]*m//g'
}

# Add log excerpts to report
# Usage: report_log_excerpts "pattern" count
report_log_excerpts() {
    local pattern="$1"
    local count="${2:-10}"

    {
        echo ""
        echo "VERIFICATION LOG EXCERPTS:"
        grep "$pattern" "$LOG_FILE" 2>/dev/null | strip_ansi | head -n "$count" | while read line; do
            echo "  $line"
        done
        local total
        total=$(grep -c "$pattern" "$LOG_FILE" 2>/dev/null) || total=0
        if [[ $total -gt $count ]]; then
            echo "  ... ($total total matches)"
        fi
    } >> "$REPORT_FILE"
}

# Finalize report with overall result
# Usage: report_finish <PASS|FAIL>
report_finish() {
    local result="$1"

    {
        echo ""
        echo "RESULT: $result"
        echo "================================================================================"
    } >> "$REPORT_FILE"

    if [[ "$result" == "PASS" ]]; then
        echo -e "${GREEN}$TEST_NAME: PASS${NC}"
        return 0
    else
        echo -e "${RED}$TEST_NAME: FAIL${NC}"
        return 1
    fi
}

# Generate summary report from individual test reports
# Usage: generate_summary test1_result test2_result ...
generate_summary() {
    local summary_file="$PROJECT_DIR/tests/reports/test_summary.txt"
    local total=0
    local passed=0

    {
        echo "================================================================================"
        echo "ROPEWAY SIMULATION TEST SUMMARY"
        echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
        echo "================================================================================"
        echo ""
    } > "$summary_file"

    for result in "$@"; do
        local test_name=$(echo "$result" | cut -d: -f1)
        local test_status=$(echo "$result" | cut -d: -f2)

        total=$((total + 1))

        if [[ "$test_status" == "PASS" ]]; then
            passed=$((passed + 1))
            printf "Test %d: %-30s PASS\n" "$total" "$test_name" >> "$summary_file"
        else
            printf "Test %d: %-30s FAIL\n" "$total" "$test_name" >> "$summary_file"
        fi
    done

    {
        echo ""
        echo "TOTAL: $passed/$total tests passed"
        echo ""
        echo "Detailed reports available in tests/reports/"
        echo "================================================================================"
    } >> "$summary_file"

    # Return success only if all tests passed
    if [[ $passed -eq $total ]]; then
        return 0
    else
        return 1
    fi
}

# Parse daily_report.txt for statistics
# Usage: get_report_stat "field_name"
get_report_stat() {
    local field="$1"
    local report="$BUILD_DIR/daily_report.txt"

    if [[ ! -f "$report" ]]; then
        echo "0"
        return
    fi

    grep "$field" "$report" 2>/dev/null | awk '{print $NF}' | head -1
}

# Export functions for use in test scripts
export -f find_directories
export -f cleanup_ipc
export -f cleanup_processes
export -f setup_cleanup_trap

# Setup trap for SIGINT/SIGTERM to clean up on interrupt
# Usage: call setup_cleanup_trap at the start of test scripts
setup_cleanup_trap() {
    trap 'echo ""; echo "Interrupted - cleaning up..."; cleanup_processes; cleanup_ipc; exit 130' INT TERM
}
export -f setup_test
export -f run_simulation
export -f check_no_zombies
export -f check_log_pattern
export -f count_log_pattern
export -f extract_max_from_logs
export -f assert_max_value
export -f report_header
export -f report_check
export -f report_stats
export -f report_log_excerpts
export -f report_finish
export -f generate_summary
export -f get_report_stat
export -f strip_ansi
