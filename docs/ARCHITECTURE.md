# System Architecture

This document describes the overall architecture of the Ropeway Simulation system.

## Overview

The simulation follows a **multi-process architecture** where each logical component runs as a separate Unix process. Processes communicate using System V IPC mechanisms.

## Component Diagram

```
                              ┌─────────────────────┐
                              │    ORCHESTRATOR     │
                              │    (main process)   │
                              │                     │
                              │ • Creates IPC       │
                              │ • Spawns processes  │
                              │ • Monitors state    │
                              │ • Generates reports │
                              └──────────┬──────────┘
                                         │
              ┌──────────────────────────┼──────────────────────────┐
              │                          │                          │
              ▼                          ▼                          ▼
    ┌─────────────────┐       ┌─────────────────┐       ┌─────────────────┐
    │    CASHIER      │       │    WORKER 1     │       │    WORKER 2     │
    │                 │       │  (Lower Station)│       │  (Upper Station)│
    │ • Sells tickets │       │                 │       │                 │
    │ • Applies       │       │ • Entry gates   │       │ • Exit routes   │
    │   discounts     │       │ • Boarding      │       │ • Monitors      │
    │ • VIP handling  │       │ • Chair assign  │       │   arrivals      │
    └─────────────────┘       │ • Emergency     │       │ • Emergency     │
              ▲               │   handling      │       │   coordination  │
              │               └─────────────────┘       └─────────────────┘
              │                         ▲                         ▲
              │                         │                         │
    ┌─────────┴─────────────────────────┴─────────────────────────┴─────────┐
    │                          SHARED MEMORY                                 │
    │  ┌──────────────┬──────────────┬──────────────┬──────────────┐        │
    │  │ Core State   │ Chair Pool   │ Boarding     │ Statistics   │        │
    │  │              │              │ Queue        │              │        │
    │  │ • State      │ • 72 chairs  │ • VIP queue  │ • Rides      │        │
    │  │ • Worker PIDs│ • In-use cnt │ • Regular    │ • Revenue    │        │
    │  │ • Flags      │ • Assign info│   queue      │ • Per-tourist│        │
    │  └──────────────┴──────────────┴──────────────┴──────────────┘        │
    └───────────────────────────────────────────────────────────────────────┘
              │
    ┌─────────┴─────────────────────────────────────────────────────────────┐
    │                            TOURISTS (N processes)                      │
    │                                                                        │
    │   Tourist 1    Tourist 2    Tourist 3    ...    Tourist N              │
    │   ┌───────┐    ┌───────┐    ┌───────┐          ┌───────┐              │
    │   │PEDEST │    │CYCLIST│    │CYCLIST│          │PEDEST │              │
    │   │Age: 25│    │Age: 8 │    │Age: 67│          │Age: 45│              │
    │   │VIP: No│    │VIP: No│    │VIP: Yes│         │VIP: No│              │
    │   └───────┘    └───────┘    └───────┘          └───────┘              │
    └───────────────────────────────────────────────────────────────────────┘
```

## Process Lifecycle

### 1. Initialization Phase

```
Orchestrator starts
    │
    ├─► Create shared memory segment
    ├─► Create semaphore set
    ├─► Create message queues
    │
    ├─► fork() + exec() → Cashier process
    │       └─► Wait for CASHIER_READY semaphore
    │
    ├─► fork() + exec() → Worker1 process
    ├─► fork() + exec() → Worker2 process
    │       └─► Wait for LOWER_WORKER_READY, UPPER_WORKER_READY semaphores
    │
    └─► Loop: fork() + exec() → Tourist processes
            └─► Staggered spawning with random delays
```

### 2. Running Phase

