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
./run_all.sh signal       # Tests 17-18: Signal handling
./run_all.sh sync         # Test 19: Synchronization

# Run individual test
cd build
bash ../tests/test1_capacity.sh
```

## Assignment Highlights
- Multi-process pipeline: `fork()` + `exec()` per role (Main, TimeServer, Cashier, LowerWorker, UpperWorker, TouristGenerator, Tourist)
- System V IPC mix: message queues (cashier/platform/boarding/arrivals/worker), shared memory for state tracking, semaphores for capacity/gates/barriers
- Signals: `SIGUSR1`/`SIGUSR2` for emergency stop/resume; `SIGTSTP`/`SIGCONT` for pause handling; `SIGALRM` for time checks; `SIGCHLD` for zombie reaping
- Robustness: input validation, per-syscall error checks (`errno`, `EINTR`), permissions (`0666`), cleanup via `IPC_RMID`/`semctl(IPC_RMID)`/`shmctl(IPC_RMID)`
- Visibility: colored logging per component with accelerated simulated time display

## Environment & Limits
- Toolchain: C11 with CMake 3.18 (`cmake -S . -B build && cmake --build build`), no external deps beyond libc/pthreads/System V IPC
- Threading: Kids and bikes are pthreads within Tourist process (not separate processes); zombie reaper in main process
- IPC objects created with `0666` perms and removed at shutdown; `ipcs` should be empty after clean exit

## End-to-End Workflow (with permalinks)
- **Main** bootstraps IPC (`ftok`/`msgget`/`semget`/`shmget`) and spawns all workers via fork+exec; see [IPC creation](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L82-L125), [worker spawning](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L181-L212), and [main loop](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L216-L232)
- **TimeServer** maintains atomic `current_sim_time_ms` with pause offset tracking via `SIGTSTP`/`SIGCONT`: [update_sim_time](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L136-L164), [pause handling](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L49-L77)
- **Cashier** receives ticket requests via `MQ_CASHIER` and sends responses with calculated prices: [cashier_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L120-L249)
- **TouristGenerator** spawns tourists via `fork`/`execl` with random attributes: [tourist_generator_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L145-L294)
- **Tourist** acquires semaphores, sends messages to workers, rides chairlift, and descends trail: [main ride loop](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c#L107-L244)
- **LowerWorker** buffers tourists until chair is full, acquires `SEM_CHAIRS`, and dispatches: [dispatch_chair](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L101-L141), [lower_worker_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L153-L338)
- **UpperWorker** receives arrivals and releases `SEM_CHAIRS` when all tourists from a chair have arrived: [upper_worker_main](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c#L142-L265)
- **Emergency coordination** between workers via `SIGUSR1`/`SIGUSR2` + message queue handshake: [worker_trigger_emergency_stop](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L46-L79), [worker_initiate_resume](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L137-L196)

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
- **Main**: [src/lifecycle/process_signals.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/lifecycle/process_signals.c) - SIGCHLD, SIGTERM/SIGINT, SIGALRM
- **TimeServer**: [src/processes/time_server.c#L49-L97](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L49-L97) - SIGTSTP, SIGCONT, SIGTERM, SIGALRM
- **Workers**: [include/common/signal_common.h](https://github.com/Enjot/ropeway-simulation/blob/main/include/common/signal_common.h) - Macro-generated handlers for SIGUSR1/SIGUSR2/SIGALRM

### Signal usage
| Signal | Purpose |
|--------|---------|
| SIGCHLD | Zombie reaping in main |
| SIGTERM/SIGINT | Graceful shutdown |
| SIGALRM | Periodic time checks, emergency timeouts |
| SIGUSR1 | Emergency stop notification |
| SIGUSR2 | Resume notification |
| SIGTSTP | Pause simulation (shell Ctrl+Z) |
| SIGCONT | Resume simulation (shell `fg`) |

## Function Documentation

### Main Process ([src/main.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `main` | [100-254](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L100-L254) | Entry point: load config, create IPC, spawn workers, main loop |
| `shutdown_workers` | [42-98](https://github.com/Enjot/ropeway-simulation/blob/main/src/main.c#L42-L98) | Signal workers and destroy IPC to unblock operations |

### IPC Management ([src/ipc/ipc.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `ipc_cleanup_stale` | [24-80](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L24-L80) | Clean up orphaned IPC from crashed runs |
| `ipc_create` | [82-125](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L82-L125) | Create all IPC resources |
| `ipc_attach` | [127-147](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L127-L147) | Child process attach to IPC |
| `ipc_destroy` | [153-165](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L153-L165) | Destroy all IPC resources |
| `ipc_cleanup_signal_safe` | [172-181](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/ipc.c#L172-L181) | Signal-safe cleanup for handlers |

### Semaphore Operations ([src/ipc/sem.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `ipc_sem_create` | [21-53](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L21-L53) | Create and initialize semaphore set |
| `sem_wait` | [84-99](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L84-L99) | Atomic wait by count |
| `sem_wait_pauseable` | [106-125](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L106-L125) | Wait with EINTR handling |
| `sem_post` | [131-146](https://github.com/Enjot/ropeway-simulation/blob/main/src/ipc/sem.c#L131-L146) | Atomic post by count |

### Time Server ([src/processes/time_server.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `sigtstp_handler` | [49-60](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L49-L60) | Capture pause start time |
| `sigcont_handler` | [70-77](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L70-L77) | Signal pause ended |
| `handle_resume` | [105-126](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L105-L126) | Calculate pause offset |
| `update_sim_time` | [136-164](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L136-L164) | Atomic time update |
| `time_server_main` | [176-254](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/time_server.c#L176-L254) | Entry point |

### Cashier ([src/processes/cashier.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `calculate_ticket_validity` | [31-53](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L31-L53) | Ticket expiration time |
| `calculate_price` | [65-83](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L65-L83) | Price with age discounts |
| `calculate_family_price` | [97-109](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L97-L109) | Family ticket pricing |
| `cashier_main` | [120-249](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/cashier.c#L120-L249) | Entry point |

### Lower Worker ([src/processes/lower_worker.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `check_for_danger` | [65-88](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L65-L88) | Random danger detection |
| `dispatch_chair` | [101-141](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L101-L141) | Send boarding confirmations |
| `lower_worker_main` | [153-338](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/lower_worker.c#L153-L338) | Entry point |

### Upper Worker ([src/processes/upper_worker.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `get_chair_tracker` | [100-117](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c#L100-L117) | Track chair arrivals |
| `upper_worker_main` | [142-265](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/upper_worker.c#L142-L265) | Entry point |

### Tourist Generator ([src/processes/tourist_generator.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `generate_age` | [65-81](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L65-L81) | Random age with guardian eligibility |
| `generate_kid_count` | [90-95](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L90-L95) | 0-2 kids per adult |
| `select_ticket_type` | [115-122](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L115-L122) | Random ticket distribution |
| `tourist_generator_main` | [145-294](https://github.com/Enjot/ropeway-simulation/blob/main/src/processes/tourist_generator.c#L145-L294) | Entry point |

### Tourist ([src/tourist/main.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `main` | [24-258](https://github.com/Enjot/ropeway-simulation/blob/main/src/tourist/main.c#L24-L258) | Lifecycle: ticket purchase, ride loop, exit |

### Emergency Coordination ([src/common/worker_emergency.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `worker_trigger_emergency_stop` | [46-79](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L46-L79) | Initiate emergency stop |
| `worker_acknowledge_emergency_stop` | [81-135](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L81-L135) | Receive and wait for resume |
| `worker_initiate_resume` | [137-196](https://github.com/Enjot/ropeway-simulation/blob/main/src/common/worker_emergency.c#L137-L196) | Resume after cooldown |

### Logger ([src/core/logger.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `logger_init` | [30-34](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L30-L34) | Initialize with component type |
| `log_msg` | [64-106](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L64-L106) | Formatted log output |
| `log_signal_safe` | [108-112](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L108-L112) | Async-signal-safe logging |
| `int_to_str` | [114-149](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/logger.c#L114-L149) | Signal-safe int to string |

### Configuration ([src/core/config.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `config_set_defaults` | [17-45](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c#L17-L45) | Initialize defaults |
| `config_load` | [56-146](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c#L56-L146) | Load from file |
| `config_validate` | [156-233](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/config.c#L156-L233) | Validate values |

### Report ([src/core/report.c](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/report.c))
| Function | Lines | Purpose |
|----------|-------|---------|
| `print_report` | [13-67](https://github.com/Enjot/ropeway-simulation/blob/main/src/core/report.c#L13-L67) | Final simulation summary |

## Configuration Parameters

Config file format: `KEY=VALUE` with `#` comments.

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `STATION_CAPACITY` | 50 | Max tourists in lower station |
| `SIMULATION_DURATION_REAL_SECONDS` | 120 | Real time duration |
| `SIM_START_HOUR`/`SIM_START_MINUTE` | 8:00 | Simulated start time |
| `SIM_END_HOUR`/`SIM_END_MINUTE` | 17:00 | Simulated end time |
| `CHAIR_TRAVEL_TIME_SIM_MINUTES` | 5 | Chair ride duration |
| `TOTAL_TOURISTS` | 100 | Tourists to generate |
| `TOURIST_SPAWN_DELAY_US` | 200000 | Spawn delay (microseconds) |
| `VIP_PERCENTAGE` | 1 | VIP tourist percentage |
| `WALKER_PERCENTAGE` | 50 | Walker vs cyclist ratio |
| `TRAIL_WALK_TIME_SIM_MINUTES` | 30 | Walking trail duration |
| `TRAIL_BIKE_*_TIME_SIM_MINUTES` | 15/25/40 | Bike trail durations |
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

