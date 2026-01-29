# Ropeway Simulation

A multi-process simulation of a ski lift (ropeway) system implemented in C++17 using System V IPC mechanisms.

## Overview

This project simulates a ropeway/chairlift operation with:
- Multiple tourist processes
- Worker processes (lower and upper station controllers)
- Cashier process for ticket sales
- Logger process for centralized logging
- Shared memory for state coordination
- Message queues for inter-process communication
- Semaphores for synchronization

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```bash
./main
```

Configuration is loaded from `ropeway.env` file.

## Testing

The project includes integration tests implemented as shell scripts.

### Running All Tests

Using CMake:
```bash
cd build
make test
```

Or directly:
```bash
./tests/run_all_tests.sh build
```

### Test Runner Options

```bash
./tests/run_all_tests.sh [OPTIONS]

Options:
  -b, --build-dir DIR   Path to build directory (default: build)
  -v, --verbose         Show detailed output
  -t, --test NUM        Run only test number NUM (1-4)
  -h, --help            Show help
```

### Available Tests

| # | Test Name | Description |
|---|-----------|-------------|
| 1 | Station Capacity Limit | Verifies station capacity is enforced |
| 2 | Children with Guardian | Verifies children ride with guardians |
| 3 | VIP Priority | Verifies VIP tourists get priority |
| 4 | Emergency Stop/Resume | Verifies emergency stop and resume |

### Examples

Run a single test:
```bash
./tests/run_all_tests.sh -t 1
```

Run with verbose output:
```bash
./tests/run_all_tests.sh -v
```

### Test Reports

Reports are generated in `tests/reports/`:
- `test_summary.txt` - Overall summary
- `testN_*.txt` - Individual test reports
- `testN_simulation.log` - Simulation logs for each test

---

## Functions

This section documents all custom functions defined in the project headers.

### core/Config.h

Configuration management from environment variables.

#### `Config::loadEnvFile()`
Load configuration from ropeway.env file.

Reads key=value pairs from the env file and sets them as environment variables. Existing environment variables are not overwritten.

- **Throws**: `std::runtime_error` if the env file cannot be opened

---

#### `Config::validate()`
Validate all required configuration values.

Attempts to load all configuration values, which will throw an exception if any required environment variable is missing.

- **Throws**: `std::runtime_error` if any required configuration is missing

---

#### `Config::Runtime::getEnv(const char* envName)`
Get required uint32 environment variable.

- **Parameters**:
  - `envName` - Name of the environment variable
- **Returns**: Parsed uint32 value
- **Throws**: `std::runtime_error` if variable is not set

---

#### `Config::Runtime::getEnvFloat(const char* envName)`
Get required float environment variable.

- **Parameters**:
  - `envName` - Name of the environment variable
- **Returns**: Parsed float value
- **Throws**: `std::runtime_error` if variable is not set

---

#### `Config::Runtime::getEnvBool(const char* envName)`
Get required boolean environment variable.

- **Parameters**:
  - `envName` - Name of the environment variable
- **Returns**: `true` if value is non-zero, `false` otherwise
- **Throws**: `std::runtime_error` if variable is not set

---

#### `Config::Runtime::getEnvOr(const char* envName, uint32_t defaultValue)`
Get uint32 environment variable with default fallback.

- **Parameters**:
  - `envName` - Name of the environment variable
  - `defaultValue` - Value to return if variable is not set
- **Returns**: Parsed uint32 value or default

---

#### `Config::Runtime::getEnvFloatOr(const char* envName, float defaultValue)`
Get float environment variable with default fallback.

- **Parameters**:
  - `envName` - Name of the environment variable
  - `defaultValue` - Value to return if variable is not set
- **Returns**: Parsed float value or default

---

### core/RopewayState.h

#### `toString(RopewayState state)`
Convert RopewayState enum to string representation.

- **Parameters**:
  - `state` - RopewayState to convert
- **Returns**: String name of the state ("STOPPED", "RUNNING", "EMERGENCY_STOP", "CLOSING")

---

### utils/ProcessSpawner.h

Process lifecycle management utilities.

#### `ProcessSpawner::getExecutablePath(const char* processName)`
Get the full path to an executable in the same directory as current process.

