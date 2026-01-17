# IPC Mechanisms

This document explains the System V IPC mechanisms used in the Ropeway Simulation.

## Overview

The simulation uses three System V IPC mechanisms:

| Mechanism | Purpose | Header |
|-----------|---------|--------|
| Shared Memory | Store global state accessible by all processes | `<sys/shm.h>` |
| Semaphores | Synchronization and mutual exclusion | `<sys/sem.h>` |
| Message Queues | Process-to-process communication | `<sys/msg.h>` |

## Why System V IPC?

System V IPC was chosen over POSIX IPC because:
1. Project requirements specified System V APIs
2. Works consistently across Linux and macOS
3. Named resources persist until explicitly removed
4. Well-suited for unrelated process communication

## Shared Memory

### Concept

Shared memory allows multiple processes to access the same memory region. It's the fastest IPC mechanism because data doesn't need to be copied between processes.

```
Process A          Shared Memory          Process B
┌─────────┐       ┌─────────────┐        ┌─────────┐
│         │       │             │        │         │
│  ptr ───┼──────►│   DATA      │◄───────┼── ptr   │
│         │       │             │        │         │
└─────────┘       └─────────────┘        └─────────┘
```

### System Calls

```cpp
// Create shared memory segment
int shmget(key_t key, size_t size, int shmflg);

// Attach to shared memory
void* shmat(int shmid, const void* shmaddr, int shmflg);

// Detach from shared memory
int shmdt(const void* shmaddr);

// Control operations (including removal)
int shmctl(int shmid, int cmd, struct shmid_ds* buf);
```

### Our Implementation

```cpp
// ipc/SharedMemory.hpp - RAII wrapper

template<typename T>
class SharedMemory {
public:
    // Constructor: create or attach to segment
    explicit SharedMemory(key_t key, bool create = true) {
        if (create) {
            // Create new segment with exclusive flag
            shmId_ = shmget(key, sizeof(T), IPC_CREAT | IPC_EXCL | 0600);
        } else {
            // Attach to existing segment
            shmId_ = shmget(key, sizeof(T), 0600);
        }
        
        // Attach to address space
        data_ = static_cast<T*>(shmat(shmId_, nullptr, 0));
        
        // Placement new for initialization (if creating)
        if (create) {
            new (data_) T();
        }
    }
    
    // Destructor: detach and optionally remove
    ~SharedMemory() {
        if (data_) shmdt(data_);
        if (isOwner_) shmctl(shmId_, IPC_RMID, nullptr);
    }
    
    // Access operators
    T* operator->() { return data_; }
    T& operator*() { return *data_; }
    
private:
    int shmId_;
    T* data_;
    bool isOwner_;
};
```

### Usage Example

```cpp
// In Orchestrator (creates shared memory)
SharedMemory<RopewaySystemState> shm(SHM_KEY, true);
shm->core.state = RopewayState::RUNNING;

// In Worker (attaches to existing)
SharedMemory<RopewaySystemState> shm(SHM_KEY, false);
RopewayState state = shm->core.state;
```

### ⚠️ Important Notes

1. **Race Conditions**: Shared memory access must be protected by semaphores
2. **Permissions**: Use `0600` for owner-only access
3. **Cleanup**: Always remove shared memory when done (`IPC_RMID`)
4. **Size**: Segment size is fixed at creation time

---

## Semaphores

### Concept

Semaphores are synchronization primitives that control access to shared resources. System V semaphores operate as a **set** of semaphores.

```
Semaphore Value: 1 (resource available)
    │
    │ P() / wait() - decrement
    ▼
Semaphore Value: 0 (resource in use)
    │
    │ V() / signal() - increment
    ▼
Semaphore Value: 1 (resource available again)
```

### Types of Semaphores

| Type | Initial Value | Purpose |
|------|---------------|---------|
| Binary (Mutex) | 1 | Mutual exclusion |
| Counting | N | Resource pool (N items) |
| Signaling | 0 | Event notification |

### System Calls

```cpp
// Create or get semaphore set
int semget(key_t key, int nsems, int semflg);

// Perform operation on semaphore(s)
int semop(int semid, struct sembuf* sops, size_t nsops);

// Control operations
int semctl(int semid, int semnum, int cmd, ...);
```

### The sembuf Structure

```cpp
struct sembuf {
    unsigned short sem_num;  // Semaphore index in set
    short          sem_op;   // Operation: -1 = P/wait, +1 = V/signal, 0 = wait for zero
    short          sem_flg;  // Flags: IPC_NOWAIT, SEM_UNDO
};
```

