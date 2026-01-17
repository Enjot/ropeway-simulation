# Building and Running

This document explains how to build, run, and troubleshoot the Ropeway Simulation.

## Prerequisites

### Required Software

| Software | Minimum Version | Check Command |
|----------|-----------------|---------------|
| C++ Compiler | C++17 support | `g++ --version` or `clang++ --version` |
| CMake | 3.16 | `cmake --version` |
| Make | Any | `make --version` |

### Supported Platforms

- ✅ Linux (Ubuntu, Debian, Fedora, etc.)
- ✅ macOS (10.15+)
- ⚠️ Windows (WSL2 recommended)

---

## Building

### Quick Build

```bash
# Clone or navigate to the project
cd ropeway-simulation

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build all targets
make -j$(nproc)
# or on macOS:
make -j$(sysctl -n hw.ncpu)
```

### Build Targets

```bash
# Build everything
make all

# Build specific targets
make main              # Main orchestrator
make cashier_process   # Cashier executable
make worker1_process   # Worker1 executable
make worker2_process   # Worker2 executable
make tourist_process   # Tourist executable
make test_runner       # Test suite
```

### Build Types

```bash
# Debug build (default)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build (optimized)
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Release with debug info
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make
```

### Build Output

After building, you'll have these executables in `build/`:

```
build/
├── main                 # Main entry point
├── cashier_process      # Cashier process
├── worker1_process      # Lower station worker
├── worker2_process      # Upper station worker
├── tourist_process      # Tourist process
└── test_runner          # Test suite
```

---

## Running

### Basic Execution

```bash
cd build

# Run with default settings
./main

# Run with custom parameters
./main --tourists 100 --duration 60
```

### Command-Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--tourists N` | 500 | Number of tourists to spawn |
| `--duration S` | 30 | Simulation duration in seconds |
| `--capacity N` | 50 | Station capacity |

### Example Runs

```bash
# Quick test (few tourists, short duration)
./main --tourists 20 --duration 15

# Stress test
./main --tourists 1000 --duration 120

# Minimal run for debugging
./main --tourists 5 --duration 10
```

### Output

The simulation produces:
1. **Console output**: Real-time logging with colors
2. **Daily report**: Saved to `daily_report_<timestamp>.txt`

---

## Running Tests

```bash
cd build

# Run all tests
./test_runner

# Run with verbose output
./test_runner --verbose
```

---

## Troubleshooting

### Problem: "Failed to create shared memory"

**Cause**: Leftover IPC resources from a previous crash.

**Solution**:
```bash
# List IPC resources
ipcs

# Remove by key (replace with actual key)
ipcrm -M 0x1064
ipcrm -S 0x2064
ipcrm -Q 0x3064

# Or remove by ID
ipcrm -m <shmid>
ipcrm -s <semid>
ipcrm -q <msgid>
```

### Problem: Processes not terminating

**Cause**: Hung processes or zombies.

**Solution**:
```bash
# Find stuck processes
ps aux | grep -E "(worker|cashier|tourist|main)"

# Kill by PID
kill -9 <pid>

# Find and kill all related processes
ps -eo pid,comm | grep -E "worker|cashier|tourist" | awk '{print $1}' | xargs kill -9
```

### Problem: "Permission denied" for executables

**Solution**:
```bash
chmod +x build/main
chmod +x build/*_process
```

### Problem: Build fails with "C++17 required"

**Solution**:
```bash
# Check compiler version
g++ --version

# On Ubuntu, install newer GCC
sudo apt install g++-11

# Set compiler in CMake
cmake -DCMAKE_CXX_COMPILER=g++-11 ..
```

### Problem: "semget: No space left on device"

**Cause**: System limit on semaphores reached.

**Solution**:
```bash
# Check current limits (Linux)
cat /proc/sys/kernel/sem

# Temporarily increase (requires root)
echo "250 32000 100 128" | sudo tee /proc/sys/kernel/sem

# Clean up old semaphores
ipcs -s | awk '/your_username/ {print $2}' | xargs -I {} ipcrm -s {}
```

---

## IPC Cleanup Script

Create a cleanup script for development:

```bash
#!/bin/bash
# cleanup_ipc.sh

echo "Cleaning up IPC resources..."

# Kill related processes
ps -eo pid,comm | grep -E "worker|cashier|tourist|main" | awk '{print $1}' | while read pid; do
    kill -9 $pid 2>/dev/null
done

# Remove IPC resources owned by current user
ipcs -m | awk -v user="$USER" '$3 == user {print $2}' | while read id; do
    ipcrm -m $id 2>/dev/null
done

ipcs -s | awk -v user="$USER" '$3 == user {print $2}' | while read id; do
    ipcrm -s $id 2>/dev/null
done

ipcs -q | awk -v user="$USER" '$3 == user {print $2}' | while read id; do
    ipcrm -q $id 2>/dev/null
done

echo "Cleanup complete."
ipcs
```

Usage:
```bash
chmod +x cleanup_ipc.sh
./cleanup_ipc.sh
```

---

## Development Workflow

### 1. Make Changes

Edit source files in:
- `processes/` - Process logic
- `ipc/` - IPC wrappers
- `common/` - Configuration
- `structures/` - Data structures

### 2. Rebuild

```bash
cd build
make -j$(nproc)
```

### 3. Test

```bash
# Quick test
./main --tourists 10 --duration 10

# Run test suite
./test_runner
```

### 4. Debug

```bash
# Run with GDB
gdb ./main
(gdb) run --tourists 10 --duration 10

# Run with LLDB (macOS)
lldb ./main
(lldb) run --tourists 10 --duration 10

# Debug specific process
gdb --args ./tourist_process <args...>
```

### 5. View Logs

```bash
# Filter by process
./main 2>&1 | grep "Worker1"

# Filter by log level
./main 2>&1 | grep "ERROR"

# Save to file
./main 2>&1 | tee simulation.log
```

---

## CMakeLists.txt Explained

```cmake
cmake_minimum_required(VERSION 3.16)
project(ropeway_simulation)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Main executable
add_executable(main main.cpp)

# Process executables (each is a separate binary)
add_executable(cashier_process processes/cashier_process.cpp)
add_executable(worker1_process processes/worker1_process.cpp)
add_executable(worker2_process processes/worker2_process.cpp)
add_executable(tourist_process processes/tourist_process.cpp)

# Include directories
target_include_directories(main PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(cashier_process PRIVATE ${CMAKE_SOURCE_DIR})
# ... etc
```

---

## Performance Tuning

### For Many Tourists

```cpp
// In common/config.hpp

// Reduce delays for faster simulation
namespace Timing {
    constexpr int ARRIVAL_DELAY_BASE_US = 1000;      // 1ms between arrivals
    constexpr int CHAIR_RIDE_TIME_MS = 100;          // 100ms ride time
}
```

### For Long Simulations

```cpp
// Increase array sizes
namespace Limits {
    constexpr uint32_t MAX_TOURISTS = 10000;
    constexpr uint32_t MAX_TOURIST_RECORDS = 10000;
}
```

### Reduce Logging

```cpp
// In utils/Logger.hpp
// Comment out DEBUG level logging for production
#define LOG_LEVEL INFO
```

---

## See Also

- [Architecture](ARCHITECTURE.md) - System overview
- [Processes](PROCESSES.md) - Process details
- [IPC Mechanisms](IPC_MECHANISMS.md) - IPC details