```
┌─────────────────────────────────────────────────────────────────────┐
│                         SIMULATION LOOP                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  Orchestrator:                                                       │
│    • Monitors shared memory state                                    │
│    • Can trigger emergency stop (sends SIGUSR1 to Worker1)           │
│    • Can trigger resume (sends SIGUSR2 to Worker1)                   │
│    • Checks for timeout/completion                                   │
│                                                                      │
│  Cashier:                                                            │
│    • Waits for ticket requests (blocking msgrcv)                     │
│    • Processes requests, calculates prices                           │
│    • Sends responses via message queue                               │
│                                                                      │
│  Worker1 (Lower Station):                                            │
│    • Processes boarding queue                                        │
│    • Assigns tourists to chairs                                      │
│    • Handles emergency stop/resume                                   │
│    • Communicates with Worker2 via message queue                     │
│                                                                      │
│  Worker2 (Upper Station):                                            │
│    • Monitors chair arrivals                                         │
│    • Manages exit routes                                             │
│    • Responds to emergency coordination                              │
│                                                                      │
│  Tourists:                                                           │
│    • State machine: BUYING → WAITING_ENTRY → WAITING_BOARDING →      │
│                     ON_CHAIR → AT_TOP → [ON_TRAIL →] FINISHED        │
│    • Buy tickets, enter gates, board chairs, ride, exit              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 3. Shutdown Phase

```
Timeout reached OR simulation complete
    │
    ├─► Set state to CLOSING
    │
    ├─► Wait for tourists in station to finish
    │
    ├─► Set state to STOPPED
    │
    ├─► Generate daily report
    │
    ├─► Send SIGTERM to all child processes
    │       ├─► Cashier exits gracefully
    │       ├─► Worker1 exits gracefully
    │       ├─► Worker2 exits gracefully
    │       └─► Tourists exit gracefully
    │
    └─► Clean up IPC resources
            ├─► Detach and remove shared memory
            ├─► Remove semaphore set
            └─► Remove message queues
```

## State Machine

### Ropeway States

```
    ┌─────────┐
    │ RUNNING │◄──────────────────┐
    └────┬────┘                   │
         │                        │
         │ Emergency              │ Resume confirmed
         ▼                        │
┌─────────────────┐               │
│ EMERGENCY_STOP  │───────────────┘
└─────────────────┘
         │
         │ Timeout/Manual close
         ▼
    ┌─────────┐
    │ CLOSING │
    └────┬────┘
         │
         │ All tourists exited
         ▼
    ┌─────────┐
    │ STOPPED │
    └─────────┘
```

### Tourist States

```
┌──────────────┐
│ BUYING_TICKET│
└──────┬───────┘
       │ Ticket received
       ▼
┌──────────────┐     ┌──────────┐
│WAITING_ENTRY │────►│ FINISHED │ (if !wantsToRide)
└──────┬───────┘     └──────────┘
       │ Entry gate passed
       ▼
┌────────────────┐
│WAITING_BOARDING│
└──────┬─────────┘
       │ Chair assigned
       ▼
┌──────────────┐
│   ON_CHAIR   │
└──────┬───────┘
       │ Arrived at top
       ▼
┌──────────────┐
│   AT_TOP     │
└──────┬───────┘
       │
       ├─────────────────┐
       │ (Pedestrian)    │ (Cyclist)
       ▼                 ▼
┌──────────┐      ┌──────────┐
│ FINISHED │      │ ON_TRAIL │
└──────────┘      └────┬─────┘
                       │ Trail completed
                       ▼
               ┌───────────────┐
               │ WAITING_ENTRY │ (for another ride)
               └───────────────┘
```

## Data Flow

### Ticket Purchase Flow

```
Tourist                    Cashier
   │                          │
   │  TicketRequest (msgq)    │
   │─────────────────────────►│
   │                          │
   │                          │ Calculate price
   │                          │ Apply discounts
   │                          │ Generate ticket ID
   │                          │
   │  TicketResponse (msgq)   │
   │◄─────────────────────────│
   │                          │
   │ Store ticket info        │
   │ in shared memory         │
   │                          │
```

### Boarding Flow

```
Tourist              Worker1              Shared Memory
   │                    │                      │
   │ Join boarding      │                      │
   │ queue (shm)        │                      │
   │───────────────────►│                      │
   │                    │                      │
   │ Signal work        │                      │
   │ (semaphore)        │                      │
   │───────────────────►│                      │
   │                    │                      │
   │                    │ Read queue           │
   │                    │◄─────────────────────│
   │                    │                      │
   │                    │ Assign chair         │
   │                    │─────────────────────►│
   │                    │                      │
   │ Wait for chair     │                      │
   │ assignment (sem)   │                      │
   │◄─────────────────────────────────────────│
   │                    │                      │