### Our Implementation

```cpp
// ipc/Semaphore.hpp - RAII wrapper

class Semaphore {
public:
    explicit Semaphore(key_t key, uint32_t numSemaphores, bool create = true) {
        if (create) {
            semId_ = semget(key, numSemaphores, IPC_CREAT | IPC_EXCL | 0600);
        } else {
            semId_ = semget(key, numSemaphores, 0600);
        }
    }
    
    ~Semaphore() {
        if (isOwner_) semctl(semId_, 0, IPC_RMID);
    }
    
    // P operation (wait/decrement)
    bool wait(uint32_t semNum) {
        return operate(semNum, -1, 0);
    }
    
    // V operation (signal/increment)
    bool signal(uint32_t semNum) {
        return operate(semNum, 1, 0);
    }
    
    // Try wait (non-blocking)
    bool tryWait(uint32_t semNum) {
        return operate(semNum, -1, IPC_NOWAIT);
    }
    
private:
    bool operate(uint32_t semNum, short op, short flags) {
        struct sembuf operation{};
        operation.sem_num = semNum;
        operation.sem_op = op;
        operation.sem_flg = flags | SEM_UNDO;  // Auto-undo on process exit
        
        if (semop(semId_, &operation, 1) == -1) {
            if (errno == EINTR) return false;  // Interrupted by signal
            if (errno == EAGAIN) return false; // Would block (with IPC_NOWAIT)
            return false;
        }
        return true;
    }
};
```

### Semaphore::ScopedLock (RAII Guard)

```cpp
// Automatic lock/unlock using RAII
class Semaphore::ScopedLock {
public:
    Semaphore::ScopedLock(Semaphore& sem, uint32_t semNum)
        : sem_(sem), semNum_(semNum), locked_(false) {
        if (sem_.wait(semNum_)) {
            locked_ = true;
        }
    }
    
    ~Semaphore::ScopedLock() {
        if (locked_) {
            sem_.signal(semNum_);
        }
    }
    
    bool isLocked() const { return locked_; }
    
private:
    Semaphore& sem_;
    uint32_t semNum_;
    bool locked_;
};
```

### Usage Examples

```cpp
// Mutex for shared memory access
{
    Semaphore::ScopedLock lock(sem, Semaphore::Index::SHARED_MEMORY);
    // Safe to access shared memory here
    shm->core.state = RopewayState::RUNNING;
}  // Lock automatically released

// Counting semaphore for station capacity
sem.wait(Semaphore::Index::STATION_CAPACITY);  // Enter station (decrement)
// ... tourist is in station ...
sem.signal(Semaphore::Index::STATION_CAPACITY); // Leave station (increment)

// Signaling semaphore for initialization
// In child process:
sem.signal(Semaphore::Index::LOWER_WORKER_READY);

// In parent process:
sem.wait(Semaphore::Index::LOWER_WORKER_READY);  // Blocks until child signals
```

### Semaphore Index Definitions

```cpp
// ipc/semaphore_index.hpp
namespace Semaphore::Index {
    constexpr uint32_t SHARED_MEMORY = 0;        // Mutex
    constexpr uint32_t STATION_CAPACITY = 1;     // Counting
    constexpr uint32_t CASHIER_READY = 2;        // Signaling
    constexpr uint32_t LOWER_WORKER_READY = 3;        // Signaling
    constexpr uint32_t UPPER_WORKER_READY = 4;        // Signaling
    constexpr uint32_t BOARDING_QUEUE_WORK = 5;  // Signaling
    constexpr uint32_t ENTRY_GATE_0 = 6;         // Mutex
    constexpr uint32_t ENTRY_GATE_1 = 7;         // Mutex
    constexpr uint32_t ENTRY_GATE_2 = 8;         // Mutex
    constexpr uint32_t ENTRY_GATE_3 = 9;         // Mutex
    constexpr uint32_t CHAIR_ASSIGNED_BASE = 10; // Per-tourist signaling
    
    constexpr uint32_t TOTAL_SEMAPHORES = CHAIR_ASSIGNED_BASE + 1000;
}
```

### ⚠️ Important Notes

1. **SEM_UNDO**: Always use this flag to auto-cleanup on process crash
2. **EINTR**: Handle signal interruption gracefully
3. **Deadlocks**: Acquire locks in consistent order
4. **Performance**: Semaphores have kernel overhead; minimize hold time