Uses platform-specific methods (macOS: `_NSGetExecutablePath`, Linux: `/proc/self/exe`).

- **Parameters**:
  - `processName` - Name of the target executable
- **Returns**: Full path to the executable

---

#### `ProcessSpawner::spawn(const char* processName, const std::vector<std::string>& args)`
Spawn a new process using fork/exec.

Parent process returns immediately with child PID.

- **Parameters**:
  - `processName` - Name of the executable
  - `args` - Vector of command-line arguments (excluding program name)
- **Returns**: Child PID on success, -1 on failure

---

#### `ProcessSpawner::spawnWithKeys(const char* processName, key_t key1, key_t key2, key_t key3)`
Spawn a process with three IPC keys. Convenience wrapper around `spawn()`.

---

#### `ProcessSpawner::spawnWithKeys(const char* processName, key_t key1, key_t key2, key_t key3, key_t key4)`
Spawn a process with four IPC keys. Convenience wrapper around `spawn()`.

---

#### `ProcessSpawner::spawnWithKeys(const char* processName, key_t key1, key_t key2, key_t key3, key_t key4, key_t key5)`
Spawn a process with five IPC keys. Convenience wrapper around `spawn()`.

---

#### `ProcessSpawner::terminate(pid_t pid, const char* name = nullptr)`
Terminate a process gracefully (SIGTERM) then forcefully (SIGKILL).

- **Parameters**:
  - `pid` - Process ID to terminate
  - `name` - Optional name for logging

---

#### `ProcessSpawner::terminateAll(const std::vector<pid_t>& pids)`
Send SIGTERM to multiple processes.

- **Parameters**:
  - `pids` - Vector of process IDs to terminate

---

#### `ProcessSpawner::waitFor(pid_t pid)`
Wait for a specific process to exit (blocking).

Handles ECHILD (already reaped) and EINTR (interrupted by signal).

- **Parameters**:
  - `pid` - Process ID to wait for

---

#### `ProcessSpawner::waitForAll()`
Reap any zombie child processes (non-blocking).

---

### utils/SignalHelper.h

Signal handling utilities for inter-process communication.

#### `SignalHelper::setup(Flags& flags, bool handleUserSignals)`
Install signal handlers for a process.

Installs handlers for SIGTERM and SIGINT. If handleUserSignals is true, also installs handlers for SIGUSR1 (emergency) and SIGUSR2 (resume).

- **Parameters**:
  - `flags` - Reference to Flags structure that will be updated by handlers
  - `handleUserSignals` - If true, also handle SIGUSR1/SIGUSR2 for emergency protocol

---

#### `SignalHelper::setup(Flags& flags, Mode mode)`
Install signal handlers based on process mode.

WORKER and TOURIST modes enable SIGUSR1/SIGUSR2 handling.

- **Parameters**:
  - `flags` - Reference to Flags structure that will be updated by handlers
  - `mode` - Process mode (BASIC, WORKER, or TOURIST)

---

#### `SignalHelper::setupPauseHandler(time_t* totalPausedSeconds)`
Install SIGTSTP handler for pause tracking (main process only).

- **Parameters**:
  - `totalPausedSeconds` - Pointer to totalPausedSeconds field in shared memory

---

#### `SignalHelper::ignoreChildren()`
Set SIGCHLD to SIG_IGN for automatic zombie reaping.

When SIGCHLD is ignored, terminated children are automatically reaped by the kernel.

---

#### `SignalHelper::shouldExit(const Flags& flags)`
Check if exit signal was received.

- **Parameters**:
  - `flags` - Reference to Flags structure
- **Returns**: `true` if SIGTERM or SIGINT was received

---

#### `SignalHelper::isEmergency(const Flags& flags)`
Check if emergency signal was received.

- **Parameters**:
  - `flags` - Reference to Flags structure
- **Returns**: `true` if SIGUSR1 (emergency stop) was received

---

#### `SignalHelper::isResumeRequested(const Flags& flags)`
Check if resume signal was received.

- **Parameters**:
  - `flags` - Reference to Flags structure
- **Returns**: `true` if SIGUSR2 (resume) was received

