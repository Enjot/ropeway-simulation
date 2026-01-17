# Signal Handling

This document explains how signals are used in the Ropeway Simulation for process coordination and graceful shutdown.

## Signals Overview

| Signal | Number | Default Action | Our Usage |
|--------|--------|----------------|-----------|
| SIGTERM | 15 | Terminate | Graceful shutdown request |
| SIGINT | 2 | Terminate | Ctrl+C handling |
| SIGUSR1 | 30/10 | Terminate | Emergency stop trigger |
| SIGUSR2 | 31/12 | Terminate | Resume operation |
| SIGCHLD | 20/17 | Ignore | Child process status change |

## Signal Flow Diagram

```
                    ORCHESTRATOR
                         │
         ┌───────────────┼───────────────┐
         │               │               │
    SIGTERM         SIGUSR1/2       SIGTERM
         │               │               │
         ▼               ▼               ▼
    ┌────────┐      ┌────────┐      ┌────────┐
    │CASHIER │      │WORKER1 │◄────►│WORKER2 │
    └────────┘      └───┬────┘      └───┬────┘
                        │               │
                   SIGUSR1          SIGUSR1
                   (broadcast)      (broadcast)
                        │               │
                        ▼               ▼
                   ┌─────────────────────────┐
                   │       TOURISTS          │
                   │  (emergency awareness)  │
                   └─────────────────────────┘
```

## SignalHelper Utility

### Signal Flags Structure

```cpp
// utils/SignalHelper.hpp

struct SignalFlags {
    volatile sig_atomic_t emergency{0};   // SIGUSR1 received
    volatile sig_atomic_t resume{0};      // SIGUSR2 received
    volatile sig_atomic_t shouldExit{0};  // SIGTERM/SIGINT received
};
```

**Why `volatile sig_atomic_t`?**
- `volatile`: Prevents compiler optimizations that might cache the value
- `sig_atomic_t`: Guarantees atomic read/write operations
- Together: Safe to access from both signal handler and main code

### Signal Handler

```cpp
void signalHandler(int signum) {
    if (!g_flags) return;
    
    switch (signum) {
        case SIGUSR1:
            g_flags->emergency = 1;
            break;
        case SIGUSR2:
            g_flags->resume = 1;
            break;
        case SIGTERM:
        case SIGINT:
            g_flags->shouldExit = 1;
            break;
    }
}
```

**Signal Handler Rules:**
1. Must be async-signal-safe (only use safe functions)
2. Should be as short as possible
3. Should only set flags, not do complex work
4. Must not allocate memory or use I/O functions like `printf`

### Signal Modes

```cpp
enum class Mode {
    BASIC,          // SIGTERM, SIGINT only (Cashier)
    TOURIST,        // + SIGUSR1 (emergency awareness)
    WORKER,         // + SIGUSR1, SIGUSR2 (emergency handling)
    ORCHESTRATOR    // + SIGCHLD ignored (prevent zombies)
};
```

### Registration with sigaction

```cpp
bool registerSignal(int signum) {
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);    // Don't block other signals
    sa.sa_flags = 0;             // No special flags
    
    if (sigaction(signum, &sa, nullptr) == -1) {
        perror("sigaction");
        return false;
    }
    return true;
}
```

**Why `sigaction` over `signal`?**
- More portable behavior across Unix systems
- Can specify signal mask during handler execution
- Can control SA_RESTART behavior
- Handler doesn't get reset after first signal

---

## Emergency Stop Sequence

### Trigger (by Orchestrator or Worker)

```
1. Orchestrator or Worker1 detects emergency condition
   
2. If Orchestrator triggers:
   kill(worker1Pid, SIGUSR1);
   
3. Worker1 receives SIGUSR1:
   - Sets g_signals.emergency = 1 in signal handler
   - Main loop checks isEmergency() → true
   - Calls handleEmergencyStopTrigger()
```

### Worker1 Emergency Handler

```cpp
void handleEmergencyStopTrigger() {
    // 1. Update shared state
    {
        Semaphore::ScopedLock lock(sem_, SHARED_MEMORY);
        shm_->core.state = RopewayState::EMERGENCY_STOP;
        shm_->stats.dailyStats.recordEmergencyStart(WORKER_ID);
    }
    
    // 2. Notify Worker2 via message queue
    sendMessage(WorkerSignal::EMERGENCY_STOP, 
                "Emergency stop initiated by Worker1");
    
    // 3. Signal Worker2 to check messages
    pid_t worker2Pid;
    {
        Semaphore::ScopedLock lock(sem_, SHARED_MEMORY);
        worker2Pid = shm_->core.worker2Pid;
    }
    if (worker2Pid > 0) {
        kill(worker2Pid, SIGUSR1);
    }
    
    isEmergencyStopped_ = true;
}
```

