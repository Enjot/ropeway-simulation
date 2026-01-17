# 🚡 Ropeway Simulation

A multi-process simulation of a chairlift ropeway system using **System V IPC** mechanisms in C++.

This project demonstrates inter-process communication (IPC) concepts including shared memory, semaphores, and message queues in a realistic simulation scenario.

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Documentation](#documentation)
- [Project Structure](#project-structure)

## Overview

This simulation models a mountain chairlift system that serves tourists (pedestrians and cyclists). The system includes:

- **72 four-person chairs** (max 36 in use simultaneously)
- **Entry gates** for ticket validation
- **Boarding gates** controlled by workers
- **Two stations** (lower and upper) managed by workers
- **Emergency stop** mechanism with worker coordination

### Business Rules

| Passenger Type | Max per Chair |
|----------------|---------------|
| Pedestrians only | 4 |
| Cyclists only | 2 |
| Mixed (1 cyclist + pedestrians) | 1 + 2 = 3 |

## Features

- ✅ Multi-process architecture using `fork()` and `exec()`
- ✅ System V shared memory for state management
- ✅ System V semaphores for synchronization
- ✅ System V message queues for communication
- ✅ Signal handling (SIGTERM, SIGUSR1, SIGUSR2)
- ✅ Emergency stop/resume coordination between workers
- ✅ VIP priority queue support
- ✅ Ticket system with discounts (children, seniors)
- ✅ Trail descent simulation for cyclists
- ✅ Daily statistics and reporting

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        ORCHESTRATOR                             │
│                      (Parent Process)                           │
│  - Creates IPC resources                                        │
│  - Spawns child processes                                       │
│  - Manages simulation lifecycle                                 │
└─────────────────────────────────────────────────────────────────┘
        │ fork/exec
        ▼
┌───────────────┬───────────────┬───────────────┬─────────────────┐
│   CASHIER     │   WORKER 1    │   WORKER 2    │   TOURISTS      │
│               │ (Lower Stn)   │ (Upper Stn)   │   (1...N)       │
│ Sells tickets │ Controls      │ Monitors      │ Buy tickets,    │
│ via msg queue │ boarding      │ arrivals      │ ride chairs     │
└───────────────┴───────────────┴───────────────┴─────────────────┘
        │               │               │               │
        └───────────────┴───────────────┴───────────────┘
                        │
            ┌───────────┴───────────┐
            │   SHARED MEMORY       │
            │   (System State)      │
            │   + SEMAPHORES        │
            │   + MESSAGE QUEUES    │
            └───────────────────────┘
```

## Quick Start

### Prerequisites

- C++23 compiler (GCC, Clang)
- CMake 4.0+
- Unix-like OS (Linux, macOS)

### Building

```bash
mkdir build && cd build
cmake ..
cmake --build . -j
```

### Running

```bash
./main
```

### Cleanup

If the simulation crashes, clean up IPC resources:

```bash
# View existing IPC resources
ipcs

# Remove specific resources
ipcrm -M <shm_key>
ipcrm -S <sem_key>
ipcrm -Q <msg_key>
```

## Documentation

Detailed documentation is available in the `docs/` directory:

| Document | Description |
|----------|-------------|
| [Architecture](docs/ARCHITECTURE.md) | System design and component interactions |
| [IPC Mechanisms](docs/IPC_MECHANISMS.md) | Shared memory, semaphores, message queues |
| [Processes](docs/PROCESSES.md) | Process roles and responsibilities |
| [Signals](docs/SIGNALS.md) | Signal handling and emergency stops |
| [Building](docs/BUILDING.md) | Build instructions and dependencies |

## Project Structure

```
ropeway-simulation/
├── main.cpp                 # Entry point
├── CMakeLists.txt           # Build configuration
├── common/                  # Enums and configuration
│   ├── config.hpp           # Simulation parameters
│   ├── ropeway_state.hpp    # State machine states
│   └── ...
├── ipc/                     # IPC wrappers (RAII)
│   ├── SharedMemory.hpp     # Shared memory wrapper
│   ├── Semaphore.hpp        # Semaphore wrapper
│   ├── MessageQueue.hpp     # Message queue wrapper
│   └── IpcManager.hpp       # IPC resource manager
├── processes/               # Process implementations
│   ├── Orchestrator.hpp     # Main coordinator
│   ├── cashier_process.cpp  # Ticket sales
│   ├── worker1_process.cpp  # Lower station
│   ├── worker2_process.cpp  # Upper station
│   └── tourist_process.cpp  # Tourist behavior
├── structures/              # Data structures
│   ├── tourist.hpp          # Tourist state
│   ├── chair.hpp            # Chair management
│   └── ticket.hpp           # Ticket types
├── utils/                   # Utilities
│   ├── SignalHelper.hpp     # Signal handling
│   ├── ProcessSpawner.hpp   # Process creation
│   └── Logger.hpp           # Async-safe logging
├── gates/                   # Gate implementations
└── docs/                    # Documentation
```

## Key Concepts Demonstrated

1. **Multi-process Programming** - Using `fork()` and `exec()` for process creation
2. **System V IPC** - Shared memory, semaphores, and message queues
3. **Synchronization** - Avoiding race conditions with semaphores
4. **Signal Handling** - Graceful shutdown and emergency coordination
5. **RAII Pattern** - Resource management with C++ destructors
6. **State Machines** - Tourist and ropeway state management

## License

Educational project for IPC concepts demonstration.

## Author

Jakub Nahacz