---

#### `SignalHelper::clearFlag(volatile sig_atomic_t& flag)`
Clear a signal flag.

- **Parameters**:
  - `flag` - Reference to the flag to clear

---

### utils/TimeHelper.h

Helper for simulated time calculations.

#### `TimeHelper::adjustedNow(time_t totalPausedSeconds)`
Get current time adjusted for simulation pauses (Ctrl+Z).

- **Parameters**:
  - `totalPausedSeconds` - Cumulative seconds spent suspended
- **Returns**: Wall-clock time minus paused time

---

#### `TimeHelper::getSimulatedSeconds(time_t simulationStartTime, time_t totalPausedSeconds = 0)`
Calculate simulated time from real time.

- **Parameters**:
  - `simulationStartTime` - When the simulation started (real time)
  - `totalPausedSeconds` - Cumulative seconds spent suspended (default 0)
- **Returns**: Simulated time as seconds since midnight

---

#### `TimeHelper::formatTime(time_t simulationStartTime, char* buffer, time_t totalPausedSeconds = 0)`
Format simulated time as HH:MM string.

- **Parameters**:
  - `simulationStartTime` - When the simulation started
  - `buffer` - Output buffer (at least 6 bytes)
  - `totalPausedSeconds` - Cumulative paused seconds

---

#### `TimeHelper::formatTimeFull(time_t simulationStartTime, char* buffer, time_t totalPausedSeconds = 0)`
Format simulated time as HH:MM:SS string.

- **Parameters**:
  - `simulationStartTime` - When the simulation started
  - `buffer` - Output buffer (at least 9 bytes)
  - `totalPausedSeconds` - Cumulative paused seconds

---

#### `TimeHelper::isPastClosingTime(time_t simulationStartTime, time_t totalPausedSeconds = 0)`
Check if simulated time is past closing hour.

- **Parameters**:
  - `simulationStartTime` - When the simulation started
  - `totalPausedSeconds` - Cumulative paused seconds
- **Returns**: `true` if past closing time

---

#### `TimeHelper::getSimulatedHour(time_t simulationStartTime, time_t totalPausedSeconds = 0)`
Get simulated hour (0-23).

- **Parameters**:
  - `simulationStartTime` - When the simulation started
  - `totalPausedSeconds` - Cumulative paused seconds
- **Returns**: Current simulated hour

---

### utils/ArgumentParser.h

Command-line argument parsing utilities.

#### `ArgumentParser::parseUint32(const char* str, uint32_t& out)`
Parse string to uint32_t.

- **Parameters**:
  - `str` - Input string
  - `out` - Output value
- **Returns**: `true` if parsing succeeded, `false` otherwise

---

#### `ArgumentParser::parseInt32(const char* str, int32_t& out)`
Parse string to int32_t.

- **Parameters**:
  - `str` - Input string
  - `out` - Output value
- **Returns**: `true` if parsing succeeded, `false` otherwise

---

#### `ArgumentParser::parseKeyT(const char* str, key_t& out)`
Parse string to key_t (IPC key).

- **Parameters**:
  - `str` - Input string
  - `out` - Output key value
- **Returns**: `true` if parsing succeeded, `false` otherwise

---

#### `ArgumentParser::parseBool(const char* str, bool& out)`
Parse string to boolean (0 or 1).

- **Parameters**:
  - `str` - Input string ("0" or "1")
  - `out` - Output boolean value
- **Returns**: `true` if parsing succeeded (valid 0 or 1), `false` otherwise

---

#### `ArgumentParser::parseEnum(const char* str, int min, int max, int& out)`
Parse string to enum value within range.

- **Parameters**:
  - `str` - Input string
  - `min` - Minimum valid value (inclusive)
  - `max` - Maximum valid value (inclusive)
  - `out` - Output integer value
- **Returns**: `true` if parsing succeeded and value is in range, `false` otherwise

---

#### `ArgumentParser::parseWorkerArgs(int argc, char* argv[], WorkerArgs& args)`
Parse command-line arguments for worker process.

Expected: `<shmKey> <semKey> <msgKey> <entryGateMsgKey> <logMsgKey>`