### Worker2 Emergency Handler

```cpp
void handleEmergencyReceived() {
    // 1. Update shared state
    {
        Semaphore::ScopedLock lock(sem_, SHARED_MEMORY);
        shm_->core.state = RopewayState::EMERGENCY_STOP;
    }
    
    // 2. Acknowledge to Worker1
    sendMessage(WorkerSignal::EMERGENCY_STOP, 
                "Emergency stop acknowledged by Worker2");
    
    isEmergencyStopped_ = true;
}
```

---

## Resume Sequence

### Trigger (by Orchestrator)

```
1. Orchestrator decides to resume:
   kill(worker1Pid, SIGUSR2);
   
2. Worker1 receives SIGUSR2:
   - Sets g_signals.resume = 1
   - Main loop checks isResumeRequested() → true
   - Calls handleResumeRequest()
```

### Worker Coordination

```cpp
// Worker1: Request resume
void handleResumeRequest() {
    // 1. Clear pending messages
    while (auto old = msgQueue_.tryReceive(MSG_TYPE_FROM_WORKER2)) {
        // Drain old messages
    }
    
    // 2. Send ready-to-start to Worker2
    sendMessage(WorkerSignal::READY_TO_START, 
                "Worker1 ready to resume");
    
    // 3. Wait for confirmation
    auto response = msgQueue_.receive(MSG_TYPE_FROM_WORKER2);
    
    if (response && response->signal == WorkerSignal::READY_TO_START) {
        // 4. Resume operations
        {
            Semaphore::ScopedLock lock(sem_, SHARED_MEMORY);
            shm_->core.state = RopewayState::RUNNING;
        }
        
        // 5. Wake up Worker2
        kill(shm_->core.worker2Pid, SIGUSR2);
        
        isEmergencyStopped_ = false;
    }
}

// Worker2: Confirm ready
void handleMessage(const WorkerMessage& msg) {
    if (msg.signal == WorkerSignal::READY_TO_START) {
        if (isStationClear()) {
            sendMessage(WorkerSignal::READY_TO_START, 
                        "Worker2 confirms ready");
            isEmergencyStopped_ = false;
        } else {
            sendMessage(WorkerSignal::DANGER_DETECTED, 
                        "Station not clear");
        }
    }
}
```

---

## Graceful Shutdown

### Orchestrator Cleanup

```cpp
void cleanup() {
    // 1. Terminate each child with timeout
    ProcessSpawner::terminate(cashierPid_, "Cashier");
    ProcessSpawner::terminate(worker1Pid_, "Worker1");
    ProcessSpawner::terminate(worker2Pid_, "Worker2");
    
    // 2. Terminate all tourists (batch)
    ProcessSpawner::terminateAll(touristPids_);
    
    // 3. Wait for all children
    while (waitpid(-1, &status, 0) > 0) {
        // Reap children
    }
    
    // 4. Clean up IPC
    ipc_.reset();
}
```

### Child Process Shutdown

```cpp
// In Cashier main loop
while (!SignalHelper::shouldExit(g_signals)) {
    auto request = requestQueue_.receive(CashierMsgType::REQUEST);
    
    if (!request) {
        // Interrupted by signal (EINTR)
        continue;  // Loop will check shouldExit()
    }
    
    processRequest(*request);
}

// After loop exits
printStatistics();
Logger::info(TAG, "Shutting down");
// Destructors clean up
```

### Handling EINTR

When a blocking system call is interrupted by a signal:

```cpp
// In MessageQueue::receive()
ssize_t result = msgrcv(msgId_, &message, size, msgType, flags);

if (result == -1) {
    if (errno == EINTR) {
        // Signal interrupted us - return to let caller check signals
        return std::nullopt;
    }
    // Handle other errors
}

// In Semaphore::operate()
if (semop(semId_, &operation, 1) == -1) {
    if (errno == EINTR) {
        // Signal interrupted - return false
        return false;
    }
    // Handle other errors
}
```

---

## SIGCHLD Handling

### Problem: Zombie Processes

