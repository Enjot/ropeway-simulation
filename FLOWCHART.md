# Ropeway Simulation - Business Logic Flowchart

## Overview

This document provides a detailed flowchart of the tourist flow through the ropeway simulation system.

---

## High-Level Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              ROPEWAY SIMULATION FLOW                                │
└─────────────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────────────┐
                    │  TOURIST GENERATOR                  │
                    │  (fork + exec)                      │
                    │                                     │
                    │  • Age: 8-80                        │
                    │  • Type: Walker/Cyclist (~50%)      │
                    │  • VIP: ~1%                         │
                    │  • Kids: 0-2 (if                    │
                    │    walker, age 26+)                 │
                    └──────────┬──────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                                    CASHIER                                          │
│  ┌─────────────────────────────────────────────────────────────────────────────┐    │
│  │  MQ_CASHIER (Message Queue)                                                 │    │
│  │  • Tourist sends: id, age, type, is_vip, kid_count                          │    │
│  │  • Cashier returns: ticket_type, ticket_valid_until                         │    │
│  └─────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                     │
│  TICKET TYPES:                          DISCOUNTS:                                  │
│  ├─ SINGLE (15 PLN) - 1 ride only       ├─ Age < 10: 25% off                        │
│  ├─ TIME_T1 (30 PLN)                    └─ Age 65+: 25% off                         │
│  ├─ TIME_T2 (50 PLN)                                                                │
│  ├─ TIME_T3 (70 PLN)                    VIP SURCHARGE: +50 PLN                      │
│  └─ DAILY (80 PLN) - unlimited                                                      │
│                                                                                     │
│  Family: Parent buys same ticket type for all kids                                  │
│                                                                                     │
│  [X] If station closing → ticket_type = -1 → Tourist leaves                         │
└─────────────────────────────────────────────────────────────────────────────────────┘
                               │
                               ▼
        ┌──────────────────────────────────────────────────────────────┐
        │                     TICKET VALIDATION                        │
        │                                                              │
        │  Check before each ride:                                     │
        │  • SINGLE: rides_completed == 0?                             │
        │  • TIME_T1/T2/T3: current_time < expiration?                 │
        │  • DAILY: valid until sim_end                                │
        │                                                              │
        │  [X] Invalid ticket → Tourist leaves                         │
        │  [X] Station closing → Tourist leaves                        │
        └──────────────────────────┬───────────────────────────────────┘
                                   │
          ┌────────────────────────┴────────────────────────────────┐
          │                                                         │
          ▼                                                         │