- **Parameters**:
  - `argc` - Argument count
  - `argv` - Argument values
  - `args` - Output WorkerArgs structure
- **Returns**: `true` if all arguments were parsed successfully

---

#### `ArgumentParser::parseCashierArgs(int argc, char* argv[], CashierArgs& args)`
Parse command-line arguments for cashier process.

Expected: `<shmKey> <semKey> <cashierMsgKey> <logMsgKey>`

- **Parameters**:
  - `argc` - Argument count
  - `argv` - Argument values
  - `args` - Output CashierArgs structure
- **Returns**: `true` if all arguments were parsed successfully

---

#### `ArgumentParser::parseTouristArgs(int argc, char* argv[], TouristArgs& args)`
Parse command-line arguments for tourist process.

Expected (13 args): `<id> <age> <type> <isVip> <wantsToRide> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey> <entryGateMsgKey> <logMsgKey>`

Expected (14 args): `<id> <age> <type> <isVip> <wantsToRide> <numChildren> <trail> <shmKey> <semKey> <msgKey> <cashierMsgKey> <entryGateMsgKey> <logMsgKey>`

- **Parameters**:
  - `argc` - Argument count
  - `argv` - Argument values
  - `args` - Output TouristArgs structure
- **Returns**: `true` if all arguments were parsed successfully

---

### logging/Logger.h

Centralized and decentralized logging system.

#### `Logger::initCentralized(key_t shmKey, key_t semKey, key_t logQueueKey)`
Initialize centralized logging mode.

After calling this, all log messages are sent to the logger process via message queue instead of being printed directly.

- **Parameters**:
  - `shmKey` - Shared memory key for accessing state
  - `semKey` - Semaphore key for synchronization
  - `logQueueKey` - Message queue key for log messages

---

#### `Logger::cleanupCentralized()`
Cleanup centralized logging resources.

Switches back to direct logging mode and releases IPC resources.

---

#### `Logger::setSimulationStartTime(time_t startTime)`
Set simulation start time for log timestamps.

Enables simulated time display in log messages (e.g., [08:15]).

- **Parameters**:
  - `startTime` - Real time when simulation started

---

#### `Logger::debug(Source source, const char* tag, const char* message, Args... args)`
Log a debug message.

Debug messages are for technical details, disabled by default in production.

- **Parameters**:
  - `source` - Source process identifier
  - `tag` - Short identifier (e.g., "Tourist 5")
  - `message` - Format string (printf-style)
  - `args` - Format arguments

---

#### `Logger::info(Source source, const char* tag, const char* message, Args... args)`
Log an info message.

Info messages are for business logic events.

- **Parameters**:
  - `source` - Source process identifier
  - `tag` - Short identifier
  - `message` - Format string (printf-style)
  - `args` - Format arguments

---

#### `Logger::warn(Source source, const char* tag, const char* message, Args... args)`
Log a warning message.

Warning messages indicate potential issues.

- **Parameters**:
  - `source` - Source process identifier
  - `tag` - Short identifier
  - `message` - Format string (printf-style)
  - `args` - Format arguments

---

#### `Logger::error(Source source, const char* tag, const char* message, Args... args)`
Log an error message.

Error messages indicate failures that need attention.

- **Parameters**:
  - `source` - Source process identifier
  - `tag` - Short identifier
  - `message` - Format string (printf-style)
  - `args` - Format arguments

---

#### `Logger::pError(const char* message)`
Print POSIX error using perror().

- **Parameters**:
  - `message` - Context message to prepend

---

#### `Logger::perror(Source source, const char* tag, const char* message)`
Log a POSIX error with errno description.

Appends `strerror(errno)` to the message.

- **Parameters**:
  - `source` - Source process identifier
  - `tag` - Short identifier
  - `message` - Context message

---

#### `Logger::stateChange(Source source, const char* tag, const char* from, const char* to)`
Log a state transition.

- **Parameters**:
  - `source` - Source process identifier
  - `tag` - Short identifier
  - `from` - Previous state name
  - `to` - New state name

---

#### `Logger::separator(char ch = '-', int count = 60)`
Print a visual separator line.

- **Parameters**:
  - `ch` - Character to use for the line
  - `count` - Number of characters in the line