When a child process exits, it becomes a "zombie" until the parent calls `wait()`. With many tourists, this could create thousands of zombies.

### Solution: Ignore SIGCHLD

```cpp
// In Orchestrator setup
case Mode::ORCHESTRATOR:
    signal(SIGCHLD, SIG_IGN);  // Kernel auto-reaps children
    break;
```

**Effects of SIG_IGN for SIGCHLD:**
1. Child processes are automatically reaped
2. `wait()` family functions behave differently
3. No zombie processes accumulate

### Caveat: waitpid Behavior

When SIGCHLD is ignored and child already exited:
- `waitpid(pid, &status, 0)` may block forever on some systems
- Solution: Use `WNOHANG` and poll

```cpp
void terminate(pid_t pid) {
    kill(pid, SIGTERM);
    
    // Poll instead of blocking wait
    for (int i = 0; i < 50; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || result == -1) return;
        usleep(100000);
    }
    
    kill(pid, SIGKILL);  // Force kill
}
```

---

## Common Patterns

### Pattern 1: Wait for Signal

```cpp
// Worker in EMERGENCY_STOP state
case RopewayState::EMERGENCY_STOP:
    handleEmergencyState();
    pause();  // Sleep until ANY signal arrives
    break;
```

### Pattern 2: Signal-Aware Loop

```cpp
while (!SignalHelper::shouldExit(g_signals)) {
    // Check for signal flags
    if (SignalHelper::isEmergency(g_signals)) {
        handleEmergency();
        SignalHelper::clearFlag(g_signals.emergency);
    }
    
    // Do work that can be interrupted
    auto result = blockingOperation();
    if (!result) continue;  // Probably EINTR, check flags
    
    process(result);
}
```

### Pattern 3: Clear Flag After Handling

```cpp
if (SignalHelper::isEmergency(g_signals)) {
    handleEmergencyStopTrigger();
    SignalHelper::clearFlag(g_signals.emergency);  // Reset flag
}
```

---

## Debugging Signals

### Send Signals Manually

```bash
# Send SIGTERM to process
kill -TERM <pid>
# or
kill -15 <pid>

# Send SIGUSR1 (emergency)
kill -USR1 <pid>
# or (Linux)
kill -10 <pid>
# or (macOS)
kill -30 <pid>

# Send SIGUSR2 (resume)
kill -USR2 <pid>
```

### Check Signal Masks

```bash
# On Linux
cat /proc/<pid>/status | grep -i sig

# On macOS
ps -o pid,stat,sigmask -p <pid>
```

### Trace Signals

```bash
# Linux
strace -e signal -p <pid>

# macOS
dtrace -n 'proc:::signal-send { printf("%d -> %d: %d", pid, args[1]->pr_pid, args[2]); }'
```

---

## Best Practices

### 1. Keep Handlers Simple

```cpp
// ❌ BAD: Complex work in handler
void badHandler(int sig) {
    printf("Got signal\n");  // Not async-signal-safe!
    cleanupResources();      // Too complex
}

// ✅ GOOD: Just set a flag
void goodHandler(int sig) {
    g_flags->emergency = 1;
}
```

### 2. Use sigaction, Not signal

```cpp
// ❌ BAD: signal() has portability issues
signal(SIGTERM, handler);

// ✅ GOOD: sigaction() is portable and configurable
struct sigaction sa{};
sa.sa_handler = handler;
sigaction(SIGTERM, &sa, nullptr);
```

### 3. Handle EINTR

```cpp
// ❌ BAD: Ignores interruption
result = msgrcv(msgid, &msg, size, type, 0);

// ✅ GOOD: Returns to allow signal check
result = msgrcv(msgid, &msg, size, type, 0);
if (result == -1 && errno == EINTR) {
    return std::nullopt;  // Let caller check signals
}
```

### 4. Clear Flags After Handling

```cpp
// ❌ BAD: Flag stays set forever
if (g_signals.emergency) {
    handleEmergency();
    // Flag still set!
}

// ✅ GOOD: Reset flag
if (g_signals.emergency) {
    handleEmergency();
    g_signals.emergency = 0;
}
```

---

## See Also

- `man 7 signal` - Signal overview
- `man 2 sigaction` - sigaction() system call
- `man 2 kill` - Sending signals
- `man 7 signal-safety` - Async-signal-safe functions
- [Processes](PROCESSES.md) - Process details
- [Architecture](ARCHITECTURE.md) - System overview
