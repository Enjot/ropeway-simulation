# Processes

This document describes each process in the Ropeway Simulation and their responsibilities.

## Process Overview

| Process | File | Role |
|---------|------|------|
| Orchestrator | `processes/Orchestrator.hpp` | Parent process, manages lifecycle |
| Cashier | `processes/cashier_process.cpp` | Sells tickets to tourists |
| Worker1 | `processes/worker1_process.cpp` | Controls lower station (boarding) |
| Worker2 | `processes/worker2_process.cpp` | Controls upper station (exits) |
| Tourist | `processes/tourist_process.cpp` | Individual tourist behavior |

---

## Orchestrator

**Role**: Main coordinator that creates IPC resources, spawns all other processes, monitors the simulation, and handles cleanup.

### Lifecycle

```
main()
  │
  ├─► Orchestrator::Orchestrator()
  │     ├─ Setup signal handlers (ORCHESTRATOR mode)
  │     └─ Cleanup any leftover IPC resources
  │
  ├─► Orchestrator::run()
  │     ├─ initializeIpc()
  │     │    ├─ Create shared memory
  │     │    ├─ Create semaphore set
  │     │    ├─ Create message queues
  │     │    └─ Initialize semaphore values
  │     │
  │     ├─ spawnProcesses()
  │     │    ├─ fork/exec → Cashier
  │     │    │    └─ wait(CASHIER_READY)
  │     │    ├─ fork/exec → Worker1
  │     │    ├─ fork/exec → Worker2
  │     │    │    ├─ wait(LOWER_WORKER_READY)
  │     │    │    └─ wait(UPPER_WORKER_READY)
  │     │
  │     ├─ spawnTourists()
  │     │    └─ Loop: fork/exec → Tourist (with random delay)
  │     │
  │     ├─ runSimulationLoop()
  │     │    └─ Monitor state, trigger emergency stop/resume
  │     │
  │     └─ generateReport()
  │          └─ Write daily statistics to file
  │
  └─► Orchestrator::~Orchestrator()
        └─ cleanup()
             ├─ Send SIGTERM to all children
             ├─ Wait for children to exit
             └─ Remove IPC resources
```

### Key Responsibilities

1. **IPC Creation**: Creates and owns all IPC resources
2. **Process Spawning**: Uses `fork()` + `exec()` to create child processes
3. **Simulation Control**: Triggers emergency stops and resumes
4. **Reporting**: Generates daily statistics report
5. **Cleanup**: Ensures all resources are properly released

### Code Highlights

```cpp
// Spawning a child process
void spawnProcesses() {
    cashierPid_ = ProcessSpawner::spawnWithKeys("cashier_process",
        ipc_->shmKey(), ipc_->semKey(), ipc_->cashierMsgKey());
    
    // Wait for cashier to signal it's ready
    ipc_->semaphores().wait(Semaphore::Index::CASHIER_READY);
}

// Triggering emergency stop
if (elapsed >= 8 && !emergencyTriggered) {
    kill(worker1Pid_, SIGUSR1);  // Send SIGUSR1 to Worker1
    emergencyTriggered = true;
}
```

---

## Cashier

**Role**: Handles ticket sales for tourists. Waits for ticket requests via message queue and sends responses.

### State Machine

```
┌─────────────────────────────────────────────┐
│                  RUNNING                     │
│                                              │
│  ┌────────────────────────────────────────┐ │
│  │ Loop:                                  │ │
│  │   1. Wait for TicketRequest (msgrcv)   │ │
│  │   2. Check if accepting tourists       │ │
│  │   3. Calculate price with discounts    │ │
│  │   4. Determine VIP status              │ │
│  │   5. Send TicketResponse (msgsnd)      │ │
│  │   6. Update statistics                 │ │
│  └────────────────────────────────────────┘ │
│                                              │
│  Exit when: SIGTERM received                 │
└─────────────────────────────────────────────┘
           │
           ▼
    ┌─────────────┐
    │  SHUTDOWN   │
    │             │
    │ Print stats │
    │ Exit        │
    └─────────────┘
```

### Message Protocol

```
Tourist ──────► [Message Queue] ──────► Cashier
         TicketRequest                    │
         mtype = 1                        │
                                          │
Tourist ◄────── [Message Queue] ◄──────┘
         TicketResponse
         mtype = 1000 + touristId
```

### Pricing Logic

```cpp
double basePrice = TicketPricing::getPrice(request.requestedType);
double discount = 0.0;

// Child discount (under 10)
if (request.touristAge < 10) {
    discount = 0.25;  // 25% off
}
// Senior discount (65+)
else if (request.touristAge >= 65) {
    discount = 0.25;  // 25% off
}

double finalPrice = basePrice * (1.0 - discount);
```

### Key Points