#### [test1_capacity.sh](tests/test1_capacity.sh) - Lower Station Capacity Limit
- **Goal**: Station visitor count never exceeds N
- **Rationale**: Tests for race condition between 4 entry gates incrementing station count via `SEM_LOWER_STATION`. Without proper semaphore synchronization, concurrent `sem_wait()` calls could allow count > N briefly.
- **Parameters**: `station_capacity=5`, `tourists=30`, `simulation_time=60s`
- **Expected**: Max observed count ≤ 5. No zombies. IPC cleaned.

#### [test2_children.sh](tests/test2_children.sh) - Children Under 8 with Guardians
- **Goal**: Children board with guardians, max 2 kids per adult
- **Rationale**: Tests atomic multi-slot semaphore acquisition for families. `sem_wait_n(SEM_LOWER_STATION, family_size)` must be atomic - partial acquisition would split family or cause deadlock if slots < family_size.
- **Parameters**: `walker_percentage=100` (high family probability), `tourists=50`
- **Expected**: Families board together. No adult has >2 kids. No deadlock.

#### [test3_vip.sh](tests/test3_vip.sh) - VIP Priority Without Queue Starvation
- **Goal**: VIPs served first, regular tourists not starved indefinitely
- **Rationale**: Tests `msgrcv()` with `mtype=-2` for priority ordering. If VIPs continuously arrive, `mtype=2` messages could starve. Verifies cashier eventually serves regular tourists between VIP bursts.
- **Parameters**: `vip_percentage=10`, `tourists=50`, `simulation_time=120s`
- **Expected**: VIPs board first. Regular tourists eventually served.