┌─────────────────────────────────────────────────────────────┐     │
│                    4 ENTRY GATES                            │     │
│                                                             │     │
│  SEM_ENTRY_GATES = 4 (semaphore)                            │     │
│                                                             │     │
│  ┌───┐  ┌───┐  ┌───┐  ┌───┐                                 │     │
│  │ 1 │  │ 2 │  │ 3 │  │ 4 │                                 │     │
│  └───┘  └───┘  └───┘  └───┘                                 │     │
│                                                             │     │
│  • VIPs SKIP the queue (bypass SEM_ENTRY_GATES)             │     │
│  • Entry recorded: tourist_entries[id] = {ticket_type}      │     │
│  • Family enters together (synchronized with barrier)       │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           ▼                                        │
┌─────────────────────────────────────────────────────────────┐     │
│               LOWER STATION (WAITING ROOM)                  │     │
│                                                             │     │
│  SEM_LOWER_STATION = N (configurable capacity)              │     │
│                                                             │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │  ATOMIC FAMILY SLOT ACQUISITION                     │    │     │
│  │  sem_wait_n(SEM_LOWER_STATION, family_size)         │    │     │
│  │                                                     │    │     │
│  │  family_size = 1 (parent) + kid_count               │    │     │
│  │  Prevents race conditions where families exceed N   │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  ~10% of first-time riders decide NOT to use chairlift      │     │
│  → Release slots and leave                                  │     │
│                                                             │     │
│  [X] Station closing → Tourists leave                       │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           ▼                                        │
┌─────────────────────────────────────────────────────────────┐     │
│                   3 PLATFORM GATES                          │     │
│                                                             │     │
│  SEM_PLATFORM_GATES = 3 (semaphore)                         │     │
│                                                             │     │
│  ┌───────┐  ┌───────┐  ┌───────┐                            │     │
│  │ Gate1 │  │ Gate2 │  │ Gate3 │                            │     │
│  └───────┘  └───────┘  └───────┘                            │     │
│                                                             │     │
│  Tourist passes through and proceeds to boarding            │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           ▼                                        │
┌─────────────────────────────────────────────────────────────┐     │
│                     LOWER WORKER                            │     │
│              (Controls Chair Boarding)                      │     │
│                                                             │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │  MQ_PLATFORM - Boarding Request Queue               │    │     │
│  │                                                     │    │     │
│  │  NO VIP priority (FIFO boarding):                   │    │     │
│  │  • mtype = 1 → Requeued (didn't fit, priority)      │    │     │
│  │  • mtype = 2 → All new requests                     │    │     │
│  │                                                     │    │     │
│  │  Message: {tourist_id, tourist_type, slots_needed,  │    │     │
│  │            kid_count}                               │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  CHAIR CAPACITY = 4 slots                                   │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │  SLOT CALCULATION:                                  │    │     │
│  │  • Walker: 1 slot                                   │    │     │
│  │  • Cyclist: 2 slots                                 │    │     │
│  │  • Kids: 1 slot each (added to parent's slots)      │    │     │
│  │                                                     │    │     │
│  │  Walker + 2 kids = 3 slots                          │    │     │
│  │  Cyclist + 2 kids = 4 slots                         │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  BOARDING LOGIC:                                            │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │  IF tourist fits on current chair:                  │    │     │
│  │    → Send confirmation via MQ_BOARDING              │    │     │
│  │    → Add slots to current chair                     │    │     │
│  │    → IF chair full: dispatch & start new            │    │     │
│  │                                                     │    │     │
│  │  IF tourist DOESN'T fit:                            │    │     │
│  │    → Dispatch current chair                         │    │     │
│  │    → Re-queue tourist with mtype=1 (priority)       │    │     │
│  │    → Tourist boards next chair                      │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  [!] EMERGENCY STOP: SIGUSR1 → stops chairlift              │     │
│  ✓  RESUME: Coordinates with Upper Worker via semaphores    │     │
│     SEM_LOWER_READY + SEM_UPPER_READY → both ready          │     │
│     → SIGUSR2 → resumes chairlift                           │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           ▼                                        │
┌─────────────────────────────────────────────────────────────┐     │
│                       CHAIRLIFT                             │     │
│                                                             │     │
│  SEM_CHAIRS = 36 (chair availability)                       │     │
│                                                             │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │     ◯───◯───◯───◯───◯───◯───◯───◯───◯───◯───◯       │    │     │
│  │    ╱                                         ╲      │    │     │
│  │   ╱   LOWER PLATFORM ════════> UPPER PLATFORM ╲     │    │     │
│  │  ╱          ▲                       │          ╲    │    │     │
│  │ ◯───◯───◯───◯───◯───◯───◯───◯───◯───◯───◯───◯───◯   │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  Travel time: chair_travel_time_sim (simulated minutes)     │     │
│  ONE-WAY ONLY: Tourists cannot ride back down               │     │
│                                                             │     │
│  Tourist flow:                                              │     │
│  1. Acquire SEM_CHAIRS slot                                 │     │
│  2. Sleep for travel time                                   │     │
│  3. Release SEM_CHAIRS slot on arrival                      │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           ▼                                        │
┌─────────────────────────────────────────────────────────────┐     │
│                     UPPER PLATFORM                          │     │
│                                                             │     │
│  SEM_EXIT_GATES = 2 (semaphore)                             │     │
│                                                             │     │
│  ┌───────────┐  ┌───────────┐                               │     │
│  │  Exit 1   │  │  Exit 2   │                               │     │
│  └───────────┘  └───────────┘                               │     │
│                                                             │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │  MQ_ARRIVALS - Arrival Notification Queue           │    │     │
│  │  • Tourist notifies Upper Worker of arrival         │    │     │
│  │  • Message: {tourist_id, kid_count}                 │    │     │
│  │  • Upper Worker counts arrivals for report          │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  UPPER WORKER:                                              │     │
│  [!]  EMERGENCY STOP: SIGUSR1 → stops chairlift             │     │
│  [~]  RESUME: Coordinates with Lower Worker via semaphores  │     │
│                                                             │     │
│  After closing: Wait 3 real seconds after last tourist,     │     │
│  then turn off chairlift                                    │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           ▼                                        │
┌─────────────────────────────────────────────────────────────┐     │
│                         TRAILS                              │     │
│                                                             │     │
│  ┌─────────────────────────────────────────────────────┐    │     │
│  │  4 TRAIL TYPES:                                     │    │     │
│  │                                                     │    │     │
│  │  WALKING TRAIL (Walkers only)                       │    │     │
│  │     Time: trail_walk_time (between T1 and T2)       │    │     │
│  │                                                     │    │     │
│  │  BIKE TRAILS (Cyclists only, random selection):     │    │     │
│  │     ├─ FAST:   trail_bike_fast_time (T1)            │    │     │
│  │     ├─ MEDIUM: trail_bike_medium_time (T2)          │    │     │
│  │     └─ SLOW:   trail_bike_slow_time (T3)            │    │     │
│  │                                                     │    │     │
│  │  Time order: T1 < walking < T2 < T3                 │    │     │
│  └─────────────────────────────────────────────────────┘    │     │
│                                                             │     │
│  Tourist descends and returns to Entry Gates                │     │
│  Statistics updated: rides_completed++                      │     │
└──────────────────────────┬──────────────────────────────────┘     │
                           │                                        │
                           │                                        │
                           └──────────────────────────────────┬─────┘
                                                              │
                                    LOOP BACK TO ENTRY GATES ─┘
                                    (if ticket still valid)


┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              EXIT CONDITIONS                                        │
│                                                                                     │
│  Tourist LEAVES the simulation when:                                                │
│  • Single ticket used (rides_completed >= 1)                                        │
│  • Time-based ticket expired (current_time >= ticket_valid_until)                   │
│  • Station closing (closing = 1)                                                    │
│  • Cashier rejected ticket (station closing when buying)                            │
│  • Decided not to ride (~10% on first visit to waiting room)                        │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Detailed Stage-by-Stage Flow

### Stage 1: Tourist Generation
```
TouristGenerator (tourist_generator.c)
├── Runs in loop until closing
├── Respects max_concurrent_tourists limit
├── For each tourist:
│   ├── Generate age (weighted: 70% adults 26-64, 15% young adults, 10% young, 5% seniors)
│   ├── Generate type (walker/cyclist based on walker_percentage)
│   ├── Generate VIP status (~1%)
│   ├── IF walker AND age >= 26:
│   │   └── Generate kids (0-2, weighted: 60%=0, 25%=1, 15%=2)
│   └── fork() + execl("tourist", id, age, type, vip, kid_count)
└── Reap zombie processes with WNOHANG
```

### Stage 2: Cashier
```
Tourist → MQ_CASHIER → Cashier → MQ_CASHIER → Tourist

Request Message (mtype=1):
├── tourist_id
├── age
├── tourist_type (walker/cyclist)
├── is_vip
└── kid_count

Cashier Processing:
├── Check if station closing → reject with ticket_type=-1
├── Select random ticket type (weighted distribution)
├── Calculate price with discounts
├── Calculate validity time
├── Update statistics (total_tourists += 1 + kid_count)
└── Send response (mtype=tourist_id)

Response Message:
├── ticket_type
└── ticket_valid_until
```

### Stage 3: Entry Gates (4 gates)
```
sem_wait(SEM_ENTRY_GATES)
├── Wait for available gate (4 slots)
├── sync_with_kids(stage=1)
├── Record entry in tourist_entries[]
└── sem_post(SEM_ENTRY_GATES)
```

### Stage 4: Lower Station
```
sem_wait_n(SEM_LOWER_STATION, family_size)
├── Atomic acquisition of N slots (prevents exceeding capacity)
├── Update lower_station_count
├── sync_with_kids(stage=2)
├── IF first ride AND random(10) == 0:
│   ├── Decide not to ride (~10%)
│   ├── sem_post_n(SEM_LOWER_STATION, family_size)
│   └── LEAVE
└── Proceed to platform gates
```

### Stage 5: Platform Gates (3 gates)
```
sem_wait(SEM_PLATFORM_GATES)
├── Wait for available platform gate (3 slots)
├── sync_with_kids(stage=3)
└── Proceed to boarding
```

### Stage 6: Boarding (Lower Worker)
```
Tourist Side:
├── Send PlatformMsg to MQ_PLATFORM
│   ├── mtype = is_vip ? 1 : 2  (VIP priority)
│   ├── tourist_id
│   ├── tourist_type
│   ├── slots_needed (walker=1, cyclist=2, + kid_count)
│   └── kid_count
├── Wait for confirmation on MQ_BOARDING (mtype=tourist_id)
├── sem_post(SEM_PLATFORM_GATES)
├── sem_post_n(SEM_LOWER_STATION, family_size)
├── sem_wait(SEM_CHAIRS)
└── sync_with_kids(stage=4)

Lower Worker Side:
├── msgrcv(MQ_PLATFORM, ..., -2, 0)  // Gets lowest mtype first
├── IF slots_needed <= remaining_chair_capacity:
│   ├── current_chair_slots += slots_needed
│   ├── Send confirmation to MQ_BOARDING (mtype=tourist_id)
│   └── IF chair full: dispatch, reset counter
└── ELSE (doesn't fit):
    ├── Dispatch current chair
    ├── Re-queue tourist with mtype=1 (high priority)
    └── Tourist boards next chair
```

### Stage 7: Chairlift Ride
```
├── Sleep for chair_travel_time (simulated → real seconds)
├── sync_with_kids(stage=5)
└── sem_post(SEM_CHAIRS)  // Release chair slot on arrival
```

### Stage 8: Upper Platform
```
sem_wait(SEM_EXIT_GATES)
├── Wait for exit gate (2 slots)
├── Send ArrivalMsg to MQ_ARRIVALS
│   ├── tourist_id
│   └── kid_count
├── sem_post(SEM_EXIT_GATES)
└── sync_with_kids(stage=6)
```

### Stage 9: Trail Descent
```
IF walker:
└── Sleep for trail_walk_time

IF cyclist:
├── Random selection: fast/medium/slow
└── Sleep for selected trail time

sync_with_kids(stage=7)
rides_completed++
statistics.total_rides++
```

### Stage 10: Loop Decision
```
IF single_ticket AND rides_completed >= 1:
└── EXIT

IF time_ticket AND current_time >= ticket_valid_until:
└── EXIT

IF station_closing:
└── EXIT

ELSE:
└── GOTO Entry Gates (Stage 3)
```

---

## IPC Summary

| Resource | Type | Count | Purpose |
|----------|------|-------|---------|
| SEM_ENTRY_GATES | Semaphore | 4 | Entry gate capacity |
| SEM_LOWER_STATION | Semaphore | N | Waiting room capacity |
| SEM_PLATFORM_GATES | Semaphore | 3 | Platform gate capacity |
| SEM_CHAIRS | Semaphore | 36 | Available chairs |
| SEM_EXIT_GATES | Semaphore | 2 | Exit gate capacity |
| MQ_CASHIER | Message Queue | - | Ticket purchases |
| MQ_PLATFORM | Message Queue | - | Boarding requests (VIP priority) |
| MQ_BOARDING | Message Queue | - | Boarding confirmations |
| MQ_ARRIVALS | Message Queue | - | Arrival notifications |

---

## Key Files

| File | Purpose |
|------|---------|
| [main.c](src/main.c) | Main orchestration, signal handling, process spawning |
| [tourist_generator.c](src/tourist_generator.c) | Tourist process spawning |
| [tourist_main.c](src/tourist_main.c) | Tourist lifecycle, ride loop |
| [cashier.c](src/cashier.c) | Ticket sales |
| [lower_worker.c](src/lower_worker.c) | Chair boarding management |
| [upper_worker.c](src/upper_worker.c) | Arrival tracking |
| [ipc.c](src/ipc.c) | IPC creation and operations |
| [types.h](include/types.h) | Shared data structures |