- **Blocking receive**: Waits for requests using `msgrcv()` with no `IPC_NOWAIT`
- **Signal handling**: SIGTERM causes `msgrcv()` to return with `EINTR`
- **Response routing**: Uses `mtype = RESPONSE_BASE + touristId` for routing

---

## Worker1 (Lower Station)

**Role**: Controls the lower station. Manages entry gates, boarding queue, and chair assignments. Handles emergency stop initiation.

### State-Based Behavior

```cpp
switch (currentState) {
    case RopewayState::RUNNING:
        handleRunningState();     // Process boarding queue
        sem_.wait(BOARDING_QUEUE_WORK);  // Wait for work
        break;
        
    case RopewayState::EMERGENCY_STOP:
        handleEmergencyState();   // Wait for resume
        pause();                  // Sleep until signal
        break;
        
    case RopewayState::CLOSING:
        handleClosingState();     // Drain remaining tourists
        sem_.wait(BOARDING_QUEUE_WORK);
        break;
        
    case RopewayState::STOPPED:
        handleStoppedState();     // Cleanup and wait for exit
        break;
}
```

### Boarding Algorithm

```cpp
void processQueue() {
    // 1. Get next tourist(s) from queue (VIP first)
    auto group = getNextBoardingGroup();
    
    // 2. Find available chair
    int chairIdx = findAvailableChair();
    
    // 3. Validate seating rules:
    //    - Max 4 pedestrians
    //    - Max 2 cyclists
    //    - Max 1 cyclist + 2 pedestrians
    if (!validateSeating(group, chairIdx)) {
        return;  // Can't board yet
    }
    
    // 4. Assign tourists to chair
    assignToChair(group, chairIdx);
    
    // 5. Signal each tourist that chair is assigned
    for (auto& tourist : group) {
        sem_.signal(CHAIR_ASSIGNED_BASE + tourist.id);
    }
}
```

### Emergency Stop Handling

```cpp
void handleEmergencyStopTrigger() {
    // 1. Set state in shared memory
    {
        Semaphore::ScopedLock lock(sem_, SHARED_MEMORY);
        shm_->core.state = RopewayState::EMERGENCY_STOP;
    }
    
    // 2. Notify Worker2 via message queue
    sendMessage(WorkerSignal::EMERGENCY_STOP, "Emergency stop initiated");
    
    // 3. Get Worker2's PID and send SIGUSR1
    pid_t worker2Pid = shm_->core.worker2Pid;
    kill(worker2Pid, SIGUSR1);
}
```

---

## Worker2 (Upper Station)

**Role**: Controls the upper station. Manages exit routes and coordinates with Worker1 for emergency handling.

### Main Loop

```cpp
while (!SignalHelper::shouldExit(g_signals)) {
    // Check for emergency signal
    if (SignalHelper::isEmergency(g_signals)) {
        handleEmergencyReceived();
        SignalHelper::clearFlag(g_signals.emergency);
    }
    
    // Check for messages from Worker1 (non-blocking)
    auto msg = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER1);
    if (msg) {
        handleMessage(*msg);
    }
    
    // State-based behavior
    switch (currentState) {
        case RopewayState::RUNNING:
            handleRunningState();
            waitForMessage();  // Blocking wait for Worker1 messages
            break;
        // ... other states
    }
}
```

### Worker Communication Protocol

```
Worker1 ◄───────────► Worker2
        WorkerMessage
        
Signals:
  EMERGENCY_STOP    - Stop the ropeway immediately
  READY_TO_START    - Ready to resume operations
  STATION_CLEAR     - Station is clear of tourists
  DANGER_DETECTED   - Potential danger detected
```

---

## Tourist

**Role**: Simulates an individual tourist. Each tourist is a separate process with its own state machine.

### State Machine

```cpp
enum class TouristState {
    BUYING_TICKET,      // At cashier
    WAITING_ENTRY,      // Waiting for entry gate
    WAITING_BOARDING,   // On platform, waiting for chair
    ON_CHAIR,           // Riding up
    AT_TOP,             // Arrived at top station
    ON_TRAIL,           // Cyclist descending trail
    FINISHED            // Done for the day
};
```

### Full Lifecycle