#### [test4_emergency.sh](tests/test4_emergency.sh) - Emergency Stop and Resume (Signals)
- **Goal**: SIGUSR1 stops chairlift, SIGUSR2 resumes after worker confirmation
- **Rationale**: Tests signal handler coordination via `SEM_EMERGENCY_LOCK` and `SEM_EMERGENCY_CLEAR`. Race condition possible if both workers try to initiate emergency simultaneously. Resume requires both workers to signal ready.
- **Parameters**: Manual SIGUSR1/SIGUSR2 injection during runtime
- **Expected**: Emergency stop logged. Resume only after both workers ready.

### Stress Tests (5-8)

#### [test5_stress.sh](tests/test5_stress.sh) - High Throughput Stress Test
- **Goal**: Simulation completes without timeout under high concurrency
- **Rationale**: Tests for deadlock between `MQ_CASHIER`, `MQ_PLATFORM`, `MQ_BOARDING` under load. 500 concurrent tourists stress all semaphores and message queues simultaneously - exposes circular wait conditions or semaphore exhaustion.
- **Parameters**: `tourists=500`, `spawn_delay=0`, `station_capacity=50`, `simulation_time=300s`
- **Expected**: No timeout (deadlock). No zombies. IPC cleaned.

#### [test6_race.sh](tests/test6_race.sh) - Entry Gate Race Condition Test
- **Goal**: Capacity N=5 never exceeded across 10 iterations
- **Rationale**: Tests TOCTOU race in `SEM_LOWER_STATION`. Multiple tourists calling `sem_wait()` simultaneously could interleave check-and-decrement operations. 10 iterations increases probability of catching intermittent race.
- **Parameters**: `tourists=50`, `spawn_delay=0`, `N=5`, 10 iterations
- **Expected**: Count ≤ 5 in all iterations. 100% pass rate.

