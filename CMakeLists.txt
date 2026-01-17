cmake_minimum_required(VERSION 4.0)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Header files list (for IDE integration)
set(COMMON_HEADERS
        common/config.hpp
        common/GateType.hpp
        common/RopewayState.hpp
        common/StationType.hpp
        common/TicketName.hpp
        common/TouristType.hpp
        common/TrailDifficulty.hpp
        common/WorkerSignal.hpp
)

set(IPC_HEADERS
        ipc/BoardingQueue.hpp
        ipc/CashierMessage.hpp
        ipc/MessageQueue.hpp
        ipc/ReportingData.hpp
        ipc/RopewaySystemState.hpp
        ipc/SemaphoreIndex.hpp
        ipc/Semaphore.hpp
        ipc/SharedMemory.hpp
        ipc/WorkerMessage.hpp
        ipc/IpcManager.hpp
)

set(STRUCTURE_HEADERS
        structures/chair.hpp
        structures/DailyStatistic.hpp
        structures/GatePassage.hpp
        structures/ticket.hpp
        structures/tourist.hpp
)

set(GATE_HEADERS
        gates/EntryGate.hpp
        gates/RideGate.hpp
)

set(UTILS_HEADERS
        utils/GatePassageLogger.hpp
        utils/Logger.hpp
        utils/SignalHelper.hpp
        utils/EnumStrings.hpp
        utils/ArgumentParser.hpp
        utils/ProcessSpawner.hpp
)

set(TEST_HEADERS
        tests/TestConfig.hpp
        tests/TestValidator.hpp
        tests/TestRunner.hpp
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

# Tourist process (spawned via fork/exec)
add_executable(tourist_process processes/Touristprocess.cpp ${ALL_HEADERS})

# Worker1 process - Lower Station Controller (spawned via fork/exec)
add_executable(worker1_process processes/worker1_process.cpp ${ALL_HEADERS})

# Worker2 process - Upper Station Controller (spawned via fork/exec)
add_executable(worker2_process processes/worker2_process.cpp ${ALL_HEADERS})

# Cashier process - Ticket sales (spawned via fork/exec)
add_executable(cashier_process processes/CashierProcess.cpp ${ALL_HEADERS})

# Test runner - Automated test suite (Phase 10)
add_executable(test_runner tests/testRunner.cpp ${ALL_HEADERS})

# Main executable - Manual simulation control
add_executable(main main.cpp ${ALL_HEADERS})
