# Ropeway Simulation (C11 + System V IPC)
Multi-process chairlift/ropeway simulation using C11 and System V IPC (msg/sem/shm) on Linux.

## Build & Run
```bash
cd ropeway-simulation
mkdir -p build && cd build
cmake ..
cmake --build .

# Run with default config
./ropeway_simulation

# Run with specific config
./ropeway_simulation test1_capacity.conf

# Save logs to file (colored output preserved)
./ropeway_simulation 2>&1 | tee simulation.log

# Save logs without colors (for later analysis)
./ropeway_simulation > simulation.log 2>&1
```

Config files are located in `../config/` relative to the binary.

## Running Tests
```bash
# Run all tests
cd tests
./run_all.sh

# Run by category
./run_all.sh integration  # Tests 1-4: Basic functionality
./run_all.sh stress       # Tests 5-8: High load scenarios
./run_all.sh edge         # Tests 9-13: Edge cases
./run_all.sh recovery     # Tests 14-16: Crash recovery
./run_all.sh signal       # Tests 17-18, 20: Signal handling
./run_all.sh sync         # Test 19: Synchronization

# Run individual test
cd build
bash ../tests/test1_capacity.sh
```

## Core Concepts

The simulation demonstrates classical Unix IPC patterns through a real-world scenario:

**Process Model** — Each logical component runs as a separate process created via the `fork()`+`exec()` pattern. Seven distinct process types cooperate: the orchestrating Main process, a dedicated TimeServer for clock management, Cashier for ticket sales, two platform Workers (lower/upper), a TouristGenerator, and individual Tourist processes.

**Inter-Process Communication** — All coordination happens through System V primitives:
- Five message queues handle request-response flows between tourists, cashier, and workers
- A shared memory segment stores global simulation state accessible to all processes
- Ten semaphores control concurrent access to station capacity, chairlift slots, entry/exit gates, and emergency coordination

**Signal-Driven Events** — The system responds to various signals for different purposes:
- `SIGUSR1`/`SIGUSR2` coordinate emergency stop and resume between workers
- `SIGTSTP`/`SIGCONT` (Ctrl+Z / fg) pause and resume the entire simulation with proper time offset tracking
- `SIGALRM` triggers periodic checks and timeouts
- `SIGCHLD` is handled by a dedicated zombie reaper thread via `sigwait()` to reap terminated children

**Defensive Design** — Every system call checks for errors and handles `EINTR` interrupts. IPC resources are cleaned up on both graceful shutdown and crash recovery. All user-provided configuration values are validated before use.

## Technical Constraints

| Aspect | Requirement |
|--------|-------------|
| Language | C11 standard, compiled with CMake 3.18+ |
| Dependencies | Standard library, pthreads, System V IPC only |
| IPC | Exclusively System V (no POSIX semaphores/queues) |
| Process creation | `fork()` + `exec()` for workers, no process pools |
| Threading | Limited to kid simulation within Tourist and zombie reaper in Main |
| Permissions | All IPC objects use `0600` mode |
| Cleanup | Resources removed via `IPC_RMID` on shutdown; `ipcs` empty after exit |

## Simulation Flow

The chairlift operates through a chain of coordinated steps:

1. **Initialization** — Main generates IPC keys with `ftok()`, creates all resources ([ipc_create](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L82-L125)), then spawns worker processes ([process spawning](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L201-L225))

2. **Time Management** — TimeServer atomically updates simulated clock every 10ms ([update_sim_time](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L137-L165)), compensating for any shell-level pauses ([pause handling](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L50-L78))

3. **Tourist Generation** — Generator creates tourist processes with randomized attributes (age, type, VIP status) via `fork()`+`execl()` ([tourist_generator_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L145-L294))

4. **Ticket Purchase** — Each tourist sends a request to the Cashier queue and waits for a response with pricing and validity ([cashier_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L120-L252))