---

### ipc/core/Semaphore.h

RAII wrapper for System V semaphore sets.

#### `Semaphore::Semaphore(key_t key)`
Construct semaphore set wrapper.

- **Parameters**:
  - `key` - System V IPC key for the semaphore set
- **Throws**: `ipc_exception` if semget fails

---

#### `Semaphore::initialize(uint8_t semIndex, int32_t value)`
Initialize a semaphore to a specific value.

- **Parameters**:
  - `semIndex` - Index of the semaphore in the set
  - `value` - Initial value to set

---

#### `Semaphore::wait(uint8_t semIndex, int32_t n, bool useUndo)`
Wait (decrement) on a semaphore.

Blocks until the semaphore value is >= n, then decrements by n. Handles EINTR for signal safety.

- **Parameters**:
  - `semIndex` - Index of the semaphore in the set
  - `n` - Amount to decrement (typically 1)
  - `useUndo` - If true, uses SEM_UNDO for automatic cleanup on process termination
- **Returns**: `true` if successful, `false` if interrupted by signal

---

#### `Semaphore::tryAcquire(uint8_t semIndex, int32_t n, bool useUndo)`
Try to acquire a semaphore without blocking.

- **Parameters**:
  - `semIndex` - Index of the semaphore in the set
  - `n` - Amount to decrement (typically 1)
  - `useUndo` - If true, uses SEM_UNDO for automatic cleanup
- **Returns**: `true` if acquired, `false` if would block

---

#### `Semaphore::post(uint8_t semIndex, int32_t n, bool useUndo)`
Post (increment) a semaphore.

- **Parameters**:
  - `semIndex` - Index of the semaphore in the set
  - `n` - Amount to increment (typically 1)
  - `useUndo` - If true, uses SEM_UNDO for automatic cleanup

---

#### `Semaphore::setValue(uint8_t semIndex, int32_t value)`
Set a semaphore to an absolute value.

- **Parameters**:
  - `semIndex` - Index of the semaphore in the set
  - `value` - Value to set

---

#### `Semaphore::getAvailableSpace(uint8_t semIndex)`
Get current value of a semaphore.

- **Parameters**:
  - `semIndex` - Index of the semaphore in the set
- **Returns**: Current semaphore value

---

#### `Semaphore::destroy()`
Destroy the semaphore set.

- **Throws**: `ipc_exception` if semctl IPC_RMID fails

---

#### `Semaphore::Index::toString(uint8_t index)`
Get human-readable name of a semaphore index.

- **Parameters**:
  - `index` - Semaphore index value
- **Returns**: String name of the semaphore

---

### ipc/core/SharedMemory.h

RAII wrapper for System V shared memory segments.

#### `SharedMemory<T>::create(key_t key)`
Create a new shared memory segment.

If segment already exists, it is removed and recreated. The caller becomes the owner responsible for cleanup.

- **Parameters**:
  - `key` - System V IPC key
- **Returns**: SharedMemory wrapper with ownership
- **Throws**: `ipc_exception` if creation fails

---

#### `SharedMemory<T>::attach(key_t key)`
Attach to an existing shared memory segment.

The caller is not responsible for cleanup (non-owner).

- **Parameters**:
  - `key` - System V IPC key
- **Returns**: SharedMemory wrapper without ownership
- **Throws**: `ipc_exception` if attachment fails

---

#### `SharedMemory<T>::exists(key_t key)`
Check if a shared memory segment exists.

- **Parameters**:
  - `key` - System V IPC key to check
- **Returns**: `true` if segment exists, `false` otherwise

---

#### `SharedMemory<T>::destroy()`
Destroy the shared memory segment.

- **Throws**: `ipc_exception` if destruction fails

---

### ipc/core/MessageQueue.h

RAII wrapper for System V message queues.

#### `MessageQueue<T>::MessageQueue(key_t key, const char* tag)`
Create or connect to a message queue.

Creates queue if it doesn't exist, otherwise connects to existing.

- **Parameters**:
  - `key` - System V IPC key
  - `tag` - Identifier for logging

---

#### `MessageQueue<T>::send(const T& message, long type)`
Send a message (blocking).