---

## Message Queues

### Concept

Message queues provide asynchronous communication between processes. Messages are stored in a kernel-managed queue until read.

```
Process A                Queue                  Process B
┌─────────┐         ┌───────────┐            ┌─────────┐
│         │ msgsnd  │ MSG 1     │  msgrcv    │         │
│         │────────►│ MSG 2     │───────────►│         │
│         │         │ MSG 3     │            │         │
└─────────┘         └───────────┘            └─────────┘
```

### Message Structure

Every message must have a `long mtype` as the first field:

```cpp
struct MyMessage {
    long mtype;      // Message type (must be > 0)
    // ... your data fields ...
    char text[100];
    int value;
};
```

### System Calls

```cpp
// Create or get message queue
int msgget(key_t key, int msgflg);

// Send message
int msgsnd(int msqid, const void* msgp, size_t msgsz, int msgflg);

// Receive message
ssize_t msgrcv(int msqid, void* msgp, size_t msgsz, long msgtyp, int msgflg);

// Control operations
int msgctl(int msqid, int cmd, struct msqid_ds* buf);
```

### Message Type Filtering

The `msgtyp` parameter in `msgrcv` controls which messages to receive:

| msgtyp | Behavior |
|--------|----------|
| 0 | Receive first message (any type) |
| > 0 | Receive first message of exactly this type |
| < 0 | Receive first message with type ≤ |msgtyp| |

### Our Implementation

```cpp
// ipc/MessageQueue.hpp - RAII wrapper

template<typename T>
class MessageQueue {
public:
    explicit MessageQueue(key_t key, bool create = true) {
        if (create) {
            msgId_ = msgget(key, IPC_CREAT | IPC_EXCL | 0600);
        } else {
            msgId_ = msgget(key, 0600);
        }
    }
    
    ~MessageQueue() {
        if (isOwner_) msgctl(msgId_, IPC_RMID, nullptr);
    }
    
    // Send message (blocking)
    bool send(const T& message, int flags = 0) {
        if (msgsnd(msgId_, &message, sizeof(T) - sizeof(long), flags) == -1) {
            if (errno == EINTR) return false;
            return false;
        }
        return true;
    }
    
    // Receive message (blocking)
    std::optional<T> receive(long msgType = 0, int flags = 0) {
        T message{};
        ssize_t result = msgrcv(msgId_, &message, 
                                sizeof(T) - sizeof(long), 
                                msgType, flags);
        if (result == -1) {
            if (errno == EINTR) return std::nullopt;
            return std::nullopt;
        }
        return message;
    }
    
    // Try receive (non-blocking)
    std::optional<T> tryReceive(long msgType = 0) {
        return receive(msgType, IPC_NOWAIT);
    }
};
```

### Message Definitions

```cpp
// Ticket purchase request
struct TicketRequest {
    long mtype;              // Always CashierMsgType::REQUEST (1)
    uint32_t touristId;
    uint32_t touristAge;
    TicketType requestedType;
    bool requestVip;
};

// Ticket purchase response
struct TicketResponse {
    long mtype;              // CashierMsgType::RESPONSE_BASE + touristId
    uint32_t touristId;
    bool success;
    uint32_t ticketId;
    TicketType ticketType;
    bool isVip;
    double price;
    double discount;
    time_t validFrom;
    time_t validUntil;
    char message[64];
};

// Worker-to-worker communication
struct WorkerMessage {
    long mtype;              // 1 = to Worker1, 2 = to Worker2
    uint32_t senderId;
    uint32_t receiverId;
    WorkerSignal signal;
    time_t timestamp;
    char messageText[128];
};
```

### Usage Examples

```cpp
// Tourist sends ticket request
TicketRequest request;
request.mtype = CashierMsgType::REQUEST;
request.touristId = myId;
request.touristAge = 25;
request.requestedType = TicketType::SINGLE_USE;
request.requestVip = false;

requestQueue.send(request);

// Tourist waits for response
auto response = responseQueue.receive(
    CashierMsgType::RESPONSE_BASE + myId  // Only my response
);
if (response && response->success) {
    // Got ticket!
}

// Cashier receives any request
auto request = requestQueue.receive(CashierMsgType::REQUEST);
if (request) {
    processRequest(*request);
}

// Worker1 checks for messages from Worker2 (non-blocking)
auto msg = msgQueue.tryReceive(MSG_TYPE_FROM_WORKER2);
if (msg) {
    handleMessage(*msg);
}
```

