# Glossary

Key terms and concepts used in the Ropeway Simulation project.

---

## IPC (Inter-Process Communication)

### System V IPC
A set of IPC mechanisms (shared memory, semaphores, message queues) defined by AT&T in System V Unix. Identified by numeric keys and accessible via system calls like `shmget`, `semget`, `msgget`.

### POSIX IPC
An alternative IPC API defined by POSIX standards. Uses named objects (like `/my_shm`) instead of numeric keys. Not used in this project per requirements.

### Shared Memory
A memory segment that multiple processes can map into their address space. The fastest IPC mechanism because no data copying is needed.

### Semaphore
A synchronization primitive that maintains a count. Used for:
- **Mutex** (binary semaphore): Mutual exclusion
- **Counting semaphore**: Resource counting
- **Signaling**: Event notification

### Message Queue
A kernel-managed queue for sending discrete messages between processes. Messages have a type field for selective receiving.

---

## Semaphore Operations

### P Operation (Wait/Decrement)
Also called `wait()` or `down()`. Decrements the semaphore. Blocks if the value would go negative.

### V Operation (Signal/Increment)
Also called `signal()` or `up()`. Increments the semaphore. May wake up a waiting process.

### SEM_UNDO
A flag that tells the kernel to reverse semaphore operations if the process exits unexpectedly. Prevents deadlocks from crashed processes.

---

## Process Concepts

### fork()
System call that creates a new process by duplicating the calling process. Returns 0 in the child, child's PID in the parent.

### exec()
Family of system calls (`execl`, `execv`, `execvp`, etc.) that replace the current process image with a new program.

### Zombie Process
A process that has terminated but whose exit status hasn't been collected by its parent via `wait()`.

### Orphan Process
A process whose parent has terminated. Gets adopted by init (PID 1).

### Process Group
A collection of processes that can receive signals together. Created by `setpgid()`.

---

## Signals

### SIGTERM (15)
Termination signal. Requests graceful shutdown. Can be caught and handled.

### SIGKILL (9)
Kill signal. Forces immediate termination. Cannot be caught or ignored.

### SIGUSR1/SIGUSR2 (10/12 or 30/31)
User-defined signals. Available for application-specific purposes. In this project: emergency stop and resume.

### SIGCHLD (17 or 20)
Sent to parent when child process terminates or stops. Can be ignored to auto-reap children.

### Signal Handler
A function that runs when a signal is received. Must be async-signal-safe.

### Async-Signal-Safe
Functions that can be safely called from a signal handler. Examples: `write()`, `_exit()`. Not safe: `printf()`, `malloc()`.

---

## Synchronization Concepts

### Race Condition
A bug where program behavior depends on the relative timing of events, particularly in concurrent access to shared data.

### Critical Section
A code section that accesses shared resources and must not be executed by more than one process at a time.

### Mutex (Mutual Exclusion)
A synchronization mechanism ensuring only one process can access a resource at a time.

### Deadlock
A situation where two or more processes are waiting for each other indefinitely.
```
Process A holds Lock1, waits for Lock2
Process B holds Lock2, waits for Lock1
→ Neither can proceed
```

### Starvation
A situation where a process never gets access to a resource because others always get priority.

---

## RAII (Resource Acquisition Is Initialization)

A C++ idiom where resources are:
- **Acquired** in a constructor
- **Released** in the destructor

Ensures cleanup happens automatically, even on exceptions.

```cpp
class Semaphore::ScopedLock {
public:
    Semaphore::ScopedLock(Semaphore& s, int n) : sem(s), num(n) {
        sem.wait(num);    // Acquire in constructor
    }
    ~Semaphore::ScopedLock() {
        sem.signal(num);  // Release in destructor
    }
};
```

---

## Simulation-Specific Terms

### Chair Pool
The 72 chairs available in the ropeway system. Maximum 36 can be in use at once.

### Boarding Queue
Waiting area for tourists ready to board. Has VIP (priority) and regular sections.

### Entry Gate
One of 4 gates for entering the station area. Controls flow and validates tickets.

### Boarding Gate
One of 3 gates to the platform. Controlled by Worker1.

### Trail Difficulty
Three cycling trail difficulty levels (EASY, MEDIUM, HARD) with different descent times.

### VIP Tourist
A tourist with priority boarding. Enters a separate queue that's processed first.

---

## Error Handling

### errno
Global variable set by system calls to indicate error type. Check immediately after failed call.

### perror()
Function that prints a description of the last error (based on errno).

```cpp
if (shmget(...) == -1) {
    perror("shmget");  // Prints: "shmget: <error description>"
}
```

### EINTR
Error code indicating a system call was interrupted by a signal. Usually means retry the call.

### EAGAIN / EWOULDBLOCK
Error code indicating the operation would block but non-blocking mode was requested.

### EIDRM
Error code indicating an IPC resource (semaphore, message queue) was removed.

---

## State Machine

### State
A distinct mode of operation. The ropeway has states: RUNNING, EMERGENCY_STOP, CLOSING, STOPPED.

### Transition
Moving from one state to another based on events or conditions.

### State Diagram
Visual representation of states and transitions:
```
RUNNING ──(emergency)──► EMERGENCY_STOP
    │                         │
    │                    (resume)
    │                         │
(timeout)◄────────────────────┘
    │
    ▼
CLOSING ──(all exited)──► STOPPED
```

---

## C++ Concepts Used

### std::optional<T>
A wrapper that may or may not contain a value. Used for functions that might fail:
```cpp
std::optional<Message> receive() {
    if (error) return std::nullopt;
    return message;
}
```

### Template Class
A class parameterized by type. Used for type-safe IPC wrappers:
```cpp
template<typename T>
class MessageQueue { ... };

MessageQueue<TicketRequest> requestQueue;
```

### volatile
Tells compiler not to optimize away reads/writes. Used for signal flags:
```cpp
volatile sig_atomic_t shouldExit;
```

### sig_atomic_t
Integer type that can be accessed atomically. Safe to use in signal handlers.

### Placement new
Constructing an object in pre-allocated memory:
```cpp
void* memory = shmat(shmid, nullptr, 0);
new (memory) MyStruct();  // Construct in shared memory
```

---

## Command Line Tools

### ipcs
Lists System V IPC resources (shared memory, semaphores, message queues).

### ipcrm
Removes System V IPC resources.

### ps
Lists running processes.

### kill
Sends signals to processes.

### strace (Linux)
Traces system calls made by a process.

### dtrace (macOS)
Dynamic tracing tool for debugging.

---

## See Also

- [Architecture](ARCHITECTURE.md) - System design
- [IPC Mechanisms](IPC_MECHANISMS.md) - IPC details
- [Processes](PROCESSES.md) - Process descriptions
- [Signals](SIGNALS.md) - Signal handling