Handles EINTR for signal safety. Blocks if queue is full.

- **Parameters**:
  - `message` - Message payload to send
  - `type` - Message type for priority/filtering
- **Throws**: `ipc_exception` if sending fails

---

#### `MessageQueue<T>::trySend(const T& message, long type)`
Try to send a message (non-blocking).

- **Parameters**:
  - `message` - Message payload to send
  - `type` - Message type for priority/filtering
- **Returns**: `true` if sent successfully, `false` if queue is full

---

#### `MessageQueue<T>::receive(long type, int32_t flags = 0)`
Receive a message (blocking).

Caller should check exit signals when nullopt is returned.

- **Parameters**:
  - `type` - Message type to receive (0 = any, >0 = exact, <0 = priority)
  - `flags` - Additional msgrcv flags
- **Returns**: Message if received, `nullopt` on EINTR or error

---

#### `MessageQueue<T>::tryReceive(long type)`
Try to receive a message (non-blocking).

- **Parameters**:
  - `type` - Message type to receive
- **Returns**: Message if available, `nullopt` if queue is empty

---

#### `MessageQueue<T>::receiveInterruptible(long type)`
Receive a message, returning on signal interrupt.

Designed for signal-driven loops that need to check flags after interruption.

- **Parameters**:
  - `type` - Message type to receive
- **Returns**: Message if received, `nullopt` on EINTR

---

#### `MessageQueue<T>::destroy()`
Destroy the message queue.

- **Throws**: `ipc_exception` if destruction fails

---

### ipc/IpcManager.h

Central manager for all IPC resources.

#### `IpcManager::IpcManager()`
Create all IPC resources for the simulation.

Creates shared memory, semaphore set, and all message queues. Registers cleanup handler for automatic resource release.

- **Throws**: `ipc_exception` if any IPC creation fails

---

#### `IpcManager::initSemaphores(uint16_t stationCapacity)`
Initialize all semaphores to their starting values.

Must be called after construction and before starting simulation.

- **Parameters**:
  - `stationCapacity` - Maximum tourists allowed in lower station

---

#### `IpcManager::initState(time_t openTime, time_t closeTime)`
Initialize shared state with simulation timing.

- **Parameters**:
  - `openTime` - Real time when simulation starts
  - `closeTime` - Real time when simulation should end

---

#### `IpcManager::cleanup()`
Clean up all IPC resources.

Safe to call multiple times. Destroys shared memory, semaphores, and all message queues. Called automatically by destructor.

---

### ipc/model/SharedRopewayState.h

Main shared memory structure for the ropeway simulation.

#### `SharedRopewayState::registerTourist(uint32_t touristId, uint32_t ticketId, uint32_t age, TouristType type, bool isVip, int32_t guardianId = -1, uint32_t childCount = 0)`
Register a tourist for ride tracking.

Called when tourist purchases ticket to start tracking their rides.

- **Parameters**:
  - `touristId` - Unique tourist ID
  - `ticketId` - Assigned ticket ID
  - `age` - Tourist age
  - `type` - Tourist type (PEDESTRIAN or CYCLIST)
  - `isVip` - VIP status
  - `guardianId` - Guardian's tourist ID (-1 if none)
  - `childCount` - Number of children with this tourist
- **Returns**: Record index in touristRecords array, or -1 if array is full

---

#### `SharedRopewayState::setGuardianId(uint32_t touristId, int32_t guardianId)`
Set guardian ID for a tourist record.

Used to link children with their supervising adult.

- **Parameters**:
  - `touristId` - Tourist ID to update
  - `guardianId` - Guardian's tourist ID

---

#### `SharedRopewayState::findTouristRecord(uint32_t touristId)`
Find tourist record by ID.

- **Parameters**:
  - `touristId` - Tourist ID to search for
- **Returns**: Index in touristRecords array, or -1 if not found

---

#### `SharedRopewayState::logGatePassage(uint32_t touristId, uint32_t ticketId, GateType gateType, uint32_t gateNumber, bool allowed, uint32_t simTimeSeconds = 0)`
Log a gate passage and update tourist statistics.

Records the passage in gateLog and updates the tourist's passage counters.

