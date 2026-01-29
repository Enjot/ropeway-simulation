cmake_minimum_required(VERSION 3.16)
project(ropeway_simulation)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Include directories
include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}  # For backward compatibility during transition
)

# === Header files (in include/) ===

set(CORE_HEADERS
        include/core/Config.h
        include/core/Constants.h
        include/core/Flags.h
        include/core/RopewayState.h
        include/core/Simulation.h
)

set(TOURIST_HEADERS
        include/tourist/Tourist.h
        include/tourist/TouristState.h
        include/tourist/TouristType.h
)

set(ENTRANCE_HEADERS
        include/entrance/Ticket.h
        include/entrance/TicketName.h
        include/entrance/CashierMessage.h
)

set(ROPEWAY_HEADERS
        include/ropeway/StationType.h
        include/ropeway/TrailDifficulty.h
        include/ropeway/worker/WorkerMessage.h
        include/ropeway/worker/WorkerSignal.h
        include/ropeway/gate/GateType.h
        include/ropeway/gate/EntryGateMessage.h
        include/ropeway/chair/Chair.h
        include/ropeway/chair/BoardingQueue.h
)

set(LOGGING_HEADERS
        include/logging/Logger.h
        include/logging/LogMessage.h
        include/logging/GatePassageLogger.h
)

set(IPC_HEADERS
        include/ipc/IpcManager.h
        include/ipc/core/SharedMemory.h
        include/ipc/core/Semaphore.h
        include/ipc/core/MessageQueue.h
        include/ipc/core/IpcException.h
        include/ipc/model/SharedRopewayState.h
        include/ipc/model/SharedOperationalState.h
        include/ipc/model/SharedChairPoolState.h
        include/ipc/model/SharedStatisticState.h
)

set(STATS_HEADERS
        include/stats/DailyStatistic.h
        include/stats/GatePassage.h
        include/stats/GatePassageLog.h
        include/stats/TouristRideRecord.h
)

set(UTILS_HEADERS
        include/utils/SignalHelper.h
        include/utils/ArgumentParser.h
        include/utils/ProcessSpawner.h
        include/utils/TimeHelper.h
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
)

# === Source files (in src/) ===

set(IPC_SOURCES
        src/ipc/Semaphore.cpp
)

set(LOGGING_SOURCES
        src/logging/Logger.cpp
)

# === Executables ===

# Common sources for all executables
set(COMMON_SOURCES
        ${IPC_SOURCES}
        ${LOGGING_SOURCES}
)

# Tourist process (spawned via fork/exec)
add_executable(tourist_process
        src/tourist/TouristProcess.cpp
        ${COMMON_SOURCES}
        ${ALL_HEADERS}
)

# LowerWorker process - Lower Station Controller (spawned via fork/exec)
add_executable(lower_worker_process
        src/ropeway/LowerStationWorker.cpp
        ${COMMON_SOURCES}
        ${ALL_HEADERS}
)

# UpperWorker process - Upper Station Controller (spawned via fork/exec)
add_executable(upper_worker_process
        src/ropeway/UpperStationWorker.cpp
        ${COMMON_SOURCES}
        ${ALL_HEADERS}
)

# Cashier process - Ticket sales (spawned via fork/exec)
add_executable(cashier_process
        src/entrance/CashierProcess.cpp
        ${COMMON_SOURCES}
        ${ALL_HEADERS}
)

# Logger process - Centralized log collection (spawned via fork/exec)
add_executable(logger_process
        src/logging/LoggerProcess.cpp
        ${COMMON_SOURCES}
        ${ALL_HEADERS}
)

# Main executable - Manual simulation control
add_executable(main
        src/core/main.cpp
        ${COMMON_SOURCES}
        ${ALL_HEADERS}
)

# Link pthread (required on Linux for std::thread)
target_link_libraries(tourist_process Threads::Threads)
target_link_libraries(main Threads::Threads)

# Pass project source directory for .env file loading
add_compile_definitions(ROPEWAY_PROJECT_DIR="${CMAKE_SOURCE_DIR}")

# === Test target ===
# Run integration tests using shell scripts
add_custom_target(test
    COMMAND ${CMAKE_SOURCE_DIR}/tests/run_all_tests.sh ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS main tourist_process cashier_process lower_worker_process upper_worker_process logger_process
    COMMENT "Running integration tests..."
)