### ⚠️ Important Notes

1. **Message Size**: Use `sizeof(T) - sizeof(long)` to exclude mtype
2. **mtype > 0**: Message type must always be positive
3. **Blocking**: Default `msgrcv` blocks; use `IPC_NOWAIT` for non-blocking
4. **Queue Limits**: There's a maximum queue size; monitor with `msgctl`

---

## IPC Resource Management

### The IpcManager Class

```cpp
// ipc/IpcManager.hpp - Central IPC resource manager

class IpcManager {
public:
    IpcManager(key_t baseKey, bool create) {
        shm_ = std::make_unique<SharedMemory<RopewaySystemState>>(baseKey, create);
        sem_ = std::make_unique<Semaphore>(baseKey + 0x1000, 
                                           Semaphore::Index::TOTAL_SEMAPHORES, 
                                           create);
        msgQueue_ = std::make_unique<MessageQueue<WorkerMessage>>(baseKey + 0x2000, 
                                                                   create);
        cashierMsgQueue_ = std::make_unique<MessageQueue<TicketRequest>>(baseKey + 0x2001, 
                                                                          create);
    }
    
    // Accessors
    RopewaySystemState* state() { return shm_->get(); }
    Semaphore& semaphores() { return *sem_; }
    // ...
    
    // Static cleanup function
    static void cleanup(key_t baseKey) {
        SharedMemory<RopewaySystemState>::removeByKey(baseKey);
        Semaphore::removeByKey(baseKey + 0x1000);
        MessageQueue<WorkerMessage>::removeByKey(baseKey + 0x2000);
        MessageQueue<TicketRequest>::removeByKey(baseKey + 0x2001);
    }
};
```

### Cleanup on Crash

If the simulation crashes, IPC resources may remain. Clean them up:

```bash
# List all IPC resources
ipcs

# Remove specific resources
ipcrm -M <key>   # Remove shared memory by key
ipcrm -S <key>   # Remove semaphore by key
ipcrm -Q <key>   # Remove message queue by key

# Remove by ID
ipcrm -m <id>    # Remove shared memory by ID
ipcrm -s <id>    # Remove semaphore by ID
ipcrm -q <id>    # Remove message queue by ID
```

---

## Common Pitfalls

### 1. Forgetting to Remove IPC Resources

```cpp
// ❌ BAD: Resources leak on crash
int shmid = shmget(key, size, IPC_CREAT);
// ... crash here = orphaned resource

// ✅ GOOD: RAII ensures cleanup
SharedMemory<Data> shm(key, true);  // Removed in destructor
```

### 2. Race Conditions in Shared Memory

```cpp
// ❌ BAD: Unprotected access
shm->counter++;  // Not atomic!

// ✅ GOOD: Protected by semaphore
{
    Semaphore::ScopedLock lock(sem, SEM_MUTEX);
    shm->counter++;
}
```

### 3. Deadlock from Lock Ordering

```cpp
// ❌ BAD: Different lock order in different processes
// Process A: lock(A), lock(B)
// Process B: lock(B), lock(A)  -> DEADLOCK!

// ✅ GOOD: Consistent lock order everywhere
// Always: lock(A) before lock(B)
```

### 4. Not Handling EINTR

```cpp
// ❌ BAD: Assumes success
semop(semid, &op, 1);

// ✅ GOOD: Handle signal interruption
while (semop(semid, &op, 1) == -1) {
    if (errno == EINTR) continue;  // Retry after signal
    break;  // Real error
}
```

### 5. Message Size Mismatch

```cpp
// ❌ BAD: Including mtype in size
msgsnd(msgid, &msg, sizeof(msg), 0);  // Wrong!

// ✅ GOOD: Exclude mtype from size
msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0);
```

---

## Summary

| Mechanism | Best For | Overhead | Persistence |
|-----------|----------|----------|-------------|
| Shared Memory | Large/frequent data | Lowest | Until removed |
| Semaphores | Synchronization | Medium | Until removed |
| Message Queues | Small messages | Highest | Until removed |

## See Also

- `man shmget`, `man shmat`, `man shmctl`
- `man semget`, `man semop`, `man semctl`
- `man msgget`, `man msgsnd`, `man msgrcv`
- [Architecture](ARCHITECTURE.md)
- [Processes](PROCESSES.md)