- **Parameters**:
  - `touristId` - Tourist ID
  - `ticketId` - Ticket ID
  - `gateType` - Type of gate (ENTRY or RIDE)
  - `gateNumber` - Gate number
  - `allowed` - Whether passage was allowed
  - `simTimeSeconds` - Simulated time as seconds since midnight

---

#### `SharedRopewayState::recordRide(uint32_t touristId)`
Record a completed ride for a tourist.

Increments the ridesCompleted counter in the tourist's record.

- **Parameters**:
  - `touristId` - Tourist ID

---

### tourist/Tourist.h

Tourist structure and methods.

#### `Tourist::calculateSlots()`
Calculate and set the slots field based on type and children.

Sets the slots field to the total chair capacity needed:
- Base: 1 slot for the tourist
- Cyclist with bike: 2 slots (1 person + 1 bike)
- Plus 1 slot per child

---

#### `Tourist::isTicketValid(time_t totalPausedSeconds = 0)`
Check if ticket is still valid.

Single-use tickets are invalid after first ride. Time-based tickets are invalid after validUntil time.

- **Parameters**:
  - `totalPausedSeconds` - Cumulative seconds the simulation was paused (Ctrl+Z)
- **Returns**: `true` if ticket is valid, `false` otherwise

---

#### `Tourist::canRideAgain()`
Check if ticket allows multiple rides.

- **Returns**: `true` if ticket is not single-use, `false` otherwise

---

#### `Tourist::isAdult()`
Check if tourist is an adult.

Adults can supervise children under 8 years old.

- **Returns**: `true` if age >= 18, `false` otherwise

---

#### `Tourist::hasGroup()`
Check if this tourist has a group.

- **Returns**: `true` if tourist has children or a bike, `false` otherwise

---

### tourist/TouristState.h

#### `toString(TouristState state)`
Convert TouristState enum to string representation.

- **Parameters**:
  - `state` - TouristState to convert
- **Returns**: String name of the state

---

### tourist/TouristType.h

#### `toString(TouristType type)`
Convert TouristType enum to string representation.

- **Parameters**:
  - `type` - TouristType to convert
- **Returns**: "PEDESTRIAN" or "CYCLIST"

---

### ropeway/TrailDifficulty.h

#### `toString(TrailDifficulty trail)`
Convert TrailDifficulty enum to string representation.

- **Parameters**:
  - `trail` - TrailDifficulty to convert
- **Returns**: "EASY", "MEDIUM", or "HARD"

---

#### `toTrailCode(TrailDifficulty trail)`
Get trail code for compact output.

- **Parameters**:
  - `trail` - TrailDifficulty to convert
- **Returns**: "T1", "T2", or "T3"

---

#### `getDurationUs(TrailDifficulty difficulty)`
Get trail duration in microseconds.

- **Parameters**:
  - `difficulty` - Trail difficulty level
- **Returns**: Duration in microseconds from configuration
- **Throws**: `std::invalid_argument` if difficulty is invalid

---

### ropeway/gate/GateType.h

#### `toString(GateType type)`
Convert GateType enum to string representation.

- **Parameters**:
  - `type` - GateType to convert
- **Returns**: "ENTRY" or "RIDE"

---

### ropeway/worker/WorkerSignal.h

#### `toString(WorkerSignal signal)`
Convert WorkerSignal enum to string representation.

- **Parameters**:
  - `signal` - WorkerSignal to convert
- **Returns**: String name of the signal

---

### entrance/TicketName.h

#### `toString(TicketType type)`
Convert TicketType enum to string representation.

- **Parameters**:
  - `type` - TicketType to convert
- **Returns**: "SINGLE_USE", "TIME_TK1", "TIME_TK2", "TIME_TK3", or "DAILY"

---

### entrance/CashierMessage.h

#### `TicketPricing::getPrice(TicketType type)`
Get base price for a ticket type.

- **Parameters**:
  - `type` - Type of ticket
- **Returns**: Price in PLN

---

#### `TicketPricing::getDuration(TicketType type)`
Get validity duration for a ticket type.

- **Parameters**:
  - `type` - Type of ticket
- **Returns**: Duration in seconds (0 for single-use and daily tickets)