```

### Emergency Stop Flow

```
Orchestrator      Worker1        Worker2       Tourists
     │               │              │              │
     │  SIGUSR1      │              │              │
     │──────────────►│              │              │
     │               │              │              │
     │               │ EMERGENCY_STOP              │
     │               │ message (msgq)              │
     │               │─────────────►│              │
     │               │              │              │
     │               │  Set state   │              │
     │               │  in shm      │              │
     │               │──────────────┼─────────────►│
     │               │              │              │
     │               │              │ SIGUSR1      │
     │               │              │ (broadcast)  │
     │               │◄─────────────│              │
     │               │              │              │
     │  SIGUSR2      │              │              │
     │  (resume)     │              │              │
     │──────────────►│              │              │
     │               │              │              │
     │               │ READY_TO_START              │
     │               │─────────────►│              │
     │               │              │              │
     │               │ READY_TO_START              │
     │               │◄─────────────│              │
     │               │              │              │
     │               │ Set RUNNING  │              │
     │               │──────────────┼─────────────►│
     │               │              │              │
```

## Memory Layout

### Shared Memory Structure

```cpp
struct RopewaySystemState {
    CoreState core;           // Basic state, PIDs, flags
    ChairPool chairPool;      // Chair management
    BoardingQueue queue;      // Tourist queues
    StatisticsData stats;     // Runtime statistics
};
```

```
┌─────────────────────────────────────────────────────────────┐
│                    RopewaySystemState                        │
├─────────────────────────────────────────────────────────────┤
│ CoreState (offset 0)                                         │
│   ├─ state: RopewayState (4 bytes)                          │
│   ├─ worker1Pid: pid_t (4 bytes)                            │
│   ├─ worker2Pid: pid_t (4 bytes)                            │
│   ├─ acceptingNewTourists: bool (1 byte)                    │
│   ├─ closingTime: time_t (8 bytes)                          │
│   └─ totalRidesToday: uint32_t (4 bytes)                    │
├─────────────────────────────────────────────────────────────┤
│ ChairPool                                                    │
│   ├─ chairs[72]: Chair structs                              │
│   ├─ chairsInUse: uint32_t                                  │
│   └─ nextChairIndex: uint32_t                               │
├─────────────────────────────────────────────────────────────┤
│ BoardingQueue                                                │
│   ├─ vipQueue[MAX_VIP]: TouristQueueEntry                   │
│   ├─ regularQueue[MAX_REGULAR]: TouristQueueEntry           │
│   ├─ vipCount, regularCount: uint32_t                       │
│   └─ platformCount: uint32_t                                │
├─────────────────────────────────────────────────────────────┤
│ StatisticsData                                               │
│   ├─ dailyStats: DailyStatistics                            │
│   ├─ touristRecords[MAX]: TouristRideRecord                 │
│   └─ touristRecordCount: uint32_t                           │
└─────────────────────────────────────────────────────────────┘
```

## Semaphore Usage

| Index | Name | Purpose |
|-------|------|---------|
| 0 | SHARED_MEMORY | Mutex for shared memory access |
| 1 | STATION_CAPACITY | Counting sem for station limit |
| 2 | CASHIER_READY | Signals cashier initialization |
| 3 | LOWER_WORKER_READY | Signals worker1 initialization |
| 4 | UPPER_WORKER_READY | Signals worker2 initialization |
| 5 | BOARDING_QUEUE_WORK | Signals work available for worker1 |
| 6 | ENTRY_GATE_0..3 | Entry gate mutexes |
| 10 | CHAIR_ASSIGNED_BASE+n | Per-tourist chair assignment notification |

## See Also

- [IPC Mechanisms](IPC_MECHANISMS.md) - Detailed IPC documentation
- [Processes](PROCESSES.md) - Process responsibilities
- [Signals](SIGNALS.md) - Signal handling