#### [test7_emergency_race.sh](tests/test7_emergency_race.sh) - Emergency Race Condition Test
- **Goal**: Only one worker initiates emergency when both detect danger
- **Rationale**: Tests race on `SEM_EMERGENCY_LOCK` when lower_worker and upper_worker both call `sem_trywait()` simultaneously. Double-initiation would corrupt emergency state or cause deadlock waiting on `SEM_EMERGENCY_CLEAR`.
- **Parameters**: `danger_probability=100`, `tourists=30`, `simulation_time=60s`
- **Expected**: Single initiator per emergency. No deadlock. System recovers.

#### [test8_chair_saturation.sh](tests/test8_chair_saturation.sh) - Chair Saturation Test
- **Goal**: Tourists block on `SEM_CHAIRS` when all 36 chairs are in transit
- **Rationale**: Tests semaphore blocking behavior when `SEM_CHAIRS` reaches zero. Fast boarding + slow travel saturates chairs. Verifies `sem_wait()` blocks correctly and `sem_post()` from arrivals unblocks waiting tourists.
- **Parameters**: `chair_travel_time=30` sim minutes, `tourists=100`, `station_capacity=50`
- **Expected**: Max 36 chairs in transit. No deadlock on chair exhaustion.

### Edge Case Tests (9-13)

#### [test9_zero.sh](tests/test9_zero.sh) - Zero Tourists Edge Case
- **Goal**: Workers initialize and shutdown cleanly with no tourists
- **Rationale**: Tests for blocking on empty message queues. `msgrcv()` without `IPC_NOWAIT` on `MQ_CASHIER`/`MQ_PLATFORM` would hang forever. Workers must handle SIGTERM during idle wait without deadlock.
- **Parameters**: `tourists=0`, `simulation_time=15s`
- **Expected**: Clean shutdown. No hang on empty queues. No IPC leaks.

#### [test10_single.sh](tests/test10_single.sh) - Single Tourist Edge Case
- **Goal**: One tourist completes full lifecycle without concurrency
- **Rationale**: Baseline test - verifies message passing chain works: tourist→MQ_CASHIER→cashier→MQ_PLATFORM→lower_worker→MQ_BOARDING→tourist→MQ_ARRIVALS→upper_worker. No concurrency masks IPC bugs.
- **Parameters**: `tourists=1`, walker, `simulation_time=60s`
- **Expected**: Tourist completes ride. All IPC handoffs succeed.

#### [test11_capacity_one.sh](tests/test11_capacity_one.sh) - Capacity One Edge Case
- **Goal**: Only 1 tourist in station at any time with N=1
- **Rationale**: Tests convoy effect on `SEM_LOWER_STATION`. With N=1, all tourists serialize on single semaphore slot. Potential for priority inversion or starvation if `sem_wait()` ordering is unfair.
- **Parameters**: `N=1`, `tourists=10`, `simulation_time=120s`
- **Expected**: Max count=1. All tourists eventually served. No deadlock.

#### [test12_all_vip.sh](tests/test12_all_vip.sh) - All VIPs Edge Case
- **Goal**: All tourists served when everyone has same mtype=1 priority
- **Rationale**: Tests `msgrcv()` behavior when all messages have identical mtype. Verifies FIFO ordering within same priority level. Edge case where priority differentiation provides no benefit - system must still function.
- **Parameters**: `vip_percentage=100`, `tourists=30`, `simulation_time=90s`
- **Expected**: FIFO order maintained. All tourists served. No starvation.

