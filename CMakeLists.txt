cmake_minimum_required(VERSION 4.0)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# === Header files by feature (for IDE integration) ===

set(CORE_HEADERS
        core/Config.hpp
        core/Constants.hpp
        core/RopewayState.hpp
        core/Simulation.hpp
)

set(TOURIST_HEADERS
        tourist/Tourist.hpp
        tourist/TouristState.hpp
        tourist/TouristType.hpp
)

set(ENTRANCE_HEADERS
        entrance/Ticket.hpp
        entrance/TicketName.hpp
        entrance/CashierMessage.hpp
)

set(ROPEWAY_HEADERS
        ropeway/StationType.hpp
        ropeway/TrailDifficulty.hpp
        ropeway/worker/WorkerMessage.hpp
        ropeway/worker/WorkerSignal.hpp
        ropeway/gate/GateType.hpp
        ropeway/gate/EntryGateMessage.hpp
        ropeway/chair/Chair.hpp
        ropeway/chair/BoardingQueue.hpp
)

set(LOGGING_HEADERS
        logging/Logger.hpp
        logging/GatePassageLogger.hpp
)

set(IPC_HEADERS
        ipc/IpcManager.hpp
        ipc/core/SharedMemory.hpp
        ipc/core/Semaphore.hpp
        ipc/core/MessageQueue.hpp
        ipc/core/IpcException.hpp
        ipc/model/SharedRopewayState.hpp
        ipc/model/SharedOperationalState.hpp
        ipc/model/SharedChairPoolState.hpp
        ipc/model/SharedStatisticState.hpp
)

set(STATS_HEADERS
        stats/DailyStatistic.hpp
        stats/GatePassage.hpp
        stats/GatePassageLog.hpp
        stats/TouristRideRecord.hpp
)

set(UTILS_HEADERS
        utils/SignalHelper.hpp
        utils/ArgumentParser.hpp
        utils/ProcessSpawner.hpp
        utils/TimeHelper.hpp
)

set(TEST_HEADERS
        tests/TestConfig.hpp
        tests/TestValidator.hpp
        tests/TestRunner.hpp
)

# All headers combined
set(ALL_HEADERS
        ${CORE_HEADERS}
        ${TOURIST_HEADERS}
        ${ENTRANCE_HEADERS}
        ${ROPEWAY_HEADERS}
        ${LOGGING_HEADERS}
        ${IPC_HEADERS}
        ${STATS_HEADERS}
        ${UTILS_HEADERS}
        ${TEST_HEADERS}
)

# === Executables ===

# Tourist process (spawned via fork/exec)
add_executable(tourist_process tourist/TouristProcess.cpp ${ALL_HEADERS})

# LowerWorker process - Lower Station Controller (spawned via fork/exec)
add_executable(lower_worker_process ropeway/worker/LowerStationWorker.cpp ${ALL_HEADERS})

# UpperWorker process - Upper Station Controller (spawned via fork/exec)
add_executable(upper_worker_process ropeway/worker/UpperStationWorker.cpp ${ALL_HEADERS})

# Cashier process - Ticket sales (spawned via fork/exec)
add_executable(cashier_process entrance/CashierProcess.cpp ${ALL_HEADERS})

# Test runner - Automated test suite
add_executable(test_runner tests/TestRunner.cpp ${ALL_HEADERS})

# Main executable - Manual simulation control
add_executable(main core/main.cpp ${ALL_HEADERS})
