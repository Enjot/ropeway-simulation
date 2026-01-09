cmake_minimum_required(VERSION 4.0)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Header files list (for IDE integration)
set(COMMON_HEADERS
        include/common/config.hpp
        include/common/gate_type.hpp
        include/common/ropeway_state.hpp
        include/common/station_type.hpp
        include/common/ticket_type.hpp
        include/common/tourist_type.hpp
        include/common/trail_difficulty.hpp
        include/common/worker_signal.hpp
)

set(IPC_HEADERS
        include/ipc/cashier_message.hpp
        include/ipc/MessageQueue.hpp
        include/ipc/ropeway_system_state.hpp
        include/ipc/semaphore_index.hpp
        include/ipc/Semaphore.hpp
        include/ipc/SharedMemory.hpp
        include/ipc/worker_message.hpp
)

set(STRUCTURE_HEADERS
        include/structures/chair.hpp
        include/structures/daily_statistic.hpp
        include/structures/gate_passage.hpp
        include/structures/ticket.hpp
        include/structures/tourist.hpp
)

set(GATE_HEADERS
        include/gates/EntryGate.hpp
        include/gates/RideGate.hpp
)

set(UTILS_HEADERS
        include/utils/GatePassageLogger.hpp
)

set(TEST_HEADERS
        include/tests/TestConfig.hpp
        include/tests/TestValidator.hpp
        include/tests/TestRunner.hpp
)

# All headers combined
set(ALL_HEADERS
        ${COMMON_HEADERS}
        ${IPC_HEADERS}
        ${STRUCTURE_HEADERS}
        ${GATE_HEADERS}
        ${UTILS_HEADERS}
        ${TEST_HEADERS}
)

# Main simulation controller
add_executable(ropeway_simulation main.cpp ${ALL_HEADERS})

# Tourist process (spawned via fork/exec)
add_executable(tourist_process src/processes/tourist_process.cpp ${ALL_HEADERS})

# Worker1 process - Lower Station Controller (spawned via fork/exec)
add_executable(worker1_process src/processes/worker1_process.cpp ${ALL_HEADERS})

# Worker2 process - Upper Station Controller (spawned via fork/exec)
add_executable(worker2_process src/processes/worker2_process.cpp ${ALL_HEADERS})

# Cashier process - Ticket sales (spawned via fork/exec)
add_executable(cashier_process src/processes/cashier_process.cpp ${ALL_HEADERS})

# Test runner - Automated test suite (Phase 10)
add_executable(test_runner src/tests/test_runner.cpp ${ALL_HEADERS})