#### [test13_all_families.sh](tests/test13_all_families.sh) - All Families Edge Case
- **Goal**: Families board atomically without splitting
- **Rationale**: Tests `semop()` with `sem_op > 1` for atomic multi-slot acquisition. Family of 3 must acquire 3 slots atomically - partial acquisition would split family or deadlock if remaining capacity < family_size.
- **Parameters**: `walker_percentage=100` (maximizes family probability), `tourists=40`
- **Expected**: No split families. Atomic acquisition verified. No deadlock.

### Recovery Tests (14-16)

#### [test14_sigterm.sh](tests/test14_sigterm.sh) - SIGTERM Cleanup Test
- **Goal**: No orphaned IPC resources after SIGTERM shutdown
- **Rationale**: Tests `ipc_cleanup()` is called from SIGTERM handler. Semaphores, shared memory, and message queues must be released via `semctl(RMID)`, `shmctl(RMID)`, `msgctl(RMID)`. Leaked IPC persists until reboot.
- **Parameters**: Run 10s, send SIGTERM, verify ipcs shows no resources
- **Expected**: ipcs empty after shutdown. No zombies. All children terminated.

#### [test15_sigkill_recovery.sh](tests/test15_sigkill_recovery.sh) - SIGKILL Recovery Test
- **Goal**: New run cleans orphaned IPC from previous crash
- **Rationale**: SIGKILL bypasses signal handlers - no `ipc_cleanup()` runs. Tests `ipc_cleanup_stale()` at startup: must detect orphaned resources via `ftok()` key collision and remove before creating new IPC objects.
- **Parameters**: SIGKILL first run, start second run, verify stale cleanup
- **Expected**: Second run succeeds. Stale IPC cleaned. Log shows cleanup.

#### [test16_child_death.sh](tests/test16_child_death.sh) - Child Death Test
- **Goal**: Main process shuts down gracefully when child dies unexpectedly
- **Rationale**: Tests SIGCHLD handler and zombie reaper thread. Killing cashier mid-run must trigger: `waitpid()` reaps child, main detects worker death, initiates shutdown. Without proper handling: zombies or orphaned children.
- **Parameters**: Kill cashier process mid-simulation
- **Expected**: Main detects death. Graceful shutdown. No zombies or IPC leaks.

### Signal Tests (17-18)

#### [test17_pause_resume.sh](tests/test17_pause_resume.sh) - Pause/Resume Test (SIGTSTP/SIGCONT)
- **Goal**: Simulated time offset adjusts for pause duration
- **Rationale**: Tests SIGTSTP/SIGCONT handlers in time_server. Pause duration must be added to time_offset so accelerated simulation time doesn't jump. Without offset adjustment: tickets expire during pause, time discontinuity.
- **Parameters**: SIGTSTP for 5s, then SIGCONT
- **Expected**: Time continues smoothly after resume. No expired tickets.

#### [test18_rapid_signals.sh](tests/test18_rapid_signals.sh) - Rapid Signals Test
- **Goal**: No crash under rapid signal delivery
- **Rationale**: Tests signal handler reentrancy. Rapid SIGUSR1 can interrupt handler mid-execution. Non-async-signal-safe functions (`malloc`, `printf`) in handler cause undefined behavior. Must use only safe functions.
- **Parameters**: 10 SIGUSR1 signals with 100ms delay
- **Expected**: No segfault. No hang. Simulation survives signal storm.

### Sync Correctness Tests (19)

#### [test19_sigalrm_sync.sh](tests/test19_sigalrm_sync.sh) - SIGALRM Sync Test
- **Goal**: Verify simulation works with SIGALRM-based sync (no usleep polling)
- **Rationale**: Confirms refactored code uses blocking IPC + `alarm()` correctly. All `IPC_NOWAIT`+usleep polling has been replaced with blocking calls that use SIGALRM for periodic wakeup. EINTR handled properly on alarm interrupt.
- **Parameters**: `tourists=20`, `simulation_time=60s` (uses test1_capacity.conf)
- **Expected**: Simulation completes. No deadlock. No leftover IPC.

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
