cmake_minimum_required(VERSION 4.0)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Header files list (for IDE integration)
set(COMMON_HEADERS
        Config.hpp
        enums/GateType.hpp
        enums/RopewayState.hpp
        enums/StationType.hpp
        enums/TicketName.hpp
        enums/TouristType.hpp
        enums/TrailDifficulty.hpp
        enums/WorkerSignal.hpp
        enums/TouristState.hpp
)

set(IPC_HEADERS
        ipc/model/BoardingQueue.hpp
        ipc/message/CashierMessage.hpp
        ipc/core/MessageQueue.hpp
        ipc/model/ReportingData.hpp
        ipc/model/SharedRopewayState.hpp
        ipc/model/SharedOperationalState.hpp
        ipc/model/SharedChairPoolState.hpp
        ipc/model/SharedStatisticState.hpp
        ipc/core/SharedMemory.hpp
        ipc/message/WorkerMessage.hpp
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
        ipc/message/EntryGateMessage.hpp
)

set(UTILS_HEADERS
        utils/GatePassageLogger.hpp
        utils/Logger.hpp
        utils/SignalHelper.hpp
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
add_executable(tourist_process processes/TouristProcess.cpp ${ALL_HEADERS})

# LowerWorker process - Lower Station Controller (spawned via fork/exec)
add_executable(lower_worker_process processes/LowerStationWorker.cpp ${ALL_HEADERS})

# UpperWorker process - Upper Station Controller (spawned via fork/exec)
add_executable(upper_worker_process processes/UpperStationWorker.cpp ${ALL_HEADERS})

# Cashier process - Ticket sales (spawned via fork/exec)
add_executable(cashier_process processes/CashierProcess.cpp ${ALL_HEADERS}
        ipc/core/IpcException.hpp)

# Test runner - Automated test suite (Phase 10)
add_executable(test_runner tests/TestRunner.cpp ${ALL_HEADERS})

# Main executable - Manual simulation control
add_executable(main main.cpp ${ALL_HEADERS})