5. **Station Entry** — Tourist acquires entry gate and station capacity semaphores, then joins the platform queue ([tourist lifecycle](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c#L24-L258))

6. **Boarding** — LowerWorker collects tourists into chairs (max 4 slots), acquires a chair semaphore, and dispatches ([dispatch_chair](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L101-L141), [lower_worker_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L153-L341))

7. **Arrival** — UpperWorker tracks arrivals per chair and releases the chair semaphore when all passengers have disembarked ([upper_worker_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c#L142-L268))

8. **Trail Descent** — Tourist simulates walking or biking down, then either re-enters for another ride or exits when ticket expires

9. **Emergency Protocol** — When danger is detected, workers coordinate via SIGUSR1/SIGUSR2 signals plus message queue handshake to safely halt and resume operations ([worker_trigger_emergency_stop](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L46-L79), [worker_initiate_resume](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L137-L196))

## Process Architecture

| Process | File | Purpose |
|---------|------|---------|
| Main | [src/main.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c) | Orchestrator: IPC creation, worker spawning, signal handling, zombie reaping |
| TimeServer | [src/processes/time_server.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c) | Atomic time updates, SIGTSTP/SIGCONT pause offset |
| Cashier | [src/processes/cashier.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c) | Ticket sales with age discounts and VIP surcharges |
| LowerWorker | [src/processes/lower_worker.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c) | Lower platform boarding management |
| UpperWorker | [src/processes/upper_worker.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c) | Upper platform arrivals and chair release |
| TouristGenerator | [src/processes/tourist_generator.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c) | Spawns tourist processes via fork+exec |
| Tourist | [src/tourist/main.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c) | Individual tourist lifecycle (buy ticket, ride, descend) |

## IPC Reference

### Shared Memory ([include/ipc/shared_state.h](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/shared_state.h))
- **SharedState** structure with flexible array member for per-tourist tracking
- Atomic `current_sim_time_ms` updated by TimeServer ([line 47](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/shared_state.h#L47))
- Global flags: `running`, `closing`, `emergency_stop` ([lines 50-53](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/shared_state.h#L50-L53))
- Statistics: `total_tourists`, `total_rides`, `rides_by_ticket[]` ([lines 56-59](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/shared_state.h#L56-L59))
- Process PIDs for signal handling ([lines 91-96](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/shared_state.h#L91-L96))

### Semaphores ([include/constants.h#L28-L38](https://github.com/Enjot/ropeway-simulation/blob/main/include/constants.h#L28-L38))
| Index | Name | Purpose | Initial Value |
|-------|------|---------|---------------|
| 0 | SEM_STATE | Mutex for SharedState | 1 |
| 1 | SEM_STATS | Mutex for statistics | 1 |
| 2 | SEM_ENTRY_GATES | Entry gate slots | 4 |
| 3 | SEM_EXIT_GATES | Exit gate slots | 2 |
| 4 | SEM_LOWER_STATION | Station capacity | config |
| 5 | SEM_CHAIRS | Chair availability | 36 |
| 6 | SEM_WORKER_READY | Startup barrier | 0 |
| 7 | SEM_PLATFORM_GATES | Platform gates | 3 |
| 8 | SEM_EMERGENCY_CLEAR | Emergency release | 0 |
| 9 | SEM_EMERGENCY_LOCK | Emergency mutex | 1 |

**Semaphore operations**: [src/ipc/sem.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c)
- [sem_wait](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L84-L99) - Atomic wait (decrement) by count
- [sem_wait_pauseable](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L106-L125) - Wait with EINTR retry loop
- [sem_post](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L131-L146) - Atomic post (increment) by count
- [sem_trywait](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L148-L163) - Non-blocking acquire
- [sem_getval](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L165-L171) - Read current value

### Message Queues ([include/ipc/messages.h](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/messages.h))
| Queue | ID | Direction | Message Type |
|-------|----|-----------|----|
| MQ_CASHIER | 1 | Tourist ↔ Cashier | [CashierMsg](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/messages.h#L17-L26) |
| MQ_PLATFORM | 2 | Tourist → LowerWorker | [PlatformMsg](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/messages.h#L31-L41) |
| MQ_BOARDING | 3 | LowerWorker → Tourist | [PlatformMsg](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/messages.h#L31-L41) |
| MQ_ARRIVALS | 4 | Tourist → UpperWorker | [ArrivalMsg](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/messages.h#L46-L53) |
| MQ_WORKER | 5 | Worker ↔ Worker | [WorkerMsg](https://github.com/Enjot/ropeway-simulation/blob/main/include/ipc/messages.h#L58-L61) |

**VIP Priority**: Regular tourists use `mtype=2`, VIPs use `mtype=1`; `msgrcv` with `-2` retrieves lowest mtype first (VIPs first).

## Signal Handling

### Signal handlers
- **Main**: [src/lifecycle/process_signals.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/lifecycle/process_signals.c) - SIGTERM/SIGINT, SIGALRM
- **Zombie Reaper**: [src/lifecycle/zombie_reaper.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/lifecycle/zombie_reaper.c) - SIGCHLD via `sigwait()` in dedicated thread
- **TimeServer**: [src/processes/time_server.c#L49-L97](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L49-L97) - SIGTSTP, SIGCONT, SIGTERM, SIGALRM
- **Workers**: [include/common/signal_common.h](https://github.com/Enjot/ropeway-simulation/blob/main/include/common/signal_common.h) - Macro-generated handlers for SIGUSR1/SIGUSR2/SIGALRM

### Signal usage
| Signal | Purpose |
|--------|---------|
| SIGCHLD | Zombie reaping via dedicated thread (`sigwait()`) |
| SIGTERM/SIGINT | Graceful shutdown |
| SIGALRM | Periodic time checks, emergency timeouts |
| SIGUSR1 | Emergency stop notification |
| SIGUSR2 | Resume notification |
| SIGTSTP | Pause simulation (shell Ctrl+Z) |
| SIGCONT | Resume simulation (shell `fg`) |

## Function Documentation

### Main Process ([src/main.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c))

#### [`shutdown_workers`](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L45-L101)
Signal workers to stop and destroy IPC to unblock blocked operations.

#### [`main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L103-L275)
Entry point: load config, block SIGCHLD, create IPC, start zombie reaper thread, spawn workers, run main loop.

---

### IPC Management ([src/ipc/ipc.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c))

#### [`ipc_cleanup_stale`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L24-L80)
Clean up stale IPC resources from a previous crashed run. Checks if shared memory exists and if the stored `main_pid` is dead.
- **Parameters**: `keys` - IPC keys to check
- **Returns**: 1 if stale resources cleaned, 0 if no stale resources, -1 on error

#### [`ipc_create`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L82-L125)
Create all IPC resources (shared memory, semaphores, message queues).
- **Parameters**: `res` - IPC resources struct to populate, `keys` - IPC keys, `cfg` - configuration
- **Returns**: 0 on success, -1 on error

#### [`ipc_attach`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L127-L147)
Attach child process to existing IPC resources.
- **Parameters**: `res` - IPC resources struct to populate, `keys` - IPC keys
- **Returns**: 0 on success, -1 on error

#### [`ipc_destroy`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L153-L165)
Destroy all IPC resources (message queues, semaphores, shared memory).
- **Parameters**: `res` - IPC resources to destroy

#### [`ipc_cleanup_signal_safe`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L172-L181)
Signal-safe IPC cleanup for use in signal handlers. Only uses async-signal-safe syscalls (`msgctl`, `semctl`, `shmctl`). Cleanup order: MQ → Semaphores → SHM (unblocks waiting processes first).
- **Parameters**: `res` - IPC resources to clean up

---

### Semaphore Operations ([src/ipc/sem.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c))

#### [`ipc_sem_create`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L21-L53)
Create and initialize semaphore set with configured values.
- **Parameters**: `res` - IPC resources, `key` - semaphore key, `cfg` - configuration
- **Returns**: 0 on success, -1 on error

#### [`sem_wait`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L84-L99)
Atomically wait (decrement) a semaphore by count. Blocks until count slots are available, then acquires all at once.
- **Parameters**: `sem_id` - semaphore set ID, `sem_num` - semaphore index, `count` - number of slots to acquire
- **Returns**: 0 on success, -1 on error/interrupt

#### [`sem_wait_pauseable`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L106-L125)
Semaphore wait with EINTR handling. Kernel handles SIGTSTP/SIGCONT automatically (process gets suspended). Only returns -1 on shutdown (EIDRM) or other errors.
- **Parameters**: `res` - IPC resources, `sem_num` - semaphore index, `count` - number of slots
- **Returns**: 0 on success, -1 on shutdown/error

#### [`sem_post`](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L131-L146)
Atomically post (increment) a semaphore by count. Releases count slots at once.
- **Parameters**: `sem_id` - semaphore set ID, `sem_num` - semaphore index, `count` - number of slots to release
- **Returns**: 0 on success, -1 on error

---

### Time Server ([src/processes/time_server.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c))

#### [`sigtstp_handler`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L50-L61)
SIGTSTP handler - capture pause start time and suspend. Uses SA_RESETHAND so handler is automatically reset to SIG_DFL before this handler runs. After capture, raises SIGTSTP again to actually suspend the process.
- **Parameters**: `sig` - signal number (unused)

#### [`sigcont_handler`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L71-L78)
SIGCONT handler - signal that pause has ended. Reinstalls the SIGTSTP handler (sigaction is async-signal-safe). Actual pause offset calculation happens in `handle_resume()`.
- **Parameters**: `sig` - signal number (unused)

#### [`handle_resume`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L106-L127)
Handle pause offset calculation after SIGCONT. Called outside signal handler context to safely calculate the pause duration and update the total pause offset.

#### [`update_sim_time`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L137-L165)
Update the atomic simulated time in SharedState. Calculates current simulated time accounting for pause offsets and stores it atomically for other processes to read.
- **Parameters**: `state` - shared state to update

#### [`time_server_main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L177-L258)
Time Server process entry point. Maintains the current simulated time with sub-millisecond precision. Handles SIGTSTP/SIGCONT pause tracking and offset calculation. Atomically updates `SharedState.current_sim_time_ms` for other processes.
- **Parameters**: `res` - IPC resources (shared memory for time updates), `keys` - IPC keys (unused)

---

### Cashier ([src/processes/cashier.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c))

#### [`calculate_ticket_validity`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L31-L53)
Calculate ticket expiration time in simulated minutes.
- **Parameters**: `state` - shared state with ticket duration settings, `ticket` - type of ticket being purchased
- **Returns**: Expiration time in simulated minutes from midnight

#### [`calculate_price`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L65-L83)
Calculate ticket price for one person. Age discounts: under 10 years or 65+ get 25% off.
- **Parameters**: `age` - tourist age in years, `ticket` - type of ticket being purchased, `is_vip` - whether tourist has VIP status (adds surcharge)
- **Returns**: Price in PLN

#### [`calculate_family_price`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L97-L109)
Calculate total family ticket price. Parent and kids all get the same ticket type. Kids (4-7 years old) always get the under-10 discount.
- **Parameters**: `parent_age` - parent's age in years, `ticket` - type of ticket for the family, `is_vip` - whether family has VIP status, `kid_count` - number of children (0-2)
- **Returns**: Total price in PLN for entire family

#### [`cashier_main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L120-L252)
Cashier process entry point. Handles ticket sales via message queue. Calculates prices with age discounts and VIP surcharges. Runs until station closes or shutdown signal received.
- **Parameters**: `res` - IPC resources (message queues, semaphores, shared memory), `keys` - IPC keys (unused)

---

### Lower Worker ([src/processes/lower_worker.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c))

#### [`check_for_danger`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L65-L88)
Check for random danger and trigger emergency stop if detected. Uses pause-adjusted time for cooldown calculation.
- **Parameters**: `res` - IPC resources for emergency coordination
- **Returns**: 1 if danger was detected, 0 otherwise

#### [`dispatch_chair`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L101-L141)
Dispatch the current chair with all buffered tourists. Acquires a chair slot, then sends boarding confirmations to all buffered tourists with the same `departure_time` so they arrive together. The `upper_worker` releases the chair slot when all tourists have arrived.
- **Parameters**: `res` - IPC resources for semaphores and message queues, `chair_number` - chair identifier for logging, `slots_used` - total slots used on this chair

#### [`lower_worker_main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L153-L341)
Lower platform worker process entry point. Manages tourist boarding onto chairlift. Buffers tourists until chair is full or queue is empty, then dispatches. Handles emergency stops and random danger detection.
- **Parameters**: `res` - IPC resources (message queues, semaphores, shared memory), `keys` - IPC keys (unused)

---

### Upper Worker ([src/processes/upper_worker.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c))

#### [`get_chair_tracker`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c#L100-L117)
Find or create a tracker for a chair.
- **Parameters**: `chair_id` - chair identifier to find or create tracker for, `tourists_on_chair` - expected number of tourists on this chair
- **Returns**: Pointer to tracker, or NULL if tracking array is full

#### [`upper_worker_main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c#L142-L268)
Upper platform worker process entry point. Processes tourist arrivals at the upper platform. Tracks chairs and releases chair slots when all tourists from a chair have arrived. Handles emergency stops and random danger detection.
- **Parameters**: `res` - IPC resources (message queues, semaphores, shared memory), `keys` - IPC keys (unused)

---

### Tourist Generator ([src/processes/tourist_generator.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c))

#### [`generate_age`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L65-L81)
Generate random age (8-80) and check if person can have kids. Adults 26+ can be guardians for kids aged 4-7.
- **Parameters**: `can_have_kids` - output: 1 if person can be a guardian, 0 otherwise
- **Returns**: Generated age in years

#### [`generate_kid_count`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L91-L95)
Generate number of kids for a family (1-2). Distribution: ~63% one kid, ~37% two kids. Only called when `family_percentage` check already passed.
- **Returns**: Number of children (1 or 2)

#### [`select_ticket_type`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L115-L122)
Select ticket type for a tourist. Distribution: 30% single, 20% T1, 20% T2, 15% T3, 15% daily.
- **Returns**: Random ticket type

#### [`tourist_generator_main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L145-L294)
Tourist generator process entry point. Spawns tourist processes with random attributes (age, type, VIP status, ticket type, kids). Uses fork+exec to create tourist processes. Waits for all spawned tourists to exit before returning.
- **Parameters**: `res` - IPC resources (shared memory for config values), `keys` - IPC keys (unused), `tourist_exe` - path to tourist executable

---

### Tourist ([src/tourist/main.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c))

#### [`main`](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c#L24-L258)
Tourist process entry point. Handles complete tourist lifecycle: ticket purchase, ride loop (enter station, board chair, ride, descend trail), and exit when ticket expires.

---

### Emergency Coordination ([src/common/worker_emergency.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c))

#### [`worker_trigger_emergency_stop`](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L46-L79)
Initiate emergency stop. Attempts to acquire `SEM_EMERGENCY_LOCK` to become the initiator. If lock acquired, sets `emergency_stop` flag and signals the other worker via SIGUSR1. If lock not acquired, falls back to receiver role.
- **Parameters**: `res` - IPC resources, `role` - worker role (WORKER_LOWER or WORKER_UPPER), `state` - emergency state tracking

#### [`worker_acknowledge_emergency_stop`](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L81-L135)
Acknowledge emergency stop from the other worker. Sets `emergency_stop` flag, then blocks waiting for resume message via `MQ_WORKER`. After resume received, signals ready and clears emergency flag.
- **Parameters**: `res` - IPC resources, `role` - worker role, `state` - emergency state tracking

#### [`worker_initiate_resume`](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L137-L196)
Resume operations after emergency cooldown passed. Sends `READY_TO_RESUME` message to other worker, waits for `I_AM_READY` response, sends SIGUSR2, clears emergency flag, and releases `SEM_EMERGENCY_LOCK`.
- **Parameters**: `res` - IPC resources, `role` - worker role, `state` - emergency state tracking

---

### Logger ([src/core/logger.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c))

#### [`logger_init`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L30-L34)
Initialize logger with shared state and component type for colored output.
- **Parameters**: `state` - shared state (for simulation time), `comp` - component type enum

#### [`log_msg`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L64-L106)
Formatted log output with simulation timestamp, level, and component tag. Uses colors based on component type when output is a terminal.
- **Parameters**: `level` - log level string, `component` - component name, `fmt` - printf-style format string

#### [`log_signal_safe`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L108-L112)
Async-signal-safe logging using only `write()`. For use in signal handlers.
- **Parameters**: `msg` - message string to write

#### [`int_to_str`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L114-L149)
Signal-safe integer to string conversion. Does not use any non-async-signal-safe functions.
- **Parameters**: `n` - integer to convert, `buf` - output buffer, `buf_size` - buffer size

---

### Configuration ([src/core/config.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c))

#### [`config_set_defaults`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c#L17-L46)
Initialize configuration with default values.
- **Parameters**: `cfg` - configuration structure to initialize

#### [`config_load`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c#L57-L149)
Load configuration from a file. Sets defaults first, then overrides with file values.
- **Parameters**: `path` - path to the configuration file, `cfg` - configuration structure to populate
- **Returns**: 0 on success, -1 on error

#### [`config_validate`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c#L159-L241)
Validate configuration values. Prints error messages for invalid values to stderr.
- **Parameters**: `cfg` - configuration structure to validate
- **Returns**: 0 if valid, -1 if invalid

---

### Report ([src/core/report.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/report.c))

#### [`write_report_to_file`](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/report.c#L13-L67)
Write final simulation summary to file including duration, total tourists, total rides, per-tourist breakdown, and aggregates by ticket type. Report is saved to `simulation_report.txt`.
- **Parameters**: `state` - shared state with simulation statistics, `filepath` - output file path
- **Returns**: 0 on success, -1 on error

## Configuration Parameters

Config file format: `KEY=VALUE` with `#` comments.

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `STATION_CAPACITY` | 50 | Max tourists in lower station |
| `SIMULATION_DURATION_REAL_SECONDS` | 120 | Real time duration |
| `SIM_START_HOUR`/`SIM_START_MINUTE` | 8:00 | Simulated start time |
| `SIM_END_HOUR`/`SIM_END_MINUTE` | 17:00 | Simulated end time |
| `CHAIR_TRAVEL_TIME_SIM_MINUTES` | 1 | Chair ride duration (sim minutes) |
| `TOTAL_TOURISTS` | 100 | Tourists to generate (must be > 0) |
| `TOURIST_SPAWN_DELAY_US` | 10000 | Spawn delay (microseconds) |
| `VIP_PERCENTAGE` | 1 | VIP tourist percentage |
| `WALKER_PERCENTAGE` | 50 | Walker vs cyclist ratio |
| `TRAIL_WALK_TIME_SIM_MINUTES` | 2 | Walking trail duration (sim minutes) |
| `TRAIL_BIKE_*_TIME_SIM_MINUTES` | 1/2/3 | Bike trail durations (fast/med/slow) |
| `TICKET_T*_DURATION_SIM_MINUTES` | 60/120/180 | Time ticket durations |
| `DANGER_PROBABILITY` | 0 | Emergency detection (0-100) |
| `DANGER_DURATION_SIM_MINUTES` | 30 | Emergency duration |
| `DEBUG_LOGS_ENABLED` | 1 | Show debug logs |

## Constants ([include/constants.h](https://github.com/Enjot/ropeway-simulation/blob/main/include/constants.h))

| Constant | Value | Purpose |
|----------|-------|---------|
| `CHAIR_CAPACITY` | 4 | Max slots per chair |
| `MAX_CHAIRS_IN_TRANSIT` | 36 | Concurrent chairs |
| `TOTAL_CHAIRS` | 72 | Total chairs in system |
| `ENTRY_GATES` | 4 | Entry gate count |
| `EXIT_GATES` | 2 | Exit gate count |
| `PLATFORM_GATES` | 3 | Platform gate count |
| `MAX_KIDS_PER_ADULT` | 2 | Max children per guardian |

## Enums ([include/constants.h#L70-L114](https://github.com/Enjot/ropeway-simulation/blob/main/include/constants.h#L70-L114))

**TouristType**: `TOURIST_WALKER` (0), `TOURIST_CYCLIST` (1), `TOURIST_FAMILY` (2)

**TicketType**: `TICKET_SINGLE` (0), `TICKET_TIME_T1` (1), `TICKET_TIME_T2` (2), `TICKET_TIME_T3` (3), `TICKET_DAILY` (4)

**TouristStage**: 10 lifecycle stages from `STAGE_AT_CASHIER` (0) to `STAGE_LEAVING` (9)

## Logger Colors ([src/core/logger.c#L17-L28](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L17-L28))

| Component | Color |
|-----------|-------|
| TOURIST | Bright green |
| VIP | Bright red |
| CASHIER | Yellow |
| LOWER_WORKER | Blue |
| UPPER_WORKER | Cyan |
| GENERATOR | Magenta |
| MAIN | White |
| IPC | Gray |
| TIME_SERVER | Bright yellow |

## Test Suite Details

### Integration Tests (1-4)

#### [test1_capacity.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test1_capacity.sh) - Lower Station Capacity Limit
- **Goal**: Station visitor count never exceeds N
- **Rationale**: Tests for race condition between 4 entry gates incrementing station count via `SEM_LOWER_STATION`. Without proper semaphore synchronization, concurrent `sem_wait()` calls could allow count > N briefly.
- **Parameters**: `station_capacity=5`, `tourists=15`, `simulation_time=1s`
- **Expected**: Max observed count ≤ 5. No zombies. IPC cleaned.

#### [test2_children.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test2_children.sh) - Children Under 8 with Guardians
- **Goal**: Children board with guardians, max 2 kids per adult
- **Rationale**: Tests atomic multi-slot semaphore acquisition for families. `sem_wait_n(SEM_LOWER_STATION, family_size)` must be atomic - partial acquisition would split family or cause deadlock if slots < family_size.
- **Parameters**: `walker_percentage=80`, `family_percentage=80`, `tourists=15`, `simulation_time=1s`
- **Expected**: Families board together. No adult has >2 kids. No deadlock.

#### [test3_vip.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test3_vip.sh) - VIP Priority Without Queue Starvation
- **Goal**: VIPs skip entry gates while regular tourists wait in queue
- **Rationale**: VIPs bypass `SEM_ENTRY_GATES` entirely, reaching the platform faster when gates are congested. Verifies VIPs log "skipped gate queue" while regulars log "entered through gate", and both groups are served.
- **Parameters**: `vip_percentage=30`, `tourists=30`, `simulation_time=1s`
- **Expected**: VIPs skip gates, regulars enter gates, both groups served.

#### [test4_emergency.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test4_emergency.sh) - Emergency Stop and Resume (Signals)
- **Goal**: SIGUSR1 stops chairlift, SIGUSR2 resumes after worker confirmation
- **Rationale**: Tests signal handler coordination via `SEM_EMERGENCY_LOCK` and `SEM_EMERGENCY_CLEAR`. Race condition possible if both workers try to initiate emergency simultaneously. Resume requires both workers to signal ready.
- **Parameters**: SIGUSR1/SIGUSR2 injection with 0.5s delays, `simulation_time=3s`
- **Expected**: Emergency stop logged. Resume only after both workers ready.

### Stress Tests (5-8)

#### [test5_stress.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test5_stress.sh) - High Throughput Stress Test
- **Goal**: Simulation completes without timeout under high concurrency
- **Rationale**: Tests for deadlock between `MQ_CASHIER`, `MQ_PLATFORM`, `MQ_BOARDING` under load. Concurrent tourists stress all semaphores and message queues simultaneously - exposes circular wait conditions or semaphore exhaustion.
- **Parameters**: `tourists=6000`, `station_capacity=500`, `simulation_time=180s`, rapid spawn (no delay)
- **Expected**: No timeout (deadlock). No zombies. IPC cleaned.

#### [test6_race.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test6_race.sh) - Entry Gate Race Condition Test
- **Goal**: Capacity N=5 never exceeded across 10 iterations
- **Rationale**: Tests TOCTOU race in `SEM_LOWER_STATION`. Multiple tourists calling `sem_wait()` simultaneously could interleave check-and-decrement operations. 10 iterations increases probability of catching intermittent race.
- **Parameters**: `tourists=10`, `N=5`, `simulation_time=3s`, 10 iterations
- **Expected**: Count ≤ 5 in all iterations. 100% pass rate.

#### [test7_emergency_race.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test7_emergency_race.sh) - Emergency Race Condition Test
- **Goal**: Only one worker initiates emergency when both detect danger
- **Rationale**: Tests race on `SEM_EMERGENCY_LOCK` when lower_worker and upper_worker both call `sem_trywait()` simultaneously. Double-initiation would corrupt emergency state or cause deadlock waiting on `SEM_EMERGENCY_CLEAR`.
- **Parameters**: `danger_probability=100`, `tourists=10`, `simulation_time=3s`
- **Expected**: Single initiator per emergency. No deadlock. System recovers.

#### [test8_chair_saturation.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test8_chair_saturation.sh) - Chair Saturation Test
- **Goal**: Tourists block on `SEM_CHAIRS` when all 36 chairs are in transit
- **Rationale**: Tests semaphore blocking behavior when `SEM_CHAIRS` reaches zero. Fast boarding + slow travel saturates chairs. Verifies `sem_wait()` blocks correctly and `sem_post()` from arrivals unblocks waiting tourists.
- **Parameters**: `tourists=20`, `station_capacity=50`, `simulation_time=3s`
- **Expected**: Max 36 chairs in transit. No deadlock on chair exhaustion.

### Edge Case Tests (9-13)

#### [test9_zero.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test9_zero.sh) - Zero Tourists Edge Case
- **Goal**: Graceful rejection of invalid configuration (0 tourists)
- **Rationale**: Tests config validation. `TOTAL_TOURISTS=0` is rejected at startup with clear error message. Verifies no IPC resources are leaked when startup fails early.
- **Parameters**: `tourists=0`, `simulation_time=1s`
- **Expected**: Config rejected gracefully. No IPC leaks from failed startup.

#### [test10_single.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test10_single.sh) - Single Tourist Edge Case
- **Goal**: One tourist completes full lifecycle without concurrency
- **Rationale**: Baseline test - verifies message passing chain works: tourist→MQ_CASHIER→cashier→MQ_PLATFORM→lower_worker→MQ_BOARDING→tourist→MQ_ARRIVALS→upper_worker. No concurrency masks IPC bugs.
- **Parameters**: `tourists=1`, walker, `simulation_time=1s`
- **Expected**: Tourist completes ride. All IPC handoffs succeed.

#### [test11_capacity_one.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test11_capacity_one.sh) - Capacity One Edge Case
- **Goal**: Only 1 tourist in station at any time with N=1
- **Rationale**: Tests convoy effect on `SEM_LOWER_STATION`. With N=1, all tourists serialize on single semaphore slot. Potential for priority inversion or starvation if `sem_wait()` ordering is unfair.
- **Parameters**: `N=1`, `tourists=10`, `simulation_time=1s`
- **Expected**: Max count=1. All tourists eventually served. No deadlock.

#### [test12_all_vip.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test12_all_vip.sh) - All VIPs Edge Case
- **Goal**: All tourists served when everyone has same mtype=1 priority
- **Rationale**: Tests `msgrcv()` behavior when all messages have identical mtype. Verifies FIFO ordering within same priority level. Edge case where priority differentiation provides no benefit - system must still function.
- **Parameters**: `vip_percentage=100`, `tourists=10`, `simulation_time=1s`
- **Expected**: FIFO order maintained. All tourists served. No starvation.

#### [test13_all_families.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test13_all_families.sh) - All Families Edge Case
- **Goal**: Families board atomically without splitting
- **Rationale**: Tests `semop()` with `sem_op > 1` for atomic multi-slot acquisition. Family of 3 must acquire 3 slots atomically - partial acquisition would split family or deadlock if remaining capacity < family_size.
- **Parameters**: `walker_percentage=100`, `family_percentage=100`, `tourists=10`, `simulation_time=1s`
- **Expected**: No split families. Atomic acquisition verified. No deadlock.

### Recovery Tests (14-16)

#### [test14_sigterm.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test14_sigterm.sh) - SIGTERM Cleanup Test
- **Goal**: No orphaned IPC resources after SIGTERM shutdown
- **Rationale**: Tests `ipc_cleanup()` is called from SIGTERM handler. Semaphores, shared memory, and message queues must be released via `semctl(RMID)`, `shmctl(RMID)`, `msgctl(RMID)`. Leaked IPC persists until reboot.
- **Parameters**: Uses test1_capacity.conf (1s, 15 tourists), SIGTERM mid-run
- **Expected**: ipcs empty after shutdown. No zombies. All children terminated.

#### [test15_sigkill_recovery.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test15_sigkill_recovery.sh) - SIGKILL Recovery Test
- **Goal**: New run cleans orphaned IPC from previous crash
- **Rationale**: SIGKILL bypasses signal handlers - no `ipc_cleanup()` runs. Tests `ipc_cleanup_stale()` at startup: must detect orphaned resources via `ftok()` key collision and remove before creating new IPC objects.
- **Parameters**: Uses test1_capacity.conf, SIGKILL first run, start second run
- **Expected**: Second run succeeds. Stale IPC cleaned. Log shows cleanup.

#### [test16_child_death.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test16_child_death.sh) - Child Death Test
- **Goal**: Main process shuts down gracefully when child dies unexpectedly
- **Rationale**: Tests SIGCHLD handler and zombie reaper thread. Killing cashier mid-run must trigger: `waitpid()` reaps child, main detects worker death, initiates shutdown. Without proper handling: zombies or orphaned children.
- **Parameters**: Uses test1_capacity.conf, kill cashier mid-simulation
- **Expected**: Main detects death. Graceful shutdown. No zombies or IPC leaks.

### Signal Tests (17-18)

#### [test17_pause_resume.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test17_pause_resume.sh) - Pause/Resume Test (SIGTSTP/SIGCONT)
- **Goal**: Simulated time offset adjusts for pause duration
- **Rationale**: Tests SIGTSTP/SIGCONT handlers in time_server. Pause duration must be added to time_offset so accelerated simulation time doesn't jump. Without offset adjustment: tickets expire during pause, time discontinuity.
- **Parameters**: Uses test1_capacity.conf (1s simulation), SIGTSTP then SIGCONT
- **Expected**: Time continues smoothly after resume. No expired tickets.

#### [test18_rapid_signals.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test18_rapid_signals.sh) - Rapid Signals Test
- **Goal**: No crash under rapid signal delivery to worker processes
- **Rationale**: Tests signal handler reentrancy in workers. Rapid SIGUSR1 can interrupt handler mid-execution. Main process does not handle SIGUSR1 (default action terminates), so signals are sent to child worker processes. Non-async-signal-safe functions in handler cause undefined behavior.
- **Parameters**: 10 SIGUSR1 signals with 50ms delay, sent to child process
- **Expected**: No segfault. No hang. Simulation survives signal storm.

### Sync Correctness Tests (19)

#### [test19_sigalrm_sync.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test19_sigalrm_sync.sh) - SIGALRM Sync Test
- **Goal**: Verify simulation works with SIGALRM-based sync (no usleep polling)
- **Rationale**: Confirms refactored code uses blocking IPC + `alarm()` correctly. All `IPC_NOWAIT`+usleep polling has been replaced with blocking calls that use SIGALRM for periodic wakeup. EINTR handled properly on alarm interrupt.
- **Parameters**: `tourists=15`, `simulation_time=1s` (uses test1_capacity.conf)
- **Expected**: Simulation completes. No deadlock. No leftover IPC.

#### [test20_sigint_emergency.sh](https://github.com/Enjot/ropeway-simulation/blob/main/tests/test20_sigint_emergency.sh) - SIGINT During Emergency Stop
- **Goal**: Graceful shutdown when Ctrl+C is sent during emergency stop
- **Rationale**: Workers blocked on `msgrcv()` during emergency handshake must wake up on SIGINT (EINTR) and proceed with shutdown. Emergency lock (`SEM_EMERGENCY_LOCK`) must be released. IPC cleanup must complete even when chairlift is stopped.
- **Parameters**: `danger_probability=100`, `danger_duration=120min` (long duration ensures SIGINT hits during emergency)
- **Expected**: Clean shutdown within 15s. No zombies. No orphaned processes. No leftover IPC.

### Test Output
Tests check for:
- **Capacity violations**: Station count never exceeds configured limit
- **Zombie processes**: No defunct processes after shutdown
- **IPC cleanup**: `ipcs` shows no leftover resources
- **Log analysis**: Parses simulation logs for expected behavior

## Compliance Notes
- **C11 + CMake 3.18**: No external dependencies
- **System V IPC only**: No POSIX IPC
- **fork+exec**: All workers spawned via fork+exec pattern
- **ftok**: All IPC keys generated via ftok
- **Signal safety**: Only async-signal-safe functions in handlers
- **EINTR handling**: All blocking operations handle interrupts
- **IPC cleanup**: Resources destroyed on shutdown via `IPC_RMID`
- **No volatile/sig_atomic_t**: Uses `__atomic_*` builtins instead
- **Threads**: Only for kids/bikes within Tourist process and zombie reaper