```
Start
  │
  ├─► BUYING_TICKET
  │     ├─ Send TicketRequest to Cashier
  │     ├─ Wait for TicketResponse
  │     └─ If !wantsToRide → FINISHED
  │
  ├─► WAITING_ENTRY
  │     ├─ Wait for entry gate semaphore
  │     ├─ Wait for station capacity semaphore
  │     └─ Enter station area
  │
  ├─► WAITING_BOARDING
  │     ├─ Join boarding queue (VIP or regular)
  │     ├─ Signal BOARDING_QUEUE_WORK
  │     └─ Wait for CHAIR_ASSIGNED semaphore
  │
  ├─► ON_CHAIR
  │     ├─ Simulate ride time
  │     └─ Release station capacity semaphore
  │
  ├─► AT_TOP
  │     ├─ If CYCLIST: choose trail → ON_TRAIL
  │     └─ If PEDESTRIAN or done: → FINISHED
  │
  ├─► ON_TRAIL (cyclists only)
  │     ├─ Simulate descent time based on difficulty
  │     └─ If ticket still valid: → WAITING_ENTRY (another ride)
  │
  └─► FINISHED
        └─ Update statistics, exit
```

### Entry Gate Logic

```cpp
void enterThroughGate() {
    // Try each gate in order, VIPs get priority
    for (int gate = 0; gate < NUM_ENTRY_GATES; ++gate) {
        if (sem_.tryWait(ENTRY_GATE_0 + gate)) {
            // Got the gate!
            Logger::info(TAG, "Entered through entry gate ", gate);
            
            // Pass through (gate opens briefly)
            usleep(GATE_PASSAGE_TIME);
            
            // Release gate for next tourist
            sem_.signal(ENTRY_GATE_0 + gate);
            return;
        }
    }
    
    // All gates busy, wait on first gate
    sem_.wait(ENTRY_GATE_0);
    // ... pass through ...
    sem_.signal(ENTRY_GATE_0);
}
```

### Trail Descent (Cyclists)

```cpp
void descendTrail() {
    // Choose trail based on difficulty
    int trailTime;
    switch (trail_) {
        case TrailDifficulty::EASY:
            trailTime = Config::Trail::EASY_TIME_MS;
            break;
        case TrailDifficulty::MEDIUM:
            trailTime = Config::Trail::MEDIUM_TIME_MS;
            break;
        case TrailDifficulty::HARD:
            trailTime = Config::Trail::HARD_TIME_MS;
            break;
    }
    
    // Simulate descent
    usleep(trailTime * 1000);
    
    // Check if ticket still valid for another ride
    time_t now = time(nullptr);
    if (now < ticketValidUntil_) {
        state_ = TouristState::WAITING_ENTRY;  // Go again!
    } else {
        state_ = TouristState::FINISHED;
    }
}
```

---

## Process Spawning

### Using fork() and exec()

```cpp
// In ProcessSpawner.hpp
pid_t spawn(const std::string& executable, 
            const std::vector<std::string>& args) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(executable.c_str(), argv.data());
        
        // If execvp returns, it failed
        perror("execvp");
        _exit(1);
    }
    
    return pid;  // Parent returns child's PID
}
```

### Passing IPC Keys to Children

```cpp
// Orchestrator passes keys as command-line arguments
pid_t cashierPid = ProcessSpawner::spawnWithKeys(
    "cashier_process",
    ipc_->shmKey(),        // Shared memory key
    ipc_->semKey(),        // Semaphore key
    ipc_->cashierMsgKey()  // Message queue key
);

// Cashier parses arguments
int main(int argc, char* argv[]) {
    ArgumentParser::CashierArgs args;
    ArgumentParser::parseCashierArgs(argc, argv, args);
    
    // Attach to existing IPC resources (create = false)
    SharedMemory<RopewaySystemState> shm(args.shmKey, false);
    Semaphore sem(args.semKey, TOTAL_SEMAPHORES, false);
    MessageQueue<TicketRequest> queue(args.cashierMsgKey, false);
}
```

---

## Process Termination

### Graceful Shutdown

1. Orchestrator sends `SIGTERM` to each child
2. Child's signal handler sets `shouldExit` flag
3. Blocking operations (`msgrcv`, `semop`) return with `EINTR`
4. Main loop checks `shouldExit()` and exits
5. RAII destructors clean up resources

```cpp
// In SignalHelper.hpp
void signalHandler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        g_flags->shouldExit = 1;
    }
}

// In process main loop
while (!SignalHelper::shouldExit(g_signals)) {
    auto msg = queue.receive();  // Returns nullopt on EINTR
    if (!msg) continue;          // Check shouldExit on next iteration
    // ... process message
}
```

### Timeout-Based Termination

```cpp
// ProcessSpawner::terminate()
void terminate(pid_t pid, const char* name) {
    kill(pid, SIGTERM);  // Request graceful shutdown
    
    // Poll with timeout
    for (int i = 0; i < 50; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || result == -1) return;
        usleep(100000);  // 100ms
    }
    
    // Force kill if not responding
    kill(pid, SIGKILL);
}
```

---

## See Also

- [Architecture](ARCHITECTURE.md) - System overview
- [Signals](SIGNALS.md) - Signal handling details
- [IPC Mechanisms](IPC_MECHANISMS.md) - IPC details