---

### ropeway/chair/Chair.h

#### `Chair::hasEnoughSpace(uint32_t slotsNeeded)`
Check if chair has enough space for additional passengers.

- **Parameters**:
  - `slotsNeeded` - Number of slots required
- **Returns**: `true` if slotsUsed + slotsNeeded <= 4, `false` otherwise

---

### ropeway/chair/BoardingQueue.h

#### `BoardingQueue::findTourist(uint32_t touristId)`
Find tourist in queue by ID.

- **Parameters**:
  - `touristId` - Tourist ID to search for
- **Returns**: Index in entries array, or -1 if not found

---

#### `BoardingQueue::addTourist(const BoardingQueueEntry& entry)`
Add tourist to the boarding queue.

- **Parameters**:
  - `entry` - Boarding queue entry to add
- **Returns**: `true` if added successfully, `false` if queue is full (MAX_SIZE=64)

---

#### `BoardingQueue::removeTourist(uint32_t index)`
Remove tourist at specified index.

Shifts all subsequent entries to fill the gap. Does nothing if index is out of bounds.

- **Parameters**:
  - `index` - Index in entries array

---

### stats/DailyStatistic.h

#### `DailyStatistics::recordEmergencyStart(uint32_t workerId)`
Record the start of an emergency stop.

Creates a new EmergencyStopRecord with the current time as startTime.

- **Parameters**:
  - `workerId` - ID of the worker who initiated the stop (1 or 2)
- **Returns**: Index of the new emergency record, or -1 if record array is full

---

#### `DailyStatistics::recordEmergencyEnd(int32_t recordIndex)`
Record the end of an emergency stop (resume).

Updates the record with endTime and adds duration to totalEmergencyDuration. Does nothing if recordIndex is invalid.

- **Parameters**:
  - `recordIndex` - Index returned by recordEmergencyStart

---

### stats/GatePassage.h

#### `GatePassage::formatSimTime(char* buffer)`
Format simulated time as HH:MM string.

Converts simTimeSeconds to hours and minutes format.

- **Parameters**:
  - `buffer` - Output buffer (must be at least 6 bytes)

---

### stats/GatePassageLog.h

#### `GatePassageLog::addEntry(const GatePassage& entry)`
Add a gate passage entry to the log.

- **Parameters**:
  - `entry` - Gate passage record to add
- **Returns**: `true` if added successfully, `false` if log is full (MAX_ENTRIES=200)

---

### logging/GatePassageLogger.h

#### `GatePassageLogger::GatePassageLogger(const std::string& logFilePath = "")`
Construct a gate passage logger.

- **Parameters**:
  - `logFilePath` - Optional path for file logging (empty = no file)

---

#### `GatePassageLogger::log(GatePassageLog* logMem, const GatePassage& passage)`
Log a gate passage.

Writes to both shared memory and file if configured.

- **Parameters**:
  - `logMem` - Pointer to shared memory log (can be nullptr)
  - `passage` - Gate passage record to log

---

#### `GatePassageLogger::logEntry(GatePassageLog* logMem, uint32_t touristId, uint32_t ticketId, uint32_t gateNumber, bool wasAllowed)`
Log an entry gate passage.

- **Parameters**:
  - `logMem` - Pointer to shared memory log
  - `touristId` - Tourist ID
  - `ticketId` - Ticket ID
  - `gateNumber` - Gate number (0-3)
  - `wasAllowed` - Whether passage was allowed

---

#### `GatePassageLogger::logRide(GatePassageLog* logMem, uint32_t touristId, uint32_t ticketId, uint32_t gateNumber, bool wasAllowed)`
Log a ride gate passage.

- **Parameters**:
  - `logMem` - Pointer to shared memory log
  - `touristId` - Tourist ID
  - `ticketId` - Ticket ID
  - `gateNumber` - Gate number (0-2)
  - `wasAllowed` - Whether passage was allowed

---

#### `GatePassageLogger::getStats(const GatePassageLog* logMem)`
Calculate statistics from gate passage log.

- **Parameters**:
  - `logMem` - Pointer to shared memory log
- **Returns**: LogStats structure with passage counts
